/***********************************************************************/
/* script_engine.c                                                     */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Lua 5.4 scripting engine for GTKTerm                           */
/*      Ports Lua sur GtScriptPort / GtTransport (sous-projet 5),      */
/*      motifs, macros, journal CSV.                                   */
/***********************************************************************/

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <netdb.h>
#include <sys/socket.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "script_engine.h"
#include "script_port.h"
#include "rx_queue.h"
#include "link.h"
#include "macros.h"
#include "macros_format.h"
#include "macros_list.h"

#define MAX_LUA_PORTS      16
#define DEFAULT_TIMEOUT_MS 5000
#define MAX_TIMEOUT_MS     (24LL * 3600 * 1000)            /* délais bornés à 24 h : ms * 1000 ne déborde jamais */
#define SEND_RETRY_US      (2 * G_TIME_SPAN_MILLISECOND)   /* pause entre deux essais d'envoi sans progrès */
#define CONNECT_POLL_US    (5 * G_TIME_SPAN_MILLISECOND)   /* relevé de l'état pendant l'attente de connexion */
#define STOP_POLL_US       (20 * G_TIME_SPAN_MILLISECOND)  /* l'arrêt est vu au moins toutes les 20 ms */
#define HOOK_COUNT          1000                            /* crochet d'arrêt toutes les 1000 instructions */
#define LOG_MAX_PENDING     (1024 * 1024)                   /* journal en attente au plus 1 Mio */
#define LOG_FLUSH_DELAY_MS  20                              /* regroupement du journal : un vidage au plus toutes les 20 ms */
#define SHUTDOWN_TIMEOUT_US (3 * G_USEC_PER_SEC)            /* fermeture de GTKTerm : 3 s au plus pour le thread Lua */
#define DRAIN_POLL_US       (5 * G_TIME_SPAN_MILLISECOND)   /* vidange série avant fermeture : relevé toutes les 5 ms */
#define DRAIN_MARGIN_US     (500 * G_TIME_SPAN_MILLISECOND) /* vidange : temps d'émission des octets en attente + 500 ms */
#define DRAIN_MAX_US        (2 * G_USEC_PER_SEC)            /* vidange : 2 s au plus */
#define DRAIN_FIFO_BYTES    64                              /* vidange : FIFO matérielle de l'UART / tampons USB, hors TIOCOUTQ */

/* Règle : le thread Lua ne touche ni GTK, ni un transport, ni la liaison. Il n'accède qu'aux files (GtRxQueue, verrouillées par
 * rx_queue.c lui-même) et à l'état atomique des ports, et fait exécuter tout le reste sur le thread principal par
 * run_on_main_sync. Il ne tient jamais le verrou d'une file pendant un run_on_main_sync : le thread principal pousse dans ces
 * files (interblocage sinon). */

/* État d'un moteur (un par onglet, sous-projet 6b) : tout ce qui portait l'exécution d'un script est ici, rien n'est partagé entre deux
 * moteurs hormis le thread principal et la file des demandes run_on_main_sync. */
struct _GtScriptEngine {
    GtkTermLink   *main_link;    /* port 0 (référence) */
    GtMacros      *macros;       /* send_macro, get_macros, get_lists (non possédé) */
    /* Table des ports Lua : lue et écrite par le thread Lua seulement (aucun lecteur ailleurs, donc ni verrou ni atomique) ; indice 0
     * inutilisé (le port 0 est la liaison). */
    GtScriptPort  *ports[MAX_LUA_PORTS];
    /* Débordement déjà signalé pendant l'exécution en cours, par port (thread Lua ; remis à zéro au lancement). */
    gboolean       overflow_reported[MAX_LUA_PORTS];
    /* Port 0 : réception de la liaison, alimentée par le signal « received » sur le thread principal. */
    GtRxQueue     *main_queue;
    gulong         main_port_handler;   /* gestionnaire « received » sur main_link (thread principal) */
    GThread       *lua_thread;          /* engine_mutex */
    GMutex         engine_mutex;
    gchar         *script_text;         /* texte à exécuter, remis au thread Lua (qui le libère) */
    GtScriptEnd    end;                 /* raison de fin, posée par le thread Lua avant done_source_id */
    guint          done_source_id;      /* source de done_idle_cb programmée, 0 sinon (engine_mutex) */
    gboolean       thread_finished;     /* le thread Lua a fini son travail (calls_mutex) */
    gint           stop_requested;      /* atomique : « Stop » */
    gint           exit_requested;      /* atomique : os.exit appelé (stop_requested est alors posé aussi) */
    lua_Integer    exit_status;         /* code d'os.exit, jamais tronqué (écrit et lu par le thread Lua seulement) */
    /* Pauses interruptibles du thread Lua (sleep, attente de connexion, reprises d'envoi), réveillées par gt_script_engine_stop. */
    GMutex         pause_mutex;
    GCond          pause_cond;
    ScriptLogFunc  log_func;
    gpointer       log_data;
    ScriptDoneFunc done_func;
    gpointer       done_data;
    /* Journal (voir plus bas) */
    GMutex         log_mutex;
    GString       *log_pending;         /* texte en attente (log_mutex) */
    gsize          log_dropped;         /* octets jetés au-delà du plafond depuis le dernier vidage (log_mutex) */
    guint          log_source_id;       /* source de vidage programmée, 0 sinon (log_mutex) */
    gboolean       shut_down;           /* gt_script_engine_shutdown appelée : plus aucun lancement */
    gboolean       abandoned;           /* thread Lua abandonné à la fermeture (encore vivant) : le moteur n'est jamais libéré */
};

/* Objet d'erreur de l'arrêt : une adresse propre au moteur (commune à tous les moteurs), poussée comme valeur légère. */
static const char    stop_sentinel = 0;

/* Moteur d'un état Lua : posé dans l'espace supplémentaire juste après luaL_newstate (recopié dans chaque coroutine). */
static GtScriptEngine *engine_of(lua_State *L)
{
    return *(GtScriptEngine **)lua_getextraspace(L);
}

/* ------------------------------------------------------------------ */
/* Journal                                                            */
/* ------------------------------------------------------------------ */

/* Le texte du thread Lua et des avertissements du moteur s'accumule dans un tampon verrouillé ; une seule source à la fois, à
 * G_PRIORITY_DEFAULT_IDLE (après le rafraîchissement GTK) et 20 ms après le premier ajout, remet tout le tampon en un appel. */
static void log_flush(GtScriptEngine *e);

static gboolean log_flush_cb(gpointer user_data)
{
    GtScriptEngine *e = user_data;

    g_mutex_lock(&e->log_mutex);
    e->log_source_id = 0;
    g_mutex_unlock(&e->log_mutex);
    log_flush(e);
    return G_SOURCE_REMOVE;
}

/* Ajoute text (len octets) à out, chaque octet nul écrit « \0 » : il couperait tout le lot remis à la fonction de journal. */
static void append_escaped(GString *out, const char *text, gsize len)
{
    const char *p = text, *end = text + len;

    while (p < end) {
        const char *nul = memchr(p, '\0', (size_t)(end - p));
        const char *stop = nul != NULL ? nul : end;
        g_string_append_len(out, p, (gssize)(stop - p));
        if (nul == NULL)
            break;
        g_string_append(out, "\\0");
        p = nul + 1;
    }
}

/* N'importe quel thread : ajoute au tampon et programme un vidage s'il n'y en a pas. Plafond de 1 Mio comparé à la taille après
 * échappement des octets nuls (le tampon ne le dépasse jamais) ; au-delà, les octets du texte sont comptés comme jetés. */
static void log_append_len(GtScriptEngine *e, const char *text, gsize len)
{
    gsize escaped = len;

    for (const char *p = text; (p = memchr(p, '\0', (size_t)(text + len - p))) != NULL; p++)
        escaped++;
    g_mutex_lock(&e->log_mutex);
    if (e->log_pending == NULL)
        e->log_pending = g_string_new(NULL);
    if (e->log_pending->len + escaped > LOG_MAX_PENDING)
        e->log_dropped += len;
    else
        append_escaped(e->log_pending, text, len);
    if (e->log_source_id == 0)
        e->log_source_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, LOG_FLUSH_DELAY_MS, log_flush_cb, e, NULL);
    g_mutex_unlock(&e->log_mutex);
}

/* Thread principal : tout le texte en attente en un appel (UTF-8 rendu valide), puis la ligne de pertes éventuelle. */
static void log_flush(GtScriptEngine *e)
{
    GString *text = NULL;
    gsize dropped;

    g_mutex_lock(&e->log_mutex);
    if (e->log_pending != NULL && e->log_pending->len > 0) {
        text = e->log_pending;
        e->log_pending = g_string_new(NULL);
    }
    dropped = e->log_dropped;
    e->log_dropped = 0;
    g_mutex_unlock(&e->log_mutex);

    if (text != NULL) {
        if (e->log_func) {
            gchar *valid = g_utf8_make_valid(text->str, (gssize)text->len);
            e->log_func(valid, e->log_data);
            g_free(valid);
        }
        g_string_free(text, TRUE);
    }
    if (dropped > 0 && e->log_func) {
        gchar *msg = g_strdup_printf(_("[%zu bytes of log output dropped]"), dropped);
        gchar *line = g_strconcat(msg, "\n", NULL);
        e->log_func(line, e->log_data);
        g_free(line);
        g_free(msg);
    }
}

