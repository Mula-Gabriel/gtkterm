/***********************************************************************/
/* script_engine.c                                                     */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Lua 5.4 scripting engine for GTKTerm                          */
/*      Provides serial/TCP port control, pattern matching,            */
/*      macro invocation, CSV logging from Lua scripts.               */
/***********************************************************************/

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "script_engine.h"
#include "buffer.h"
#include "macros.h"
#include "macros_format.h"
#include "macros_list.h"

/* Forward declaration to avoid pulling in vte headers */
extern gint send_serial(gchar *string, gint len);

#define MAX_LUA_PORTS 16
#define RX_BUF_MAX    (256 * 1024)

typedef enum { PORT_NONE, PORT_SERIAL, PORT_TCP_CLIENT, PORT_TCP_SERVER } PortType;

typedef struct {
    int           fd;
    int           listen_fd;
    PortType      type;
    GIOChannel   *chan_in;
    GIOChannel   *chan_err;
    guint         watch_in;
    guint         watch_err;
    GMutex        mutex;
    GString      *rx_buf;
    gboolean      open;
    /* serial params */
    int           baudrate;
    int           bits;
    int           stops;
    int           parity;   /* 0=none 1=odd 2=even */
    int           flow;     /* 0=none 1=xon/xoff 2=rts/cts */
} lua_port_t;

static lua_port_t  ports[MAX_LUA_PORTS];
static lua_State  *L_global = NULL;
static GThread    *lua_thread = NULL;
static volatile gboolean stop_requested = FALSE;
static GMutex      engine_mutex;

static ScriptLogFunc  log_func  = NULL;
static gpointer       log_data  = NULL;
static ScriptDoneFunc done_func = NULL;
static gpointer       done_data = NULL;

/* ------------------------------------------------------------------ */
/* Logging helpers                                                     */
/* ------------------------------------------------------------------ */

typedef struct { char *text; } LogIdleData;

static gboolean log_idle_cb(gpointer user_data)
{
    LogIdleData *d = user_data;
    if (log_func)
        log_func(d->text, log_data);
    g_free(d->text);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void engine_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *text = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    LogIdleData *d = g_new(LogIdleData, 1);
    d->text = text;
    g_idle_add(log_idle_cb, d);
}

/* ------------------------------------------------------------------ */
/* Main port tap (handle 0)                                            */
/* ------------------------------------------------------------------ */

static void lua_main_port_tap(const char *data, unsigned int size)
{
    lua_port_t *p = &ports[0];
    if (!p->open) return;
    g_mutex_lock(&p->mutex);
    if (p->rx_buf->len + size <= RX_BUF_MAX)
        g_string_append_len(p->rx_buf, data, size);
    g_mutex_unlock(&p->mutex);
}

/* ------------------------------------------------------------------ */
/* GIOChannel callbacks for Lua-opened ports                          */
/* ------------------------------------------------------------------ */