static void engine_log_line(GtScriptEngine *e, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
/* Une ligne du moteur (avertissement, erreur) : texte formaté suivi d'une fin de ligne, n'importe quel thread. */
static void engine_log_line(GtScriptEngine *e, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    char *line = g_strconcat(text, "\n", NULL);
    log_append_len(e, line, strlen(line));
    g_free(line);
    g_free(text);
}

/* ------------------------------------------------------------------ */
/* Arrêt et pauses interruptibles                                     */
/* ------------------------------------------------------------------ */

/* Lève l'erreur d'arrêt (valeur légère propre au moteur). Ne rend jamais la main : aucune allocation C ne doit être en cours. */
static int raise_stop(lua_State *L)
{
    lua_pushlightuserdata(L, (void *)&stop_sentinel);
    return lua_error(L);
}

/* Attend au plus us microsecondes ; FALSE si l'arrêt est demandé (avant ou pendant l'attente). gt_script_engine_stop réveille
 * l'attente ; la relecture toutes les 20 ms n'est qu'une sécurité. */
static gboolean pause_interruptible(GtScriptEngine *e, gint64 us)
{
    gint64 deadline = g_get_monotonic_time() + MAX(us, 0);
    gboolean stopped;

    g_mutex_lock(&e->pause_mutex);
    while (!(stopped = g_atomic_int_get(&e->stop_requested)) && g_get_monotonic_time() < deadline)
        g_cond_wait_until(&e->pause_cond, &e->pause_mutex, MIN(deadline, g_get_monotonic_time() + STOP_POLL_US));
    g_mutex_unlock(&e->pause_mutex);
    return !stopped;
}

/* ------------------------------------------------------------------ */
/* Exécution synchrone sur le thread principal                        */
/* ------------------------------------------------------------------ */

/* Le thread Lua ne touche jamais GTK, un transport ni l'état du thread principal (macros, listes, liaison) : il y fait exécuter
 * func et attend sa fin (C10, C11). La demande est inscrite dans pending_calls et une source est ajoutée au contexte par défaut ;
 * pendant gt_script_engine_shutdown, le thread principal ne fait plus tourner ce contexte (les rappels de l'interface toucheraient
 * des widgets détruits) : il exécute lui-même les demandes inscrites et retire leurs sources. */
typedef struct {
    GtScriptEngine *engine;  /* moteur demandeur (toute demande est exécutée, quel que soit son moteur) */
    GSourceFunc func;
    gpointer    data;
    guint       source_id;   /* source du contexte par défaut (calls_mutex) */
    gboolean    done;
    GMutex      mutex;
    GCond       cond;
} MainCall;

static GThread *main_thread = NULL;      /* thread principal, noté par script_engine_setup (commun à tous les moteurs) */
static GMutex   calls_mutex;
static GCond    calls_cond;              /* nouvelle demande ou fin d'un thread Lua (calls_mutex) */
static GQueue   pending_calls = G_QUEUE_INIT;   /* MainCall * pas encore exécutées (calls_mutex) */

static void main_call_run(MainCall *mc)
{
    mc->func(mc->data);
    g_mutex_lock(&mc->mutex);
    mc->done = TRUE;
    g_cond_signal(&mc->cond);
    g_mutex_unlock(&mc->mutex);
}

static gboolean main_call_trampoline(gpointer user_data)
{
    MainCall *mc = user_data;

    /* Toujours encore inscrite : gt_script_engine_shutdown retire la source avant d'exécuter une demande, sur ce même thread */
    g_mutex_lock(&calls_mutex);
    g_queue_remove(&pending_calls, mc);
    g_mutex_unlock(&calls_mutex);
    main_call_run(mc);
    return G_SOURCE_REMOVE;
}

static void run_on_main_sync(GtScriptEngine *e, GSourceFunc func, gpointer data)
{
    /* Appelée par le thread principal : exécution directe (spec §2.3), sinon il attendrait une demande qu'il est seul à pouvoir
     * exécuter. */
    if (g_thread_self() == main_thread) {
        func(data);
        return;
    }
    MainCall mc;
    mc.engine = e;
    mc.func = func;
    mc.data = data;
    mc.done = FALSE;
    g_mutex_init(&mc.mutex);
    g_cond_init(&mc.cond);
    g_mutex_lock(&calls_mutex);
    mc.source_id = g_idle_add_full(G_PRIORITY_DEFAULT, main_call_trampoline, &mc, NULL);
    g_queue_push_tail(&pending_calls, &mc);
    g_cond_broadcast(&calls_cond);
    g_mutex_unlock(&calls_mutex);
    g_mutex_lock(&mc.mutex);
    while (!mc.done)
        g_cond_wait(&mc.cond, &mc.mutex);
    g_mutex_unlock(&mc.mutex);
    g_mutex_clear(&mc.mutex);
    g_cond_clear(&mc.cond);
}

/* ------------------------------------------------------------------ */
/* Port 0 : réception et état de la liaison                           */
/* ------------------------------------------------------------------ */

/* Octets reçus sur le port principal (signal « received » de la liaison, thread principal) */
static void lua_main_port_received(GtkTermLink *link, GBytes *bytes, gint64 wall_time_us, gpointer user_data)
{
    gsize size;
    const guint8 *data = g_bytes_get_data(bytes, &size);
    GtScriptEngine *e = user_data;

    gt_rx_queue_push(e->main_queue, data, size);
}

/* Connexion et déconnexion du gestionnaire sur le thread principal (run_on_main_sync) : l'objet GLib n'est jamais
   manipulé depuis le thread Lua (remarque reportée du sous-projet 1). */
static gboolean main_port_connect_cb(gpointer data)
{
    GtScriptEngine *e = data;

    if (e->main_port_handler == 0)
        e->main_port_handler = g_signal_connect(e->main_link, "received", G_CALLBACK(lua_main_port_received), e);
    return G_SOURCE_REMOVE;
}

static gboolean main_port_disconnect_cb(gpointer data)
{
    GtScriptEngine *e = data;

    if (e->main_port_handler != 0)
        g_signal_handler_disconnect(e->main_link, e->main_port_handler);
    e->main_port_handler = 0;
    return G_SOURCE_REMOVE;
}

typedef struct {
    GtScriptEngine *e;
    GtLinkState     state;
} MainLinkState;

static gboolean main_link_state_cb(gpointer user_data)
{
    MainLinkState *d = user_data;

    d->state = gt_link_get_state(d->e->main_link);
    return G_SOURCE_REMOVE;
}

static gboolean link_state_lost(GtLinkState s)
{
    return s == GT_LINK_LOST || s == GT_LINK_RECONNECTING;
}

static const char *link_state_name(GtLinkState s)
{
    switch (s) {
    case GT_LINK_OPEN:         return "open";
    case GT_LINK_CONNECTING:   return "connecting";
    case GT_LINK_LISTENING:    return "listening";
    case GT_LINK_LOST:
    case GT_LINK_RECONNECTING: return "lost";
    default:                   return "closed";
    }
}

/* ------------------------------------------------------------------ */
/* Files de réception et débordements                                 */
/* ------------------------------------------------------------------ */

/* File de réception du port h (0 : liaison) ; NULL si h est hors plage ou si le port Lua est fermé. Thread Lua. */
static GtRxQueue *queue_for(GtScriptEngine *e, int handle)
{
    if (handle == 0)
        return e->main_queue;
    if (handle > 0 && handle < MAX_LUA_PORTS && e->ports[handle] != NULL)
        return gt_script_port_get_queue(e->ports[handle]);
    return NULL;
}

/* Signale une fois par port et par exécution que sa file a débordé (thread Lua ; le cœur ne journalise pas). */
static void report_overflow(GtScriptEngine *e, int handle, GtRxQueue *q)
{
    if (q == NULL || gt_rx_queue_take_dropped(q) == 0 || e->overflow_reported[handle])
        return;
    e->overflow_reported[handle] = TRUE;
    engine_log_line(e, _("Port %d: receive buffer full, oldest data dropped"), handle);
}

static void report_all_overflows(GtScriptEngine *e)
{
    for (int h = 0; h < MAX_LUA_PORTS; h++)
        report_overflow(e, h, queue_for(e, h));
}

/* ------------------------------------------------------------------ */
/* Arguments                                                          */
/* ------------------------------------------------------------------ */

/* Numéro de port à la position idx ; -1 hors 0..15 (un grand entier n'est jamais tronqué en int dans la plage). */
static int handle_arg(lua_State *L, int idx)
{
    lua_Integer h = luaL_checkinteger(L, idx);
    return (h >= 0 && h < MAX_LUA_PORTS) ? (int)h : -1;
}

/* Délai facultatif en ms à la position idx (défaut 5000), borné à [0, 24 h], rendu en µs. */
static gint64 opt_timeout_us(lua_State *L, int idx)
{
    lua_Integer ms = luaL_optinteger(L, idx, DEFAULT_TIMEOUT_MS);
    return (gint64)CLAMP(ms, 0, MAX_TIMEOUT_MS) * 1000;
}

/* Parité facultative : 0/1/2 ou "none"/"odd"/"even" ; FALSE hors domaine. */
static gboolean opt_parity(lua_State *L, int idx, GtParity *out)
{
    if (lua_isnoneornil(L, idx)) {
        *out = GT_PARITY_NONE;
        return TRUE;
    }
    if (lua_type(L, idx) == LUA_TSTRING)
        return gt_script_parity_from_name(lua_tostring(L, idx), out);
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 2)
        return FALSE;
    *out = (GtParity)v;
    return TRUE;
}

/* Contrôle de flux facultatif : 0/1/2 ou "none"/"xon"/"rts" ; FALSE hors domaine. */
static gboolean opt_flow(lua_State *L, int idx, GtFlow *out)
{
    if (lua_isnoneornil(L, idx)) {
        *out = GT_FLOW_NONE;
        return TRUE;
    }
    if (lua_type(L, idx) == LUA_TSTRING)
        return gt_script_flow_from_name(lua_tostring(L, idx), out);
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 2)
        return FALSE;
    *out = (GtFlow)v;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Ports Lua : ouverture, fermeture, état                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const TransportParams *params;
    GtScriptPort          *port;
    gchar                 *error;   /* message du transport sans fin de ligne, à libérer */
} MainOpenData;

static gboolean main_open_cb(gpointer user_data)
{
    MainOpenData *d = user_data;
    GError *err = NULL;

    d->port = gt_script_port_open(d->params, &err);
    if (d->port == NULL) {
        d->error = g_strchomp(g_strdup(err != NULL ? err->message : "open failed"));
        g_clear_error(&err);
    }
    return G_SOURCE_REMOVE;
}

static gboolean main_close_cb(gpointer user_data)
{
    gt_script_port_close(user_data);
    return G_SOURCE_REMOVE;
}

typedef struct {
    GtScriptPort *port;
    guint         pending;   /* octets en attente d'émission */
    guint         baud;      /* débit obtenu (série), 0 sinon */
} MainOutput;

static gboolean main_output_cb(gpointer user_data)
{
    MainOutput *d = user_data;

    d->pending = gt_script_port_get_output_pending(d->port);
    d->baud = gt_script_port_get_baudrate(d->port);
    return G_SOURCE_REMOVE;
}

/* Série (thread Lua) : la fermeture du transport jette la sortie en attente (tcflush TCOFLUSH, jamais de tcdrain bloquant sur le
 * thread principal) ; on attend donc ici qu'elle soit émise, relevé toutes les 5 ms par run_on_main_sync, au plus le temps
 * d'émission (10 bits par octet) + 500 ms, plafonné à 2 s. Aucune vidange si l'arrêt est demandé (Stop, os.exit, fermeture de
 * GTKTerm) ni sur un port perdu ; TCP : rien en attente (le noyau émet après la fermeture). */
static void drain_output(GtScriptEngine *e, GtScriptPort *port)
{
    MainOutput d = { port, 0, 0 };
    gint64 start = g_get_monotonic_time();
    gint64 limit = start + DRAIN_MAX_US;    /* une seule échéance pour toute la vidange, marge de FIFO comprise */
    gint64 deadline, now;

    if (g_atomic_int_get(&e->stop_requested) || gt_script_port_get_state(port) == GT_SCRIPT_PORT_LOST)
        return;
    run_on_main_sync(e, main_output_cb, &d);
    /* pending == 0 n'est pas une sortie anticipée : une courte trame (« RESET\r\n ») passe tout de suite dans la FIFO de l'UART */
    if (d.baud == 0)
        return;
    deadline = MIN(start + (gint64)d.pending * 10 * G_USEC_PER_SEC / d.baud + DRAIN_MARGIN_US, limit);
    while (d.pending > 0) {
        now = g_get_monotonic_time();
        if (now >= deadline || !pause_interruptible(e, MIN(DRAIN_POLL_US, deadline - now)))
            return;
        run_on_main_sync(e, main_output_cb, &d);
    }
    /* TIOCOUTQ ne compte que le tampon du pilote : la FIFO de l'UART (ou les URB d'un adaptateur USB) peut encore contenir
       quelques octets, que le TCOFLUSH de la fermeture couperait ; temps d'émission de DRAIN_FIFO_BYTES + 10 ms, dans l'échéance */
    now = g_get_monotonic_time();
    if (now < limit)
        pause_interruptible(e, MIN((gint64)DRAIN_FIFO_BYTES * 10 * G_USEC_PER_SEC / d.baud + 10 * G_TIME_SPAN_MILLISECOND, limit - now));
}

/* Ferme le port h (thread Lua). gt_script_port_close libère file et port tout de suite : l'entrée est retirée de la table avant,
 * et le thread Lua ne garde aucun pointeur vers eux ; transport fermé sur le thread principal. */
static void lua_port_close(GtScriptEngine *e, int handle)
{
    GtScriptPort *port;

    if (handle < 1 || handle >= MAX_LUA_PORTS || e->ports[handle] == NULL)
        return;
    port = e->ports[handle];
    drain_output(e, port);
    report_overflow(e, handle, gt_script_port_get_queue(port));
    e->ports[handle] = NULL;
    run_on_main_sync(e, main_close_cb, port);
}

static int open_result(lua_State *L, const char *error)
{
    if (error == NULL) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushstring(L, error);
    return 2;
}

typedef enum { CONNECT_OK, CONNECT_LOST, CONNECT_TIMEOUT, CONNECT_STOPPED } ConnectOutcome;

/* Client TCP : attend OPEN jusqu'à deadline. Perte (refus) : port refermé, *reason = raison du transport (à libérer). Délai : port
 * refermé. Arrêt : le port reste dans la table et sera refermé par le nettoyage de fin de script. */
static ConnectOutcome wait_connected(GtScriptEngine *e, int handle, gint64 deadline, gchar **reason)
{
    GtScriptPort *port = e->ports[handle];

    for (;;) {
        GtScriptPortState st = gt_script_port_get_state(port);
        gint64 now;

        if (st == GT_SCRIPT_PORT_OPEN)
            return CONNECT_OK;
        if (st == GT_SCRIPT_PORT_LOST) {
            gchar *r = gt_script_port_dup_reason(port);
            *reason = r != NULL ? r : g_strdup("link lost");
            lua_port_close(e, handle);
            return CONNECT_LOST;
        }
        now = g_get_monotonic_time();
        if (now >= deadline) {
            lua_port_close(e, handle);
            return CONNECT_TIMEOUT;
        }
        if (!pause_interruptible(e, MIN(CONNECT_POLL_US, deadline - now)))
            return CONNECT_STOPPED;
    }
}

/* Ouverture d'un port sur le thread principal ; NULL et *error (message sans fin de ligne, à libérer) en cas d'échec. */
static GtScriptPort *open_on_main(GtScriptEngine *e, const TransportParams *params, gchar **error)
{
    MainOpenData d = { params, NULL, NULL };

    run_on_main_sync(e, main_open_cb, &d);
    *error = d.error;
    return d.port;
}

/* Raison d'échec du transport (« Cannot connect to 127.0.0.1:4000: … ») : quand le script a donné un nom, il est ajouté devant
 * l'adresse numérique essayée (« Cannot connect to localhost (127.0.0.1:4000): … »). Forme du transport conservée, quelle que soit la
 * traduction : seule l'occurrence de « adresse:port » est remplacée. Rend une chaîne nouvelle ; error est libérée. */
static gchar *name_failure(gchar *error, const char *host, const char *numeric, const char *port_str)
{
    gchar *needle, *hit, *out;

    if (error == NULL || numeric == NULL || g_strcmp0(host, numeric) == 0)
        return error;
    needle = g_strdup_printf("%s:%s", numeric, port_str);
    hit = strstr(error, needle);
    if (hit == NULL) {
        g_free(needle);
        return error;
    }
    *hit = '\0';
    out = g_strdup_printf("%s%s (%s)%s", error, host, needle, hit + strlen(needle));
    g_free(needle);
    g_free(error);
    return out;
}

/* Client TCP (thread Lua) : le nom est résolu ici (getaddrinfo est sûr entre threads, la résolution n'est pas interruptible), jamais
 * sur le thread principal (interface figée, Stop inopérant). Chaque adresse est ensuite ouverte par son écriture numérique
 * (ouverture instantanée) et essayée l'une après l'autre : refus → suivante (localhost : ::1 puis 127.0.0.1), dans la limite de
 * timeout_us pour l'ensemble, résolution comprise. Raison d'échec : celle du dernier essai. */
static int open_tcp_client(lua_State *L, int handle, const char *host, const char *port_str, gint64 timeout_us)
{
    GtScriptEngine *e = engine_of(L);
    gint64 deadline = g_get_monotonic_time() + timeout_us;
    struct addrinfo hints, *res = NULL;
    gchar *last_error = NULL;
    gchar *last_numeric = NULL;
    int ret;

    /* Délai nul ou négatif : rien n'est tenté, pas même la résolution */
    if (timeout_us <= 0)
        return open_result(L, "timeout");
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    ret = getaddrinfo(host, port_str, &hints, &res);
    if (g_atomic_int_get(&e->stop_requested)) {
        if (ret == 0)
            freeaddrinfo(res);
        return raise_stop(L);
    }
    if (ret != 0) {
        /* Même message que le transport (même traduction), sans fin de ligne */
        gchar *msg = g_strchomp(g_strdup_printf(_("Cannot resolve %s: %s\n"), host, gai_strerror(ret)));
        lua_pushboolean(L, 0);
        lua_pushstring(L, msg);
        g_free(msg);
        return 2;
    }

    ConnectOutcome outcome = CONNECT_LOST;
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        char numeric[NI_MAXHOST];
        TransportParams params;
        GtScriptPort *port;
        gchar *error = NULL;

        if (getnameinfo(rp->ai_addr, rp->ai_addrlen, numeric, sizeof(numeric), NULL, 0, NI_NUMERICHOST) != 0)
            continue;
        g_free(last_numeric);
        last_numeric = g_strdup(numeric);
        memset(&params, 0, sizeof(params));
        params.type = TRANSPORT_TCP_CLIENT;
        params.host = numeric;
        params.tcp_port = (gchar *)port_str;
        port = open_on_main(e, &params, &error);
        if (port == NULL) {
            g_free(last_error);
            last_error = error;
            continue;
        }
        /* Dans la table dès maintenant : un arrêt pendant l'attente de connexion laisse le port au nettoyage de fin de script */
        e->ports[handle] = port;
        g_free(last_error);
        last_error = NULL;
        outcome = wait_connected(e, handle, deadline, &last_error);
        if (outcome != CONNECT_LOST)
            break;
        /* Refus asynchrone : adresse suivante, sauf si le délai est déjà écoulé (dernière adresse : raison du refus) */
        if (rp->ai_next != NULL && g_get_monotonic_time() >= deadline) {
            outcome = CONNECT_TIMEOUT;
            break;
        }
    }
    freeaddrinfo(res);

    switch (outcome) {
    case CONNECT_OK:
        g_free(last_numeric);
        return open_result(L, NULL);
    case CONNECT_STOPPED:
        g_free(last_numeric);
        g_free(last_error);
        return raise_stop(L);
    case CONNECT_TIMEOUT:
        g_free(last_numeric);
        g_free(last_error);
        return open_result(L, "timeout");
    default:
        last_error = name_failure(last_error, host, last_numeric, port_str);
        g_free(last_numeric);
        lua_pushboolean(L, 0);
        lua_pushstring(L, last_error != NULL ? last_error : "link lost");
        g_free(last_error);
        return 2;
    }
}