static gboolean port_read_cb(GIOChannel *src, GIOCondition cond, gpointer user_data)
{
    lua_port_t *p = user_data;
    char buf[4096];
    gsize bytes_read = 0;
    GError *err = NULL;

    GIOStatus status = g_io_channel_read_chars(src, buf, sizeof(buf), &bytes_read, &err);
    if (err) { g_error_free(err); err = NULL; }

    if (bytes_read > 0) {
        g_mutex_lock(&p->mutex);
        if (p->rx_buf->len + bytes_read <= RX_BUF_MAX)
            g_string_append_len(p->rx_buf, buf, bytes_read);
        g_mutex_unlock(&p->mutex);
    }

    if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR) {
        p->watch_in = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean port_err_cb(GIOChannel *src, GIOCondition cond, gpointer user_data)
{
    lua_port_t *p = user_data;
    p->watch_err = 0;
    return G_SOURCE_REMOVE;
}

static gboolean tcp_accept_cb(GIOChannel *src, GIOCondition cond, gpointer user_data)
{
    lua_port_t *p = user_data;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int client = accept(p->listen_fd, (struct sockaddr *)&addr, &addrlen);
    if (client < 0) return G_SOURCE_CONTINUE;

    /* Replace listen watch with client fd */
    if (p->watch_in) { g_source_remove(p->watch_in); p->watch_in = 0; }
    if (p->chan_in)  { g_io_channel_unref(p->chan_in); p->chan_in = NULL; }

    p->fd = client;
    p->chan_in = g_io_channel_unix_new(client);
    g_io_channel_set_encoding(p->chan_in, NULL, NULL);
    g_io_channel_set_buffered(p->chan_in, FALSE);
    p->watch_in = g_io_add_watch(p->chan_in, G_IO_IN | G_IO_HUP | G_IO_ERR, port_read_cb, p);
    p->watch_err = g_io_add_watch(p->chan_in, G_IO_ERR | G_IO_HUP, port_err_cb, p);

    /* close listen fd */
    close(p->listen_fd);
    p->listen_fd = -1;

    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* Serial port helpers                                                 */
/* ------------------------------------------------------------------ */

static int baud_constant(int baud)
{
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default:     return B9600;
    }
}

static int configure_serial(int fd, int baud, int bits, int stops, int parity, int flow)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return -1;

    cfmakeraw(&tio);
    cfsetispeed(&tio, baud_constant(baud));
    cfsetospeed(&tio, baud_constant(baud));

    tio.c_cflag &= ~CSIZE;
    switch (bits) {
    case 5: tio.c_cflag |= CS5; break;
    case 6: tio.c_cflag |= CS6; break;
    case 7: tio.c_cflag |= CS7; break;
    default:tio.c_cflag |= CS8; break;
    }

    if (stops == 2) tio.c_cflag |= CSTOPB; else tio.c_cflag &= ~CSTOPB;

    tio.c_cflag &= ~(PARENB | PARODD);
    if (parity == 1) { tio.c_cflag |= PARENB | PARODD; }
    else if (parity == 2) { tio.c_cflag |= PARENB; }

    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (flow == 1) { tio.c_iflag |= IXON | IXOFF; }
    else if (flow == 2) { tio.c_cflag |= CRTSCTS; }

    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1;

    return tcsetattr(fd, TCSANOW, &tio);
}

/* ------------------------------------------------------------------ */
/* Lua API: open(handle, ...) / close(handle)                         */
/* ------------------------------------------------------------------ */

static int l_open(lua_State *L)
{
    int handle = (int)luaL_checkinteger(L, 1);
    if (handle < 1 || handle >= MAX_LUA_PORTS) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "invalid handle");
        return 2;
    }

    lua_port_t *p = &ports[handle];
    if (p->open) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "already open");
        return 2;
    }

    const char *type_str = luaL_checkstring(L, 2);

    if (strcmp(type_str, "serial") == 0) {
        const char *device = luaL_checkstring(L, 3);
        p->baudrate = (int)luaL_optinteger(L, 4, 9600);
        p->bits     = (int)luaL_optinteger(L, 5, 8);
        p->stops    = (int)luaL_optinteger(L, 6, 1);
        p->parity   = (int)luaL_optinteger(L, 7, 0);
        p->flow     = (int)luaL_optinteger(L, 8, 0);

        int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, strerror(errno));
            return 2;
        }
        if (configure_serial(fd, p->baudrate, p->bits, p->stops, p->parity, p->flow) != 0) {
            close(fd);
            lua_pushboolean(L, 0);
            lua_pushstring(L, "configure_serial failed");
            return 2;
        }
        p->fd = fd;
        p->type = PORT_SERIAL;

    } else if (strcmp(type_str, "tcp_client") == 0) {
        const char *host = luaL_checkstring(L, 3);
        int port = (int)luaL_checkinteger(L, 4);

        struct addrinfo hints = {0}, *res = NULL;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "getaddrinfo failed");
            return 2;
        }
        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) { freeaddrinfo(res); lua_pushboolean(L, 0); lua_pushstring(L, strerror(errno)); return 2; }
        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            close(fd); freeaddrinfo(res);
            lua_pushboolean(L, 0); lua_pushstring(L, strerror(errno)); return 2;
        }
        freeaddrinfo(res);
        fcntl(fd, F_SETFL, O_NONBLOCK);
        p->fd   = fd;
        p->type = PORT_TCP_CLIENT;

    } else if (strcmp(type_str, "tcp_server") == 0) {
        int port = (int)luaL_checkinteger(L, 3);
        int lfd = socket(AF_INET, SOCK_STREAM, 0);
        if (lfd < 0) { lua_pushboolean(L, 0); lua_pushstring(L, strerror(errno)); return 2; }
        int one = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port);
        if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(lfd, 1) != 0) {
            close(lfd); lua_pushboolean(L, 0); lua_pushstring(L, strerror(errno)); return 2;
        }
        p->listen_fd = lfd;
        p->fd        = -1;
        p->type      = PORT_TCP_SERVER;

        GIOChannel *lchan = g_io_channel_unix_new(lfd);
        g_io_channel_set_encoding(lchan, NULL, NULL);
        g_io_channel_set_buffered(lchan, FALSE);
        p->chan_in   = lchan;
        p->watch_in  = g_io_add_watch(lchan, G_IO_IN, tcp_accept_cb, p);
        p->open = TRUE;
        lua_pushboolean(L, 1);
        return 1;

    } else {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "unknown type (serial/tcp_client/tcp_server)");
        return 2;
    }

    /* For serial and tcp_client: set up GIOChannel */
    p->chan_in = g_io_channel_unix_new(p->fd);
    g_io_channel_set_encoding(p->chan_in, NULL, NULL);
    g_io_channel_set_buffered(p->chan_in, FALSE);
    p->watch_in  = g_io_add_watch(p->chan_in, G_IO_IN | G_IO_HUP | G_IO_ERR, port_read_cb, p);
    p->watch_err = g_io_add_watch(p->chan_in, G_IO_ERR | G_IO_HUP, port_err_cb, p);
    p->open = TRUE;

    lua_pushboolean(L, 1);
    return 1;
}