static int l_open(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    int handle = handle_arg(L, 1);
    TransportParams params;
    char port_str[16];

    if (handle < 1)
        return open_result(L, "invalid handle");
    if (e->ports[handle] != NULL)
        return open_result(L, "already open");

    const char *type = luaL_checkstring(L, 2);
    memset(&params, 0, sizeof(params));
    /* Chaînes empruntées à la pile Lua (valides pendant l'appel) : gt_transport_open en fait sa copie */
    if (strcmp(type, "serial") == 0) {
        const char *device = luaL_checkstring(L, 3);
        lua_Integer baud = luaL_optinteger(L, 4, 9600);
        lua_Integer bits = luaL_optinteger(L, 5, 8);
        lua_Integer stop = luaL_optinteger(L, 6, 1);
        GtParity parity;
        GtFlow flow;

        if (baud < 1 || baud > G_MAXUINT)
            return open_result(L, "invalid baud");
        if (bits < 5 || bits > 8)
            return open_result(L, "invalid bits");
        if (stop < 1 || stop > 2)
            return open_result(L, "invalid stop");
        if (!opt_parity(L, 7, &parity))
            return open_result(L, "invalid parity");
        if (!opt_flow(L, 8, &flow))
            return open_result(L, "invalid flow");
        params.type = TRANSPORT_SERIAL;
        params.port = (gchar *)device;
        params.baudrate = (guint)baud;
        params.bits = (gint)bits;
        params.stopbits = (gint)stop;
        params.parity = parity;
        params.flow = flow;
    } else if (strcmp(type, "tcp_client") == 0) {
        const char *host = luaL_checkstring(L, 3);
        lua_Integer port = luaL_checkinteger(L, 4);
        gint64 connect_timeout_us = opt_timeout_us(L, 5);

        if (port < 1 || port > 65535)
            return open_result(L, "invalid port");
        g_snprintf(port_str, sizeof(port_str), "%d", (int)port);
        return open_tcp_client(L, handle, host, port_str, connect_timeout_us);
    } else if (strcmp(type, "tcp_server") == 0) {
        lua_Integer port = luaL_checkinteger(L, 3);
        const char *host = luaL_optstring(L, 4, NULL);

        if (port < 0 || port > 65535)
            return open_result(L, "invalid port");
        g_snprintf(port_str, sizeof(port_str), "%d", (int)port);
        params.type = TRANSPORT_TCP_SERVER;
        params.host = (gchar *)host;
        params.tcp_port = port_str;
    } else {
        return open_result(L, "unknown type (serial/tcp_client/tcp_server)");
    }

    gchar *error = NULL;
    GtScriptPort *port = open_on_main(e, &params, &error);
    if (port == NULL) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, error);
        g_free(error);
        return 2;
    }
    e->ports[handle] = port;
    return open_result(L, NULL);
}

static int l_close(lua_State *L)
{
    lua_port_close(engine_of(L), handle_arg(L, 1));
    return 0;
}

static const char *port_state_name(GtScriptPortState s)
{
    switch (s) {
    case GT_SCRIPT_PORT_CONNECTING: return "connecting";
    case GT_SCRIPT_PORT_OPEN:       return "open";
    case GT_SCRIPT_PORT_LISTENING:  return "listening";
    case GT_SCRIPT_PORT_CONNECTED:  return "connected";
    default:                        return "lost";
    }
}

/* Perdu : état LOST, ou file déjà fermée alors que l'état ne l'est pas encore (la perte ferme la file juste avant de poser LOST ;
 * la fermeture par le script retire le port de la table, une file fermée d'un port encore présent est donc une perte). */
static gboolean lua_port_lost(GtScriptPort *port)
{
    return gt_script_port_get_state(port) == GT_SCRIPT_PORT_LOST || gt_rx_queue_is_closed(gt_script_port_get_queue(port));
}

static int l_status(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    int handle = handle_arg(L, 1);

    if (handle == 0) {
        MainLinkState st = { e, GT_LINK_CLOSED };
        run_on_main_sync(e, main_link_state_cb, &st);
        lua_pushstring(L, link_state_name(st.state));
    } else if (handle > 0 && e->ports[handle] != NULL) {
        GtScriptPort *port = e->ports[handle];
        lua_pushstring(L, lua_port_lost(port) ? "lost" : port_state_name(gt_script_port_get_state(port)));
    } else {
        lua_pushstring(L, "closed");
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Envoi complet : send, send_hex                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    GtScriptEngine *e;
    GtScriptPort *port;          /* NULL : port 0 (liaison) */
    const guint8 *data;
    gsize         len;
    gssize        written;
    gint          err;           /* errno relevé sur le thread principal juste après l'écriture */
    GtLinkState   link_state;    /* port 0 : état de la liaison après l'essai */
} MainTrySend;

/* Une seule tentative d'écriture, jamais bloquante : le thread principal n'attend jamais un lecteur. */
static gboolean main_try_send_cb(gpointer user_data)
{
    MainTrySend *d = user_data;

    /* Arrêt demandé (« Stop » ou fermeture, fenêtre peut-être détruite) : plus rien n'est écrit, comme main_send_macro_cb */
    if (g_atomic_int_get(&d->e->stop_requested)) {
        d->written = -1;
        d->err = EAGAIN;
        return G_SOURCE_REMOVE;
    }
    if (d->port != NULL) {
        d->written = gt_script_port_try_send(d->port, d->data, d->len, &d->err);
        return G_SOURCE_REMOVE;
    }
    GtkTermLink *link = d->e->main_link;
    errno = 0;
    d->written = gt_link_send(link, d->data, d->len, GT_SEND_LUA);
    /* errno est propre au thread : relevé ici, avant tout autre appel (le thread Lua ne le verrait jamais) */
    d->err = d->written < 0 ? errno : 0;
    d->link_state = gt_link_get_state(link);
    return G_SOURCE_REMOVE;
}

typedef enum { SEND_DONE, SEND_FAILED, SEND_STOPPED } SendOutcome;

/* Écrit tout ou échoue (thread Lua) : écritures partielles et EAGAIN repris ici, pause de 2 ms entre deux essais sans progrès,
 * jusqu'au délai. *reason : chaîne statique (ou de g_strerror) en cas d'échec. Le port h est ouvert (vérifié par l'appelant). */
static SendOutcome send_all(GtScriptEngine *e, int handle, const guint8 *data, gsize len, gint64 timeout_us, const char **reason)
{
    gint64 deadline = g_get_monotonic_time() + timeout_us;
    gsize done = 0;

    while (done < len) {
        MainTrySend d;
        gboolean lost;

        if (g_atomic_int_get(&e->stop_requested))
            return SEND_STOPPED;
        memset(&d, 0, sizeof(d));
        d.e = e;
        d.port = handle > 0 ? e->ports[handle] : NULL;
        d.data = data + done;
        d.len = len - done;
        run_on_main_sync(e, main_try_send_cb, &d);
        if (g_atomic_int_get(&e->stop_requested))
            return SEND_STOPPED;
        if (d.written > 0) {
            done += (gsize)d.written;
            continue;
        }
        lost = handle > 0 ? lua_port_lost(e->ports[handle]) : link_state_lost(d.link_state);
        if (d.written == 0) {
            /* Rien d'écrivable : connexion en cours, serveur sans client, liaison fermée ou perdue */
            *reason = lost ? "link lost" : "not connected";
            return SEND_FAILED;
        }
        if (d.err == EAGAIN || d.err == EWOULDBLOCK || d.err == EINTR) {
            if (g_get_monotonic_time() >= deadline) {
                *reason = "timeout";
                return SEND_FAILED;
            }
            if (!pause_interruptible(e, SEND_RETRY_US))
                return SEND_STOPPED;
            continue;
        }
        /* Pair parti pendant l'envoi (serveur TCP : le client a fermé ou réinitialisé) : plus de connexion, pas une erreur système (M5) */
        if (!lost && (d.err == EPIPE || d.err == ECONNRESET || d.err == ENOTCONN)) {
            *reason = "not connected";
            return SEND_FAILED;
        }
        /* EIO : perte, même si l'état n'est pas encore LOST (raison pas encore posée) */
        *reason = (lost || d.err == EIO) ? "link lost" : g_strerror(d.err);
        return SEND_FAILED;
    }
    return SEND_DONE;
}

static int send_checked(lua_State *L, int handle, const guint8 *data, gsize len, gint64 timeout_us)
{
    GtScriptEngine *e = engine_of(L);
    const char *reason = NULL;
    SendOutcome o;

    if (handle < 0)
        return open_result(L, "invalid handle");
    if (handle > 0 && e->ports[handle] == NULL)
        return open_result(L, "not open");
    o = send_all(e, handle, data, len, timeout_us, &reason);
    if (o == SEND_STOPPED)
        return raise_stop(L);
    return open_result(L, o == SEND_DONE ? NULL : reason);
}

static int l_send(lua_State *L)
{
    int handle = handle_arg(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    gint64 timeout_us = opt_timeout_us(L, 3);

    /* data appartient à la pile Lua : valable pendant tout l'appel, le thread principal le lit pendant run_on_main_sync */
    return send_checked(L, handle, (const guint8 *)data, len, timeout_us);
}

static int l_send_hex(lua_State *L)
{
    int handle = handle_arg(L, 1);
    size_t hex_len;
    const char *hex = luaL_checklstring(L, 2, &hex_len);
    gint64 timeout_us = opt_timeout_us(L, 3);
    GError *err = NULL;
    GBytes *bytes;
    gsize len;
    const char *data;

    /* gt_script_hex_decode lit une chaîne C : un \0 la tronquerait sans erreur. Le préfixe est décodé d'abord : un caractère invalide
     * qui le précède garde sa position ; sinon (préfixe valide, ou coupé en plein octet par le \0), le \0 est le premier caractère
     * invalide. Position 0-based en octets, comme le décodeur. */
    bytes = gt_script_hex_decode(hex, &err);
    if (bytes == NULL && strncmp(err->message, "invalid character", 17) == 0) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "invalid hex: %s", err->message);
        g_error_free(err);
        return 2;
    }
    if (strlen(hex) != hex_len) {
        if (bytes != NULL)
            g_bytes_unref(bytes);
        else
            g_error_free(err);
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "invalid hex: invalid character at position %d", (int)strlen(hex));
        return 2;
    }
    if (bytes == NULL) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "invalid hex: %s", err->message);
        g_error_free(err);
        return 2;
    }
    /* Copie dans une chaîne Lua : rien à libérer si l'arrêt lève une erreur pendant l'envoi */
    data = g_bytes_get_data(bytes, &len);
    lua_pushlstring(L, data, len);
    g_bytes_unref(bytes);
    data = lua_tolstring(L, -1, &len);
    return send_checked(L, handle, (const guint8 *)data, len, timeout_us);
}

/* ------------------------------------------------------------------ */
/* Réception : wait_for, read, clear                                  */
/* ------------------------------------------------------------------ */

static int nil_reason(lua_State *L, const char *reason)
{
    lua_pushnil(L);
    lua_pushstring(L, reason);
    return 2;
}

/* wait_for(handle, pattern [, timeout_ms]) -> données jusqu'au motif inclus | nil, "timeout" | nil, "closed" */
static int l_wait_for(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    int handle = handle_arg(L, 1);
    size_t pat_len;
    const char *pat = luaL_checklstring(L, 2, &pat_len);
    gint64 timeout_us = opt_timeout_us(L, 3);
    GtRxQueue *q;
    GBytes *found = NULL;
    GtRxWaitResult res;

    if (handle < 0)
        return nil_reason(L, "invalid handle");
    q = queue_for(e, handle);
    if (q == NULL)
        return nil_reason(L, "closed");
    /* L'attente prend et rend le verrou de la file elle-même ; l'arrêt est relu au moins toutes les 20 ms */
    res = gt_rx_queue_wait_for(q, (const guint8 *)pat, pat_len, g_get_monotonic_time() + timeout_us, &e->stop_requested, &found);
    report_overflow(e, handle, q);
    switch (res) {
    case GT_RX_WAIT_FOUND: {
        gsize n;
        const char *p = g_bytes_get_data(found, &n);
        lua_pushlstring(L, p, n);
        g_bytes_unref(found);
        return 1;
    }
    case GT_RX_WAIT_CANCELLED:
        return raise_stop(L);
    case GT_RX_WAIT_CLOSED:
        return nil_reason(L, "closed");
    default:
        return nil_reason(L, "timeout");
    }
}

static int l_read(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    int handle = handle_arg(L, 1);
    GtRxQueue *q = queue_for(e, handle);
    GBytes *b;
    gsize n;
    const char *p;

    if (q == NULL) {
        lua_pushstring(L, "");
        return 1;
    }
    report_overflow(e, handle, q);
    b = gt_rx_queue_take_all(q);
    p = g_bytes_get_data(b, &n);
    lua_pushlstring(L, p, n);
    g_bytes_unref(b);
    return 1;
}

static int l_clear(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    int handle = handle_arg(L, 1);
    GtRxQueue *q = queue_for(e, handle);

    if (q != NULL) {
        report_overflow(e, handle, q);
        gt_rx_queue_clear(q);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* sleep, time, log, save_csv                                         */
/* ------------------------------------------------------------------ */

static int l_sleep(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    lua_Integer ms = luaL_checkinteger(L, 1);
    gboolean completed = pause_interruptible(e, (gint64)CLAMP(ms, 0, MAX_TIMEOUT_MS) * 1000);

    report_all_overflows(e);
    if (!completed)
        return raise_stop(L);
    return 0;
}

/* time() -> secondes (nombre réel, horloge monotone) */
static int l_time(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)g_get_monotonic_time() / 1e6);
    return 1;
}

static int l_log(lua_State *L)
{
    size_t len;
    luaL_tolstring(L, 1, NULL);
    lua_pushliteral(L, "\n");
    lua_concat(L, 2);
    const char *line = lua_tolstring(L, -1, &len);
    log_append_len(engine_of(L), line, len);
    lua_pop(L, 1);
    return 0;
}

static int l_save_csv(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    int n = lua_gettop(L);

    /* Conversions avant fopen : un __tostring qui lève une erreur ne laisse aucun FILE* ouvert (M6). Les chaînes restent sur la pile,
       qu'il faut donc agrandir (une fonction C n'a que LUA_MINSTACK places garanties) */
    luaL_checkstack(L, n + LUA_MINSTACK, "too many values");
    for (int i = 2; i <= n; i++)
        luaL_tolstring(L, i, NULL);

    FILE *f = fopen(filename, "a");
    if (!f) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        return 2;
    }

    for (int i = 1; i <= n - 1; i++) {
        if (i > 1) fprintf(f, ",");
        fprintf(f, "%s", lua_tostring(L, n + i));
    }
    fprintf(f, "\n");
    fclose(f);

    lua_pushboolean(L, 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Crochet d'arrêt, os.exit, print, panique                           */
/* ------------------------------------------------------------------ */

/* Crochet de comptage (toutes les 1000 instructions). Dès que l'arrêt est demandé, il se réarme à chaque instruction puis lève
 * l'erreur d'arrêt : un pcall du script l'attrape une fois, l'instruction suivante hors de ce pcall la relève, donc toute
 * boucle finit par sortir (même un pcall qui avale tout, un xpcall dont le gestionnaire est interrompu, une coroutine). */
static void stop_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (g_atomic_int_get(&engine_of(L)->stop_requested)) {
        lua_sethook(L, stop_hook, LUA_MASKCOUNT, 1);
        raise_stop(L);
    }
}

/* os.exit([code]) : termine le script, jamais GTKTerm. Sans argument, true ou 0 : fin normale ; false (1) ou autre entier :
 * fin en erreur avec « Script exited with status N ». */
static int l_os_exit(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    lua_Integer status;

    if (lua_isnoneornil(L, 1))
        status = 0;
    else if (lua_isboolean(L, 1))
        status = lua_toboolean(L, 1) ? 0 : 1;
    else
        status = luaL_checkinteger(L, 1);
    e->exit_status = status;
    g_atomic_int_set(&e->exit_requested, 1);
    g_atomic_int_set(&e->stop_requested, 1);
    lua_sethook(L, stop_hook, LUA_MASKCOUNT, 1);
    return raise_stop(L);
}

/* print(...) : vers la console, valeurs passées par tostring, séparées par une tabulation, fin de ligne. Concaténation au fil
 * de l'eau : au plus deux valeurs de plus sur la pile, quel que soit le nombre d'arguments. */
static int l_print(lua_State *L)
{
    int n = lua_gettop(L);
    size_t len;

    lua_pushliteral(L, "");
    for (int i = 1; i <= n; i++) {
        if (i > 1) {
            lua_pushliteral(L, "\t");
            lua_concat(L, 2);
        }
        luaL_tolstring(L, i, NULL);
        lua_concat(L, 2);
    }
    lua_pushliteral(L, "\n");
    lua_concat(L, 2);
    const char *text = lua_tolstring(L, -1, &len);
    log_append_len(engine_of(L), text, len);
    return 0;
}