static int l_close(lua_State *L)
{
    int handle = (int)luaL_checkinteger(L, 1);
    if (handle < 1 || handle >= MAX_LUA_PORTS) return 0;

    lua_port_t *p = &ports[handle];
    if (!p->open) return 0;

    if (p->watch_in)  { g_source_remove(p->watch_in);  p->watch_in  = 0; }
    if (p->watch_err) { g_source_remove(p->watch_err); p->watch_err = 0; }
    if (p->chan_in)   { g_io_channel_unref(p->chan_in); p->chan_in   = NULL; }
    if (p->fd >= 0)   { close(p->fd); p->fd = -1; }
    if (p->listen_fd >= 0) { close(p->listen_fd); p->listen_fd = -1; }

    g_mutex_lock(&p->mutex);
    g_string_truncate(p->rx_buf, 0);
    g_mutex_unlock(&p->mutex);

    p->open = FALSE;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lua API: send(handle, data)                                        */
/* ------------------------------------------------------------------ */

static int l_send(lua_State *L)
{
    int handle = (int)luaL_checkinteger(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);

    if (handle == 0) {
        send_serial((gchar *)data, (gint)len);
        lua_pushboolean(L, 1);
        return 1;
    }

    if (handle < 1 || handle >= MAX_LUA_PORTS) {
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_port_t *p = &ports[handle];
    if (!p->open || p->fd < 0) { lua_pushboolean(L, 0); return 1; }

    ssize_t written = write(p->fd, data, len);
    lua_pushboolean(L, written == (ssize_t)len);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: send_hex(handle, hex_string)                              */
/* ------------------------------------------------------------------ */

static int l_send_hex(lua_State *L)
{
    int handle = (int)luaL_checkinteger(L, 1);
    const char *hex = luaL_checkstring(L, 2);

    size_t hlen = strlen(hex);
    char *bin = g_malloc(hlen / 2 + 1);
    size_t bin_len = 0;

    for (size_t i = 0; i + 1 < hlen; ) {
        while (i < hlen && (hex[i] == ' ' || hex[i] == '\t')) i++;
        if (i + 1 >= hlen) break;
        char byte_str[3] = { hex[i], hex[i+1], 0 };
        bin[bin_len++] = (char)strtol(byte_str, NULL, 16);
        i += 2;
    }

    if (handle == 0) {
        send_serial(bin, (gint)bin_len);
    } else if (handle >= 1 && handle < MAX_LUA_PORTS) {
        lua_port_t *p = &ports[handle];
        if (p->open && p->fd >= 0)
            write(p->fd, bin, bin_len);
    }

    g_free(bin);
    lua_pushboolean(L, 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: wait_for(handle, pattern, timeout_ms) -> matched, data   */
/* ------------------------------------------------------------------ */

static int l_wait_for(lua_State *L)
{
    int handle     = (int)luaL_checkinteger(L, 1);
    const char *pat = luaL_checkstring(L, 2);
    int timeout_ms  = (int)luaL_optinteger(L, 3, 5000);

    if (handle < 0 || handle >= MAX_LUA_PORTS) {
        lua_pushnil(L);
        return 1;
    }

    lua_port_t *p = &ports[handle];
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (!stop_requested) {
        g_mutex_lock(&p->mutex);
        char *found = g_strstr_len(p->rx_buf->str, (gssize)p->rx_buf->len, pat);
        if (found) {
            size_t end = (size_t)(found - p->rx_buf->str) + strlen(pat);
            char *captured = g_strndup(p->rx_buf->str, end);
            g_string_erase(p->rx_buf, 0, (gssize)end);
            g_mutex_unlock(&p->mutex);
            lua_pushstring(L, captured);
            g_free(captured);
            return 1;
        }
        g_mutex_unlock(&p->mutex);

        if (g_get_monotonic_time() >= deadline) {
            lua_pushnil(L);
            return 1;
        }
        g_usleep(10000); /* 10 ms */
    }

    lua_pushnil(L);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: read(handle) -> data                                      */
/* ------------------------------------------------------------------ */

static int l_read(lua_State *L)
{
    int handle = (int)luaL_checkinteger(L, 1);
    if (handle < 0 || handle >= MAX_LUA_PORTS) { lua_pushstring(L, ""); return 1; }

    lua_port_t *p = &ports[handle];
    g_mutex_lock(&p->mutex);
    lua_pushlstring(L, p->rx_buf->str, p->rx_buf->len);
    g_string_truncate(p->rx_buf, 0);
    g_mutex_unlock(&p->mutex);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: clear(handle)                                             */
/* ------------------------------------------------------------------ */

static int l_clear(lua_State *L)
{
    int handle = (int)luaL_checkinteger(L, 1);
    if (handle < 0 || handle >= MAX_LUA_PORTS) return 0;

    lua_port_t *p = &ports[handle];
    g_mutex_lock(&p->mutex);
    g_string_truncate(p->rx_buf, 0);
    g_mutex_unlock(&p->mutex);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lua API: sleep(ms)                                                 */
/* ------------------------------------------------------------------ */

static int l_sleep(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    gint64 deadline = g_get_monotonic_time() + (gint64)ms * 1000;
    while (!stop_requested && g_get_monotonic_time() < deadline)
        g_usleep(10000);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lua API: time() -> seconds (float, monotonic wall-clock)          */
/* ------------------------------------------------------------------ */

static int l_time(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)g_get_monotonic_time() / 1e6);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: log(text)                                                 */
/* ------------------------------------------------------------------ */

static int l_log(lua_State *L)
{
    size_t len;
    const char *text = luaL_tolstring(L, 1, &len);
    engine_log("%s\n", text);
    lua_pop(L, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lua API: save_csv(filename, ...)                                   */
/* ------------------------------------------------------------------ */

static int l_save_csv(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    int n = lua_gettop(L);

    FILE *f = fopen(filename, "a");
    if (!f) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        return 2;
    }

    for (int i = 2; i <= n; i++) {
        if (i > 2) fprintf(f, ",");
        fprintf(f, "%s", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    fprintf(f, "\n");
    fclose(f);

    lua_pushboolean(L, 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: get_lists() -> {{name, entries={{display,value},...}}, ...} */
/* ------------------------------------------------------------------ */

static int l_get_lists(lua_State *L)
{
    gint n_lists = macro_list_count();
    lua_newtable(L);
    for (int i = 0; i < n_lists; i++) {
        lua_newtable(L);
        lua_pushstring(L, macro_list_name(i) ? macro_list_name(i) : "");
        lua_setfield(L, -2, "name");
        gint n_entries = macro_list_entry_count(i);
        lua_newtable(L);
        for (int e = 0; e < n_entries; e++) {
            lua_newtable(L);
            const gchar *disp = macro_list_entry_display(i, e);
            const gchar *val  = macro_list_entry_value(i, e);
            lua_pushstring(L, disp ? disp : "");
            lua_setfield(L, -2, "display");
            lua_pushstring(L, val  ? val  : "");
            lua_setfield(L, -2, "value");
            lua_rawseti(L, -2, e + 1);
        }
        lua_setfield(L, -2, "entries");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lua API: get_macros() -> table of {index, label}                   */
/* ------------------------------------------------------------------ */

static int l_get_macros(lua_State *L)
{
    gint count = 0;
    macro_t *macros = get_shortcuts(&count);
    lua_newtable(L);

    if (!macros) return 1;

    for (int i = 0; i < count; i++) {
        lua_newtable(L);
        lua_pushinteger(L, i);
        lua_setfield(L, -2, "index");
        lua_pushstring(L, macros[i].label ? macros[i].label : "");
        lua_setfield(L, -2, "label");

        gint n_args = 0;
        macro_arg_info_t *infos = macro_get_arg_infos(macros[i].action, &n_args);
        lua_pushinteger(L, n_args);
        lua_setfield(L, -2, "n_args");

        /* args = {{type, label, list}, ...} */
        lua_newtable(L);
        for (int a = 0; a < n_args; a++) {
            lua_newtable(L);
            lua_pushstring(L, infos[a].type      ? infos[a].type      : "s");
            lua_setfield(L, -2, "type");
            lua_pushstring(L, infos[a].label     ? infos[a].label     : "");
            lua_setfield(L, -2, "label");
            lua_pushstring(L, infos[a].list_name ? infos[a].list_name : "");
            lua_setfield(L, -2, "list");
            lua_rawseti(L, -2, a + 1);
        }
        lua_setfield(L, -2, "args");
        macro_arg_infos_free(infos, n_args);

        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Macro send idle callback                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int    index;
    int    n_args;
    gchar **args;
} SendMacroData;

static gboolean send_macro_idle(gpointer user_data)
{
    SendMacroData *d = user_data;
    gint count = 0;
    macro_t *macros = get_shortcuts(&count);
    if (macros && d->index >= 0 && d->index < count && macros[d->index].action) {
        gchar *formatted = format_action_with_args(macros[d->index].action,
                                                   (const gchar **)d->args,
                                                   d->n_args);
        GByteArray *buf = parse_macro_string(formatted);
        g_free(formatted);
        if (buf) {
            send_serial((gchar *)buf->data, buf->len);
            g_byte_array_free(buf, TRUE);
        }
    }
    g_strfreev(d->args);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* Lua API: send_macro(index_or_label [, arg1, arg2, ...])           */
/* ------------------------------------------------------------------ */

static int l_send_macro(lua_State *L)
{
    gint count = 0;
    macro_t *macros = get_shortcuts(&count);
    int idx = -1;

    if (lua_type(L, 1) == LUA_TNUMBER) {
        idx = (int)lua_tointeger(L, 1);
    } else {
        const char *label = luaL_checkstring(L, 1);
        if (macros) {
            for (int i = 0; i < count; i++) {
                if (macros[i].label && strcmp(macros[i].label, label) == 0) { idx = i; break; }
            }
        }
    }

    if (idx < 0 || idx >= count) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "macro not found");
        return 2;
    }

    int n_lua_args = lua_gettop(L) - 1;
    gchar **args = n_lua_args > 0 ? g_new0(gchar *, n_lua_args + 1) : NULL;
    for (int i = 0; i < n_lua_args; i++) {
        const char *s = lua_tostring(L, i + 2);
        args[i] = g_strdup(s ? s : "");
    }

    SendMacroData *d = g_new(SendMacroData, 1);
    d->index  = idx;
    d->n_args = n_lua_args;
    d->args   = args;
    g_idle_add(send_macro_idle, d);

    lua_pushboolean(L, 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Register API                                                       */
/* ------------------------------------------------------------------ */

static const luaL_Reg gtkterm_funcs[] = {
    { "open",       l_open       },
    { "close",      l_close      },
    { "send",       l_send       },
    { "send_hex",   l_send_hex   },
    { "wait_for",   l_wait_for   },
    { "read",       l_read       },
    { "clear",      l_clear      },
    { "sleep",      l_sleep      },
    { "time",       l_time       },
    { "log",        l_log        },
    { "save_csv",   l_save_csv   },
    { "get_lists",  l_get_lists  },
    { "get_macros", l_get_macros },
    { "send_macro", l_send_macro },
    { NULL, NULL }
};

static void register_api(lua_State *L)
{
    luaL_newlib(L, gtkterm_funcs);
    lua_setglobal(L, "gtkterm");

    /* Convenience: main port handle constant */
    lua_pushinteger(L, 0);
    lua_setglobal(L, "MAIN_PORT");
}

/* ------------------------------------------------------------------ */
/* Done idle callback                                                 */
/* ------------------------------------------------------------------ */

static gboolean done_idle_cb(gpointer user_data)
{
    if (done_func)
        done_func(done_data);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* Lua thread                                                         */
/* ------------------------------------------------------------------ */

static gpointer lua_thread_func(gpointer user_data)
{
    char *script = user_data;

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    register_api(L);

    /* Tap the main port */
    set_tap_func(lua_main_port_tap);
    ports[0].open = TRUE;

    int ret = luaL_dostring(L, script);
    if (ret != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        engine_log("Lua error: %s\n", err ? err : "(unknown)");
    }

    /* Cleanup main port tap */
    unset_tap_func();
    ports[0].open = FALSE;
    g_mutex_lock(&ports[0].mutex);
    g_string_truncate(ports[0].rx_buf, 0);
    g_mutex_unlock(&ports[0].mutex);

    /* Close any Lua-opened ports */
    for (int i = 1; i < MAX_LUA_PORTS; i++) {
        if (ports[i].open) {
            lua_pushinteger(L, i);
            l_close(L);
            lua_pop(L, 0);
        }
    }

    lua_close(L);
    g_free(script);

    g_mutex_lock(&engine_mutex);
    L_global    = NULL;
    lua_thread  = NULL;
    g_mutex_unlock(&engine_mutex);

    g_idle_add(done_idle_cb, NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void script_engine_setup(void)
{
    g_mutex_init(&engine_mutex);
    for (int i = 0; i < MAX_LUA_PORTS; i++) {
        ports[i].fd        = -1;
        ports[i].listen_fd = -1;
        ports[i].type      = PORT_NONE;
        ports[i].chan_in   = NULL;
        ports[i].chan_err  = NULL;
        ports[i].watch_in  = 0;
        ports[i].watch_err = 0;
        ports[i].open      = FALSE;
        g_mutex_init(&ports[i].mutex);
        ports[i].rx_buf    = g_string_new(NULL);
    }
}

void script_engine_run(const char *script_text)
{
    g_mutex_lock(&engine_mutex);
    if (lua_thread != NULL) {
        g_mutex_unlock(&engine_mutex);
        engine_log("Script already running.\n");
        return;
    }
    stop_requested = FALSE;
    char *copy = g_strdup(script_text);
    lua_thread = g_thread_new("lua-script", lua_thread_func, copy);
    g_mutex_unlock(&engine_mutex);
}

void script_engine_stop(void)
{
    stop_requested = TRUE;
}

gboolean script_engine_is_running(void)
{
    g_mutex_lock(&engine_mutex);
    gboolean running = (lua_thread != NULL);
    g_mutex_unlock(&engine_mutex);
    return running;
}

void script_engine_set_log_func(ScriptLogFunc func, gpointer user_data)
{
    log_func = func;
    log_data = user_data;
}

void script_engine_set_done_func(ScriptDoneFunc func, gpointer user_data)
{
    done_func = func;
    done_data = user_data;
}