/* Ne devrait jamais servir (tout s'exécute sous lua_pcall) ; Lua appelle abort() au retour. */
static int panic_cb(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    g_printerr("GTKTerm: Lua panic: %s\n", msg != NULL ? msg : "(error object is not a string)");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Macros et listes (instantanés pris sur le thread principal)        */
/* ------------------------------------------------------------------ */

/* Instantanés des listes et des macros, pris sur le thread principal puis poussés vers Lua (C10). */
typedef struct {
    gchar     *name;
    GPtrArray *displays;
    GPtrArray *values;
} ListSnapshot;

static void list_snapshot_free(gpointer p)
{
    ListSnapshot *s = p;
    g_free(s->name);
    g_ptr_array_unref(s->displays);
    g_ptr_array_unref(s->values);
    g_free(s);
}

/* Demande d'instantané : moteur (ses macros) et tableau rempli sur le thread principal */
typedef struct {
    GtScriptEngine *e;
    GPtrArray      *out;
} MainSnapshot;

static gboolean main_snapshot_lists_cb(gpointer user_data)
{
    MainSnapshot *d = user_data;
    GPtrArray *out = d->out;

    /* Arrêt demandé (« Stop », fermeture ; moteur peut-être abandonné, ses macros libérées) : macros jamais lues, instantané vide */
    if (g_atomic_int_get(&d->e->stop_requested))
        return G_SOURCE_REMOVE;
    const GtMacroLists *lists = gt_macro_set_get_lists(gt_macros_get_set(d->e->macros));
    gint n_lists = gt_macro_lists_count(lists);
    for (int i = 0; i < n_lists; i++) {
        ListSnapshot *s = g_new0(ListSnapshot, 1);
        const gchar *name = gt_macro_lists_name(lists, i);
        s->name = g_strdup(name ? name : "");
        s->displays = g_ptr_array_new_with_free_func(g_free);
        s->values = g_ptr_array_new_with_free_func(g_free);
        for (int e = 0; e < gt_macro_lists_entry_count(lists, i); e++) {
            const gchar *disp = gt_macro_lists_entry_display(lists, i, e);
            const gchar *val  = gt_macro_lists_entry_value(lists, i, e);
            g_ptr_array_add(s->displays, g_strdup(disp ? disp : ""));
            g_ptr_array_add(s->values, g_strdup(val ? val : ""));
        }
        g_ptr_array_add(out, s);
    }
    return G_SOURCE_REMOVE;
}

typedef struct {
    gchar            *label;
    gint              n_args;
    macro_arg_info_t *infos;
} MacroSnapshot;

static void macro_snapshot_free(gpointer p)
{
    MacroSnapshot *s = p;
    g_free(s->label);
    macro_arg_infos_free(s->infos, s->n_args);
    g_free(s);
}

static gboolean main_snapshot_macros_cb(gpointer user_data)
{
    MainSnapshot *d = user_data;
    GPtrArray *out = d->out;

    /* Arrêt demandé : macros jamais lues, instantané vide (comme main_snapshot_lists_cb) */
    if (g_atomic_int_get(&d->e->stop_requested))
        return G_SOURCE_REMOVE;
    GtMacroSet *set = gt_macros_get_set(d->e->macros);
    for (guint i = 0; i < gt_macro_set_count(set); i++) {
        const GtMacro *m = gt_macro_set_get(set, i);
        MacroSnapshot *s = g_new0(MacroSnapshot, 1);
        s->label = g_strdup(m->label);
        s->infos = macro_get_arg_infos(gt_macro_set_get_lists(set), m->action, &s->n_args);
        g_ptr_array_add(out, s);
    }
    return G_SOURCE_REMOVE;
}

/* get_lists() -> {{name, entries={{display,value},...}}, ...} */
static int l_get_lists(lua_State *L)
{
    GPtrArray *lists = g_ptr_array_new_with_free_func(list_snapshot_free);
    MainSnapshot d = { engine_of(L), lists };
    run_on_main_sync(d.e, main_snapshot_lists_cb, &d);
    if (g_atomic_int_get(&d.e->stop_requested)) {
        g_ptr_array_unref(lists);
        return raise_stop(L);
    }
    lua_newtable(L);
    for (guint i = 0; i < lists->len; i++) {
        ListSnapshot *s = g_ptr_array_index(lists, i);
        lua_newtable(L);
        lua_pushstring(L, s->name);
        lua_setfield(L, -2, "name");
        lua_newtable(L);
        for (guint e = 0; e < s->displays->len; e++) {
            lua_newtable(L);
            lua_pushstring(L, g_ptr_array_index(s->displays, e));
            lua_setfield(L, -2, "display");
            lua_pushstring(L, g_ptr_array_index(s->values, e));
            lua_setfield(L, -2, "value");
            lua_rawseti(L, -2, e + 1);
        }
        lua_setfield(L, -2, "entries");
        lua_rawseti(L, -2, i + 1);
    }
    g_ptr_array_unref(lists);
    return 1;
}

/* get_macros() -> table of {index, label, n_args, args} */
static int l_get_macros(lua_State *L)
{
    GPtrArray *snap = g_ptr_array_new_with_free_func(macro_snapshot_free);
    MainSnapshot d = { engine_of(L), snap };
    run_on_main_sync(d.e, main_snapshot_macros_cb, &d);
    if (g_atomic_int_get(&d.e->stop_requested)) {
        g_ptr_array_unref(snap);
        return raise_stop(L);
    }
    lua_newtable(L);
    for (guint i = 0; i < snap->len; i++) {
        MacroSnapshot *s = g_ptr_array_index(snap, i);
        lua_newtable(L);
        lua_pushinteger(L, i);
        lua_setfield(L, -2, "index");
        lua_pushstring(L, s->label);
        lua_setfield(L, -2, "label");
        lua_pushinteger(L, s->n_args);
        lua_setfield(L, -2, "n_args");
        lua_newtable(L);
        for (int a = 0; a < s->n_args; a++) {
            lua_newtable(L);
            lua_pushstring(L, s->infos[a].type      ? s->infos[a].type      : "s");
            lua_setfield(L, -2, "type");
            lua_pushstring(L, s->infos[a].label     ? s->infos[a].label     : "");
            lua_setfield(L, -2, "label");
            lua_pushstring(L, s->infos[a].list_name ? s->infos[a].list_name : "");
            lua_setfield(L, -2, "list");
            lua_rawseti(L, -2, a + 1);
        }
        lua_setfield(L, -2, "args");
        lua_rawseti(L, -2, i + 1);
    }
    g_ptr_array_unref(snap);
    return 1;
}

typedef struct {
    GtScriptEngine *e;
    int          index;     /* -1 : chercher par libellé */
    const char  *label;
    int          n_args;
    gchar      **args;
    gboolean     found;
    gboolean     ok;
} MainMacroData;

/* Résolution et envoi sur le thread principal, par l'envoi de macro de l'application sans message (macros_send_quiet : pas de
 * « Macro … sent! » pour un script, spec Lua §1) : ordre respecté avec gtkterm.send (M27). Sans argument Lua, les arguments
 * enregistrés, comme le raccourci et le polling. */
static gboolean main_send_macro_cb(gpointer user_data)
{
    MainMacroData *d = user_data;

    d->found = FALSE;
    d->ok = FALSE;
    /* Arrêt demandé (« Stop », fermeture de l'onglet ou de GTKTerm, fenêtre peut-être détruite, moteur peut-être abandonné et ses
     * macros libérées) : macros jamais lues, plus aucun envoi, même déjà demandé ; l_send_macro lève alors l'erreur d'arrêt */
    if (g_atomic_int_get(&d->e->stop_requested))
        return G_SOURCE_REMOVE;
    GtMacroSet *set = gt_macros_get_set(d->e->macros);
    const GtMacro *m = d->index >= 0 ? gt_macro_set_get(set, (guint)d->index) : gt_macro_set_find_label(set, d->label);

    d->found = gt_macro_is_sendable(m);
    if (d->found)
        d->ok = gt_macros_send_quiet(d->e->macros, m->id, (const gchar *const *)d->args, d->n_args > 0 ? d->n_args : -1);
    return G_SOURCE_REMOVE;
}

/* send_macro(index_or_label [, arg1, arg2, ...]) */
static int l_send_macro(lua_State *L)
{
    GtScriptEngine *e = engine_of(L);
    MainMacroData d = { e, -1, NULL, 0, NULL, FALSE, FALSE };

    if (lua_type(L, 1) == LUA_TNUMBER) {
        /* luaL_checkinteger refuse 1.5 ; hors 0..G_MAXINT : jamais tronqué en int, simplement introuvable (M4) */
        lua_Integer index = luaL_checkinteger(L, 1);

        if (index < 0 || index > G_MAXINT) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "macro not found");
            return 2;
        }
        d.index = (int)index;
    } else {
        d.label = luaL_checkstring(L, 1);
    }

    d.n_args = lua_gettop(L) - 1;
    d.args = g_new0(gchar *, d.n_args + 1);
    for (int i = 0; i < d.n_args; i++) {
        const char *s = lua_tostring(L, i + 2);
        d.args[i] = g_strdup(s ? s : "");
    }

    run_on_main_sync(e, main_send_macro_cb, &d);
    g_strfreev(d.args);
    if (g_atomic_int_get(&e->stop_requested))
        return raise_stop(L);

    if (!d.found) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "macro not found");
        return 2;
    }
    lua_pushboolean(L, d.ok);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Enregistrement de l'API                                            */
/* ------------------------------------------------------------------ */

static const luaL_Reg gtkterm_funcs[] = {
    { "open",       l_open       },
    { "close",      l_close      },
    { "status",     l_status     },
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

/* Entrée de chaque fonction gtkterm.* et de print (fonction réelle en valeur liée) : arrêt demandé, erreur d'arrêt immédiate, sans
 * attendre le crochet (jusqu'à 1000 instructions plus tard) ; une boucle d'appels à l'API s'arrête au premier appel suivant. */
static int api_guard(lua_State *L)
{
    if (g_atomic_int_get(&engine_of(L)->stop_requested))
        return raise_stop(L);
    return lua_tocfunction(L, lua_upvalueindex(1))(L);
}

static void push_guarded(lua_State *L, lua_CFunction f)
{
    lua_pushcfunction(L, f);
    lua_pushcclosure(L, api_guard, 1);
}

/* Fonction de la bibliothèque standard enveloppée (valeur liée 1 : la fonction d'origine, C ou non) : arrêt vu à l'entrée, comme
 * api_guard. Pas de LUA_MASKCALL (coût à chaque appel) : une boucle d'appels lents (os.execute('sleep 1')) ne verrait sinon l'arrêt
 * qu'au bout de 1000 instructions, soit des centaines d'appels (I5). L'appel en cours n'est jamais interrompu. */
static int std_guard(lua_State *L)
{
    int n = lua_gettop(L);

    if (g_atomic_int_get(&engine_of(L)->stop_requested))
        return raise_stop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    if (lua_pcall(L, n, LUA_MULTRET, 0) != LUA_OK) {
        /* Appelée depuis cette enveloppe C, la fonction d'origine perd deux choses de son message d'erreur : sa position
         * (luaL_where voit l'enveloppe, pas le script) et son nom (« bad argument #1 to '?' »). Ajoutés ici pour les messages
         * textuels : position de l'appelant Lua, nom vu à l'appel (« open » pour io.open(...)) sinon le nom complet enregistré dans
         * la valeur liée 2 (« io.open » ; absente pour l'itérateur de lines : nom inchangé). L'objet d'erreur d'arrêt et les erreurs
         * non textuelles repartent tels quels. */
        lua_Debug ar;
        const char *name = lua_type(L, lua_upvalueindex(2)) == LUA_TSTRING ? lua_tostring(L, lua_upvalueindex(2)) : NULL;
        const char *msg = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;

        if (name != NULL && lua_getstack(L, 0, &ar) && lua_getinfo(L, "n", &ar) && ar.name != NULL)
            name = ar.name;
        if (msg != NULL) {
            const char *hit = name != NULL ? strstr(msg, "to '?'") : NULL;

            luaL_where(L, 1);
            if (hit != NULL) {
                /* lua_pushfstring n'a pas de « %.*s » : morceaux assemblés par lua_concat */
                lua_pushlstring(L, msg, (size_t)(hit - msg));
                lua_pushfstring(L, "to '%s'%s", name, hit + 6);
                lua_concat(L, 2);
            } else {
                lua_pushvalue(L, -2);
            }
            lua_concat(L, 2);
        }
        return lua_error(L);
    }
    return lua_gettop(L);
}

/* io.lines, f:lines : comme std_guard, et l'itérateur rendu est enveloppé à son tour (chaque ligne lue voit l'arrêt). */
static int std_guard_lines(lua_State *L)
{
    int n = std_guard(L);

    if (n >= 1 && lua_isfunction(L, 1)) {
        lua_pushvalue(L, 1);
        lua_pushcclosure(L, std_guard, 1);
        lua_replace(L, 1);
    }
    return n;
}

/* Remplace t[name] (table en haut de la pile) par sa version enveloppée ; sans effet si ce n'est pas une fonction. */
static void guard_field(lua_State *L, const char *prefix, const char *name, lua_CFunction guard)
{
    if (lua_getfield(L, -1, name) != LUA_TFUNCTION) {
        lua_pop(L, 1);
        return;
    }
    lua_pushfstring(L, "%s%s", prefix, name);
    lua_pushcclosure(L, guard, 2);
    lua_setfield(L, -2, name);
}

/* Fonctions bloquantes de la bibliothèque standard : os.execute, io.read/lines/popen/open/write et les méthodes read, lines, write
 * des fichiers (__index du métatable LUA_FILEHANDLE). */
static void guard_stdlib(lua_State *L)
{
    if (lua_getglobal(L, "os") == LUA_TTABLE)
        guard_field(L, "os.", "execute", std_guard);
    lua_pop(L, 1);
    if (lua_getglobal(L, "io") == LUA_TTABLE) {
        guard_field(L, "io.", "read", std_guard);
        guard_field(L, "io.", "lines", std_guard_lines);
        guard_field(L, "io.", "popen", std_guard);
        guard_field(L, "io.", "open", std_guard);
        guard_field(L, "io.", "write", std_guard);
    }
    lua_pop(L, 1);
    if (luaL_getmetatable(L, LUA_FILEHANDLE) == LUA_TTABLE) {
        if (lua_getfield(L, -1, "__index") == LUA_TTABLE) {
            guard_field(L, "", "read", std_guard);
            guard_field(L, "", "lines", std_guard_lines);
            guard_field(L, "", "write", std_guard);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

static void register_api(lua_State *L)
{
    guard_stdlib(L);

    lua_createtable(L, 0, G_N_ELEMENTS(gtkterm_funcs) - 1);
    for (const luaL_Reg *r = gtkterm_funcs; r->name != NULL; r++) {
        push_guarded(L, r->func);
        lua_setfield(L, -2, r->name);
    }
    lua_setglobal(L, "gtkterm");

    /* Convenience: main port handle constant */
    lua_pushinteger(L, 0);
    lua_setglobal(L, "MAIN_PORT");

    /* print vers la console, os.exit qui termine le script et non GTKTerm */
    push_guarded(L, l_print);
    lua_setglobal(L, "print");
    lua_getglobal(L, "os");
    lua_pushcfunction(L, l_os_exit);
    lua_setfield(L, -2, "exit");
    lua_pop(L, 1);
}

/* ------------------------------------------------------------------ */
/* Fin de script et thread Lua                                        */
/* ------------------------------------------------------------------ */

static gboolean done_idle_cb(gpointer user_data)
{
    GtScriptEngine *e = user_data;
    GThread *finished;

    /* Le thread Lua a fini : on le joint ici, sur le thread principal, jamais pendant qu'il peut attendre ce thread (M30). */
    g_mutex_lock(&e->engine_mutex);
    e->done_source_id = 0;
    finished = e->lua_thread;
    e->lua_thread = NULL;
    g_mutex_unlock(&e->engine_mutex);
    if (finished)
        g_thread_join(finished);

    /* Tout le texte du script avant le message de fin */
    log_flush(e);
    if (e->done_func)
        e->done_func(e->end, e->done_data);
    return G_SOURCE_REMOVE;
}

/* Fin de script (thread Lua) : pertes signalées, ports Lua refermés sur le thread principal, écoute du port 0 retirée, file du
 * port 0 vidée. */
static void end_of_script_cleanup(GtScriptEngine *e)
{
    report_all_overflows(e);
    for (int h = 1; h < MAX_LUA_PORTS; h++)
        lua_port_close(e, h);
    run_on_main_sync(e, main_port_disconnect_cb, e);
    gt_rx_queue_clear(e->main_queue);
}

/* Message de l'objet d'erreur en haut de la pile, sans exécuter de Lua (aucun __tostring : on est hors de tout pcall). */
static gchar *error_message(lua_State *L)
{
    if (L == NULL)
        return g_strdup("not enough memory");
    switch (lua_type(L, -1)) {
    case LUA_TSTRING: {
        /* Avec sa longueur : un octet nul est montré « \0 » au lieu de couper le message */
        size_t len;
        const char *text = lua_tolstring(L, -1, &len);
        GString *out = g_string_sized_new(len);
        append_escaped(out, text, len);
        return g_string_free(out, FALSE);
    }
    case LUA_TNUMBER:
        if (lua_isinteger(L, -1))
            return g_strdup_printf("%lld", (long long)lua_tointeger(L, -1));
        return g_strdup_printf("%.14g", (double)lua_tonumber(L, -1));
    default:
        return g_strdup_printf("(error object is a %s value)", luaL_typename(L, -1));
    }
}

/* Raison de fin, décidée par les drapeaux et non par l'objet d'erreur (un gestionnaire xpcall interrompu donne « error in error
 * handling ») ; journalise le message d'erreur ou de code de sortie avant. Thread Lua. */
static GtScriptEnd script_end_reason(GtScriptEngine *e, lua_State *L, int ret)
{
    if (g_atomic_int_get(&e->exit_requested)) {
        if (e->exit_status == 0)
            return GT_SCRIPT_END_FINISHED;
        engine_log_line(e, _("Script exited with status %lld"), (long long)e->exit_status);
        return GT_SCRIPT_END_ERROR;
    }
    if (ret == LUA_OK)
        return GT_SCRIPT_END_FINISHED;
    if (g_atomic_int_get(&e->stop_requested))
        return GT_SCRIPT_END_STOPPED;
    gchar *msg = error_message(L);
    engine_log_line(e, _("Lua error: %s"), msg);
    g_free(msg);
    return GT_SCRIPT_END_ERROR;
}

/* Exécutée par lua_pcall : bibliothèques, API et script. Plus aucune erreur n'est levée hors de cet appel protégé. */
static int protected_main(lua_State *L)
{
    const char *script = lua_touserdata(L, 1);

    luaL_openlibs(L);
    register_api(L);
    /* debug.sethook retiré : le script ne doit pas pouvoir enlever le crochet d'arrêt */
    if (lua_getglobal(L, "debug") == LUA_TTABLE) {
        lua_pushnil(L);
        lua_setfield(L, -2, "sethook");
    }
    lua_pop(L, 1);
    if (luaL_loadbuffer(L, script, strlen(script), "=script") != LUA_OK)
        return lua_error(L);
    lua_call(L, 0, 0);
    return 0;
}

static gpointer lua_thread_func(gpointer user_data)
{
    GtScriptEngine *e = user_data;
    char *script = e->script_text;   /* remis par gt_script_engine_run, libéré ici */
    int ret = LUA_ERRMEM;

    /* Nombres Lua avec un point quelle que soit la langue (M9) : LC_NUMERIC seul passe en « C », pour ce thread. Le reste (LC_MESSAGES
     * en particulier) reste celui du processus, pour que les messages du thread Lua soient traduits (M1). newlocale consomme la base
     * en cas de succès ; en cas d'échec elle est à libérer ici. */
    locale_t base = duplocale(LC_GLOBAL_LOCALE);
    locale_t c_locale = base ? newlocale(LC_NUMERIC_MASK, "C", base) : (locale_t)0;
    if (c_locale)
        uselocale(c_locale);
    else if (base)
        freelocale(base);

    /* Écoute du port principal */
    run_on_main_sync(e, main_port_connect_cb, e);

    lua_State *L = luaL_newstate();
    if (L != NULL) {
        *(GtScriptEngine **)lua_getextraspace(L) = e;
        lua_atpanic(L, panic_cb);
        lua_sethook(L, stop_hook, LUA_MASKCOUNT, HOOK_COUNT);
        /* Ni lua_pushcfunction (fonction C sans valeur liée) ni lua_pushlightuserdata n'allouent : rien ne lève hors du pcall */
        lua_pushcfunction(L, protected_main);
        lua_pushlightuserdata(L, script);
        ret = lua_pcall(L, 1, 0, 0);
    }
    GtScriptEnd end = script_end_reason(e, L, ret);

    end_of_script_cleanup(e);
    if (L != NULL) {
        /* Arrêt posé avant lua_close (raison de fin déjà décidée, ports déjà refermés) : un finaliseur (__gc) ne peut plus rien
         * ouvrir ni attendre, api_guard et std_guard refusent tout appel (M2) ; le crochet reste armé. Lua coupe les crochets pendant
         * un finaliseur : une boucle Lua pure dans un __gc n'est pas interruptible (fermeture de GTKTerm : abandon au bout de 3 s). */
        g_atomic_int_set(&e->stop_requested, 1);
        lua_close(L);
    }
    g_free(script);

    if (c_locale) {
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(c_locale);
    }

    g_mutex_lock(&e->engine_mutex);
    e->end = end;
    e->done_source_id = g_idle_add_full(G_PRIORITY_DEFAULT, done_idle_cb, e, NULL);
    g_mutex_unlock(&e->engine_mutex);
    /* Après done_source_id : gt_script_engine_shutdown, réveillé ici, retire cette source avant de joindre le thread */
    g_mutex_lock(&calls_mutex);
    e->thread_finished = TRUE;
    g_cond_broadcast(&calls_cond);
    g_mutex_unlock(&calls_mutex);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* API publique                                                       */
/* ------------------------------------------------------------------ */

void script_engine_setup(void)
{
    if (main_thread == NULL)
        main_thread = g_thread_self();
}

GtScriptEngine *gt_script_engine_new(GtkTermLink *main_link, GtMacros *macros)
{
    GtScriptEngine *e = g_new0(GtScriptEngine, 1);

    e->main_link = g_object_ref(main_link);
    e->macros = macros;
    e->main_queue = gt_rx_queue_new(GT_SCRIPT_RX_MAX);
    g_mutex_init(&e->engine_mutex);
    g_mutex_init(&e->pause_mutex);
    g_cond_init(&e->pause_cond);
    g_mutex_init(&e->log_mutex);
    return e;
}

void gt_script_engine_run(GtScriptEngine *e, const char *script_text)
{
    script_engine_setup();
    if (e->shut_down)
        return;
    g_mutex_lock(&e->engine_mutex);
    if (e->lua_thread != NULL) {
        g_mutex_unlock(&e->engine_mutex);
        engine_log_line(e, _("Script already running."));
        return;
    }
    /* Exécution suivante : drapeaux d'arrêt et de sortie, pertes signalées et file du port 0 remis à zéro avant le thread */
    g_atomic_int_set(&e->stop_requested, 0);
    g_atomic_int_set(&e->exit_requested, 0);
    e->exit_status = 0;
    memset(e->overflow_reported, 0, sizeof(e->overflow_reported));
    gt_rx_queue_reset(e->main_queue);
    g_mutex_lock(&calls_mutex);
    e->thread_finished = FALSE;
    g_mutex_unlock(&calls_mutex);
    e->script_text = g_strdup(script_text);
    e->lua_thread = g_thread_new("lua-script", lua_thread_func, e);
    g_mutex_unlock(&e->engine_mutex);
}

void gt_script_engine_stop(GtScriptEngine *e)
{
    g_atomic_int_set(&e->stop_requested, 1);
    g_mutex_lock(&e->pause_mutex);
    g_cond_broadcast(&e->pause_cond);
    g_mutex_unlock(&e->pause_mutex);
}

gboolean gt_script_engine_is_running(GtScriptEngine *e)
{
    g_mutex_lock(&e->engine_mutex);
    gboolean running = (e->lua_thread != NULL);
    g_mutex_unlock(&e->engine_mutex);
    return running;
}

void gt_script_engine_shutdown(GtScriptEngine *e)
{
    if (e->shut_down)
        return;
    e->shut_down = TRUE;
    /* Les widgets du panneau sont détruits ou vont l'être : plus de journal ni de fin vers l'interface */
    gt_script_engine_set_log_func(e, NULL, NULL);
    gt_script_engine_set_done_func(e, NULL, NULL);
    if (!gt_script_engine_is_running(e))
        return;
    gt_script_engine_stop(e);

    /* Le contexte par défaut ne tourne plus forcément : ses sources (réception de la liaison, voyants, messages, minuteries de
     * l'interface) toucheraient des widgets peut-être détruits. Ce thread exécute lui-même les demandes run_on_main_sync en attente
     * (celles de ce moteur : fermeture de ses ports, fin de l'écoute du port 0 ; et celles des autres moteurs, demandes légitimes du
     * thread principal) en retirant leurs sources, jusqu'à la fin du thread de ce moteur, au plus 3 s. */
    gint64 deadline = g_get_monotonic_time() + SHUTDOWN_TIMEOUT_US;
    gboolean finished;

    g_mutex_lock(&calls_mutex);
    for (;;) {
        /* Fin et échéance relues avant chaque demande : un autre moteur qui enchaîne ses demandes ne prolonge pas l'attente */
        if (e->thread_finished || g_get_monotonic_time() >= deadline)
            break;
        MainCall *mc = g_queue_pop_head(&pending_calls);
        if (mc != NULL) {
            g_source_remove(mc->source_id);
            g_mutex_unlock(&calls_mutex);
            main_call_run(mc);
            g_mutex_lock(&calls_mutex);
            continue;
        }
        g_cond_wait_until(&calls_cond, &calls_mutex, deadline);
    }
    finished = e->thread_finished;
    g_mutex_unlock(&calls_mutex);

    if (!finished) {
        g_warning("Lua script still running after 3 s (blocked in a standard library call?): quitting without waiting for it");
        e->abandoned = TRUE;
        return;
    }
    /* Thread fini : joint ici, sa source de fin retirée (elle joindrait un thread déjà joint et appellerait la fonction de fin) */
    g_mutex_lock(&e->engine_mutex);
    GThread *thread = e->lua_thread;
    guint source = e->done_source_id;
    e->lua_thread = NULL;
    e->done_source_id = 0;
    g_mutex_unlock(&e->engine_mutex);
    if (source != 0)
        g_source_remove(source);
    g_thread_join(thread);
}

void gt_script_engine_free(GtScriptEngine *e)
{
    guint source;

    if (e == NULL)
        return;
    gt_script_engine_shutdown(e);
    /* Thread Lua abandonné après 3 s (fermeture de GTKTerm ou d'un onglet) : il tient encore le moteur, jamais libéré. À la fermeture
     * d'un onglet, l'application continue : ce thread et ses ports Lua 1-15 restent vivants jusqu'au retour de l'appel C long qui le
     * bloque (il voit alors l'arrêt, ferme ses ports par run_on_main_sync et se termine) ; le moteur reste une fuite assumée. */
    if (e->abandoned)
        return;
    g_mutex_lock(&e->log_mutex);
    source = e->log_source_id;
    e->log_source_id = 0;
    g_mutex_unlock(&e->log_mutex);
    if (source != 0)
        g_source_remove(source);
    /* Thread joint : l'écoute du port 0 a été retirée par la fin du script ; par sécurité */
    if (e->main_port_handler != 0)
        g_signal_handler_disconnect(e->main_link, e->main_port_handler);
    gt_rx_queue_free(e->main_queue);
    if (e->log_pending != NULL)
        g_string_free(e->log_pending, TRUE);
    g_object_unref(e->main_link);
    g_mutex_clear(&e->engine_mutex);
    g_mutex_clear(&e->pause_mutex);
    g_cond_clear(&e->pause_cond);
    g_mutex_clear(&e->log_mutex);
    g_free(e);
}

void gt_script_engine_set_log_func(GtScriptEngine *e, ScriptLogFunc func, gpointer user_data)
{
    e->log_func = func;
    e->log_data = user_data;
}

void gt_script_engine_set_done_func(GtScriptEngine *e, ScriptDoneFunc func, gpointer user_data)
{
    e->done_func = func;
    e->done_data = user_data;
}
