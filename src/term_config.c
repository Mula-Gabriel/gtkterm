/***********************************************************************/
/* config.c                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Configuration of the serial port                               */
/*                                                                     */
/*   ChangeLog                                                         */
/*      - 0.99.7 : Refactor to use newer gtk widgets                   */
/*                 Add ability to use arbitrary baud                   */
/*                 Add rs458 capability - Marc Le Douarain             */
/*                 Remove auto cr/lf stuff - (use macros instead)      */
/*      - 0.99.5 : Make the combo list for the device editable         */
/*      - 0.99.3 : Configuration for VTE terminal                      */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.99.1 : fixed memory management bug                         */
/*                 test if there are devices found                     */
/*      - 0.99.0 : fixed enormous memory management bug ;-)            */
/*                 save / read macros                                  */
/*      - 0.98.5 : font saved in configuration                         */
/*                 bug fixed in memory management                      */
/*                 combos set to non editable                          */
/*      - 0.98.3 : configuration file                                  */
/*      - 0.98.2 : autodetect existing devices                         */
/*      - 0.98 : added devfs devices                                   */
/*                                                                     */
/***********************************************************************/

#include <gtk/gtk.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <glob.h>
#include <vte/vte.h>
#include <glib/gi18n.h>

#include "serial.h"
#include "term_config.h"
#include "interface.h"
#include "parsecfg.h"
#include "macros.h"
#include "i18n.h"
#include "config.h"

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#define CONFIGURATION_FILENAME ".gtktermrc"

/* Configuration file variables */
gchar **port;
guint *speed;
gint *bits;
gint *stopbits;
gchar **parity;
gchar **flow;
gint *wait_delay;
gint *wait_char;
gint *rts_time_before_tx;
gint *rts_time_after_tx;
gint *echo;
gint *crlfauto;
gint *autoreconnect_enabled;
gint *esc_clear_screen;
gint *timestamp;
gint *show_rxtx_compat; /* kept for backward compatibility with old config files */
gchar **font;

gint *block_cursor;
gint *rows;
gint *columns;
gint *scrollback;
gint *visual_bell;
gfloat *foreground_red;
gfloat *foreground_blue;
gfloat *foreground_green;
gfloat *foreground_alpha;
gfloat *background_red;
gfloat *background_blue;
gfloat *background_green;
gfloat *background_alpha;
gchar **macros_file;
gint *transport_type_cfg;
gchar **socket_host;
gchar **socket_port;
gchar **update_url_cfg;
gint *update_check_startup_cfg;
gchar **update_prefix_cfg;


cfgStruct cfg[] =
{
	{"port", CFG_STRING, &port},
	{"speed", CFG_UINT, &speed},
	{"bits", CFG_INT, &bits},
	{"stopbits", CFG_INT, &stopbits},
	{"parity", CFG_STRING, &parity},
	{"flow", CFG_STRING, &flow},
	{"wait_delay", CFG_INT, &wait_delay},
	{"wait_char", CFG_INT, &wait_char},
	{"rs485_rts_time_before_tx", CFG_INT, &rts_time_before_tx},
	{"rs485_rts_time_after_tx", CFG_INT, &rts_time_after_tx},
	{"echo", CFG_BOOL, &echo},
	{"crlfauto", CFG_BOOL, &crlfauto},
	{"autoreconnect_enabled", CFG_BOOL, &autoreconnect_enabled},
	{"esc_clear_screen", CFG_BOOL, &esc_clear_screen},
	{"timestamp", CFG_BOOL, &timestamp},
	{"show_rxtx", CFG_BOOL, &show_rxtx_compat},
	{"font", CFG_STRING, &font},
	{"term_block_cursor", CFG_BOOL, &block_cursor},
	{"term_rows", CFG_INT, &rows},
	{"term_columns", CFG_INT, &columns},
	{"term_scrollback", CFG_INT, &scrollback},
	{"term_visual_bell", CFG_BOOL, &visual_bell},
	{"term_foreground_red", CFG_FLOAT, &foreground_red},
	{"term_foreground_blue", CFG_FLOAT, &foreground_blue},
	{"term_foreground_green", CFG_FLOAT, &foreground_green},
	{"term_foreground_alpha", CFG_FLOAT, &foreground_alpha},
	{"term_background_red", CFG_FLOAT, &background_red},
	{"term_background_blue", CFG_FLOAT, &background_blue},
	{"term_background_green", CFG_FLOAT, &background_green},
	{"term_background_alpha", CFG_FLOAT, &background_alpha},
	{"macros_file", CFG_STRING, &macros_file},
	{"transport_type", CFG_INT, &transport_type_cfg},
	{"socket_host", CFG_STRING, &socket_host},
	{"socket_port", CFG_STRING, &socket_port},
	{"update_url", CFG_STRING, &update_url_cfg},
	{"update_check_startup", CFG_BOOL, &update_check_startup_cfg},
	{"update_prefix", CFG_STRING, &update_prefix_cfg},
	{NULL, CFG_END, NULL}
};

GFile *config_file;
static gchar *active_config_name = NULL;

struct configuration_port config;
display_config_t term_conf;

GtkWidget *Entry;

gint Grise_Degrise(GtkWidget *bouton, gpointer pointeur);
void transport_toggle_cb(GtkWidget *combo, gpointer user_data);
void read_font_button(GtkFontButton *fontButton);
void Hard_default_configuration(void);
void Copy_configuration(int);

static void Select_config(gchar *, void *);
static void Save_config_file(void);
static void load_config(GtkDialog *, gint, GtkTreeSelection *);
static void delete_config(GtkDialog *, gint, GtkTreeSelection *);
static void save_config(GtkDialog *, gint, GtkWidget *);
static void really_save_config(GtkDialog *, gint, gpointer);
static gint remove_section(const gchar *, gchar *);
static gboolean cursor_block(GtkSwitch *, gboolean, gpointer);
static void Selec_couleur(GdkRGBA *, gfloat, gfloat, gfloat, gfloat);
void config_fg_color(GtkWidget *button, gpointer data);
void config_bg_color(GtkWidget *button, gpointer data);
static void scrollback_set(GtkAdjustment *, gpointer);

extern GtkWidget *display;

void config_file_init(void)
{
	/*
	 * Old location of configuration file was $HOME/.gtktermrc
	 * New location is $XDG_CONFIG_HOME/.gtktermrc
	 *
	 * If configuration file exists at new location, use that one.
	 * Otherwise, if file exists at old location, move file to new location.
	 */
	GFile *config_file_old = g_file_new_build_filename(getenv("HOME"), CONFIGURATION_FILENAME, NULL);
	config_file = g_file_new_build_filename(g_get_user_config_dir(), CONFIGURATION_FILENAME, NULL);

	if (!g_file_query_exists(config_file, NULL) && g_file_query_exists(config_file_old, NULL))
		g_file_move(config_file_old, config_file, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);

	g_object_unref(config_file_old);
}

const gchar *get_config_file_path(void)
{
	return g_file_get_path(config_file);
}

/* Section names written by GKeyFile (must be invisible to parsecfg). */
static const gchar * const kf_groups[] = {
	"window", "colors", "layout", "session", NULL
};

/* Remove kf_groups from .gtktermrc so parsecfg doesn't see them.
   Returns the stripped content as an allocated string; caller must g_free. */
static gchar *cfg_strip_kf_sections(void)
{
	const gchar *path = get_config_file_path();
	gchar *contents = NULL;
	gsize  len;

	if (!g_file_get_contents(path, &contents, &len, NULL))
		return g_strdup("");

	GString *kept   = g_string_new(NULL);
	GString *extras = g_string_new(NULL);
	gchar  **lines  = g_strsplit(contents, "\n", -1);
	g_free(contents);

	gboolean in_protected = FALSE;
	for (int i = 0; lines[i] != NULL; i++) {
		const gchar *line = lines[i];
		if (line[0] == '[') {
			in_protected = FALSE;
			for (int j = 0; kf_groups[j]; j++) {
				gchar *tag = g_strdup_printf("[%s]", kf_groups[j]);
				if (g_str_has_prefix(line, tag))
					in_protected = TRUE;
				g_free(tag);
				if (in_protected) break;
			}
		}
		GString *dest = in_protected ? extras : kept;
		g_string_append(dest, line);
		if (lines[i + 1] != NULL)
			g_string_append_c(dest, '\n');
	}
	g_strfreev(lines);

	g_file_set_contents(path, kept->str, kept->len, NULL);
	g_string_free(kept, TRUE);
	return g_string_free(extras, FALSE);
}

/* Append previously extracted kf_section content back to .gtktermrc. */
static void cfg_restore_kf_sections(gchar *extras)
{
	if (!extras || !extras[0]) return;

	const gchar *path = get_config_file_path();
	gchar *existing = NULL;
	gsize  elen;

	if (!g_file_get_contents(path, &existing, &elen, NULL)) {
		existing = g_strdup("");
		elen = 0;
	}
	GString *out = g_string_new_len(existing, elen);
	g_free(existing);

	if (out->len && out->str[out->len - 1] != '\n')
		g_string_append_c(out, '\n');
	g_string_append(out, extras);

	g_file_set_contents(path, out->str, out->len, NULL);
	g_string_free(out, TRUE);
}

void save_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean script_visible)
{
	const gchar *path = get_config_file_path();
	GKeyFile *kf = g_key_file_new();
	g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL); /* load existing, preserve other sections */

	gint x, y, w, h;
	gtk_window_get_position(GTK_WINDOW(window), &x, &y);
	gtk_window_get_size(GTK_WINDOW(window), &w, &h);

	g_key_file_set_integer(kf, "window", "x", x);
	g_key_file_set_integer(kf, "window", "y", y);
	g_key_file_set_integer(kf, "window", "width", w);
	g_key_file_set_integer(kf, "window", "height", h);
	g_key_file_set_integer(kf, "window", "paned_position", gtk_paned_get_position(GTK_PANED(paned)));
	g_key_file_set_boolean(kf, "window", "script_panel_visible", script_visible);
	if (script_paned)
		g_key_file_set_integer(kf, "window", "v_paned_position", gtk_paned_get_position(GTK_PANED(script_paned)));

	gsize  length;
	gchar *data = g_key_file_to_data(kf, &length, NULL);
	GError *werr = NULL;
	if (!g_file_set_contents(path, data, (gssize)length, &werr)) {
		g_printerr("Cannot save window state: %s\n", werr ? werr->message : "?");
		g_clear_error(&werr);
	}
	g_free(data);
	g_key_file_free(kf);
}

void load_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean *script_visible_out)
{
	GKeyFile *kf = g_key_file_new();

	if (!g_key_file_load_from_file(kf, get_config_file_path(), G_KEY_FILE_NONE, NULL))
	{
		g_key_file_free(kf);
		return;
	}

	gint x    = g_key_file_get_integer(kf, "window", "x",              NULL);
	gint y    = g_key_file_get_integer(kf, "window", "y",              NULL);
	gint w    = g_key_file_get_integer(kf, "window", "width",          NULL);
	gint h    = g_key_file_get_integer(kf, "window", "height",         NULL);
	gint pos  = g_key_file_get_integer(kf, "window", "paned_position", NULL);
	gint spos = g_key_file_get_integer(kf, "window", "v_paned_position", NULL);

	if (script_visible_out)
		*script_visible_out = g_key_file_get_boolean(kf, "window", "script_panel_visible", NULL);

	g_key_file_free(kf);

	if (w > 0 && h > 0)
	{
		gtk_window_move(GTK_WINDOW(window), x, y);
		gtk_window_resize(GTK_WINDOW(window), w, h);
	}
	if (pos > 0)
		gtk_paned_set_position(GTK_PANED(paned), pos);
	if (script_paned && spos > 0)
		gtk_paned_set_position(GTK_PANED(script_paned), spos);
}

void ConfigFlags(void)
{
	Set_crlfauto(config.crlfauto);
	Set_autoreconnect_enabled(config.autoreconnect_enabled);
	Set_esc_clear_screen(config.esc_clear_screen);
	Set_timestamp(config.timestamp);
}

/* This list should perhaps be added to the configuration? */
struct device_path {
	const char *pat;
	unsigned long major;
	unsigned long minor;
	unsigned long nminors;	/* 0 = no device matching */
};

static const struct device_path default_device_paths[] =
{
	{ "/dev/ttyS*", 0, 0, 0 },
	{ "/dev/tts/[0-9]*", 0, 0, 0 },
	{ "/dev/usb/tts/[0-9]*", 0, 0, 0 },
	{ NULL, 0, 0, 0 }
};

static inline char *skip_word(char *p)
{
	while (*p && !isspace((unsigned char)*p))
		p++;
	return p;
}
static inline char *skip_space(char *p)
{
	while (isspace((unsigned char)*p))
		p++;
	return p;
}

static const struct device_path *get_device_paths(void)
{
#ifdef __linux__
	GArray *array;
	char line[BUFSIZ];
	FILE *f = fopen("/proc/tty/drivers", "r");
	if (!f)
		return default_device_paths;

	array = g_array_new(TRUE, FALSE, sizeof(struct device_path));
	while (fgets(line, sizeof line, f))
	{
		struct device_path devp;
		char *path, *p, *pat;
		struct stat st;

		p = line;
		p = skip_word(p);
		if (p == line || !*p)
			continue;

		*p++ = '\0';
		p = skip_space(p);
		if (*p != '/')
			continue;
		path = p;
		p = skip_word(p);
		if (p == path || !*p)
			continue;

		*p++ = '\0';
		devp.major = strtoul(p, &p, 10);
		if (!isspace((unsigned char)*p))
			continue;

		devp.minor = strtoul(p, &p, 10);
		devp.nminors = 1;
		if (*p == '-')
			devp.nminors = strtoul(++p, &p, 10) - devp.minor + 1;
		if (!isspace((unsigned char)*p))
			continue;

		p = skip_space(p);
		if (strncmp(p, "serial", 6))
			continue;

		/* This should be a serial driver */
		pat = "[0-9]*";
		if (!stat(path, &st))
		{
			if (S_ISDIR(st.st_mode))
				pat = "/[0-9]*";
			else if (devp.nminors == 1)
				pat = "";
		}
		devp.pat = g_strdup_printf("%s%s", path, pat);
		array = g_array_append_vals(array, &devp, 1);
	}

	fclose(f);

	if (array->len)
	{
		const struct device_path *dpp = (void *)array->data;
		g_array_free(array, FALSE);
		return dpp;
	}
	g_array_unref(array);
#endif
	return default_device_paths;
}

static void free_device_paths(const struct device_path *paths)
{
	const struct device_path *dpp;

	if (paths == default_device_paths)
		return;

	for (dpp = paths; dpp->pat; dpp++)
		g_free((void *)dpp->pat);

	g_free((void *)paths);
}

/* Compare strings with digit sequences treated in groups */
static int compare_seminum(const void *a, const void *b)
{
	const char *sa = a;
	const char *sb = b;
	char *ea, *eb;
	unsigned long na, nb;
	unsigned char ca, cb;
	gboolean da, db;

	while ((ca = *sa))
	{
		cb = *sb;
		if (!cb)
			return 1; /* End of string b: a > b */

		/* Are these digits? */
		da = isdigit(ca);
		db = isdigit(cb);

		if (da) {
			/* a is digit */
			if (!db)
				return -1; /* b is not digit: a < b */

			/* Both are numbers */
			na = strtoul(sa, &ea, 10);
			nb = strtoul(sb, &eb, 10);
			if (na != nb)
				return na < nb ? -1 : 1;

			/* Leading zeroes can cause duplicates, tie breaker */
			if ((ea - sa) != (eb - sb))
				return (ea - sa) < (eb - sb) ? -1 : 1;

			sa = ea;
			sb = eb;
		} else {
			/* a is not digit */
			if (db)
				return 1; /* b is digit: a > b */

			if (ca != cb)
				return (int)ca - (int)cb;

			sa++;
			sb++;
		}
	}

	/* If string b is also at end, equal, otherwise b > a */
	return *sb != 0;
}

static int is_serial_port(const char *path, const struct device_path *dp)
{
	struct stat st;

	if (access(path, R_OK|W_OK))
		return FALSE;	/* Not accessible */

	if (stat(path, &st) || !S_ISCHR(st.st_mode))
		return FALSE;	/* Not a character device */

	/*
	 * Unfortunately, just opening a tty messes with DTR and RTS, so
	 * it isn't possible to perform any probes that require a file
	 * descriptor!!
	 *
	 * This is a kernel API problem and needs to be fixed there...
	 */

#if defined(major) && defined(minor)
	/* Match against the device number if possible */
	if (!dp->nminors)
		return TRUE;

	if (dp->major != major(st.st_rdev))
		return FALSE;

	if ((unsigned long)minor(st.st_rdev) - dp->minor >= dp->nminors)
		return FALSE;
#endif

	return TRUE;
}

static GPtrArray *find_serial_ports(const struct device_path *devp)
{
	GPtrArray *ports = g_ptr_array_new();

	for (; devp->pat; devp++)
	{
		glob_t gl;
		char **filep;

		memset(&gl, 0, sizeof gl);

		if (glob(devp->pat, GLOB_NOSORT, NULL, &gl))
			continue;

		for (filep = gl.gl_pathv; *filep; filep++)
		{
			if (is_serial_port(*filep, devp))
				g_ptr_array_add(ports, g_strdup(*filep));
		}
	}

	g_ptr_array_sort_values(ports, compare_seminum);
	return ports;
}

static gchar *dlist_to_string(const struct device_path *dlist)
{
	const struct device_path *dp;
	gchar *str, *p, *ep;
	size_t len = 1;		/* Final null */

	for (dp = dlist; dp->pat; dp++)
		len += strlen(dp->pat) + 2; /* TAB NL */

	str = p = g_malloc(len);
	ep = str + len - 1;	/* Include space for final newline */
	for (dp = dlist; dp->pat; dp++)
	{
		*p++ = '\t';
		p += g_strlcpy(p, dp->pat, ep-p);
		*p++ = '\n';
	}
	*p = '\0';
	return str;
}

void Config_Port_Fenetre(GtkAction *action, gpointer data)
{
	GtkWidget *Table, *Label, *Bouton_OK, *Bouton_annule,
	          *Combo, *Dialogue, *Frame, *CheckBouton,
	          *Spin, *Expander, *ExpanderVbox,
	       	  *content_area, *serial_box, *tcp_frame,
	          *TransportCombo;

	static GtkWidget *Combos[13];
	GtkAdjustment *adj;
	gchar *string;
	char *prev;
	int i;
	const struct device_path *device_paths;
	GPtrArray *ports;
	int speed_index;

	device_paths = get_device_paths();
	ports = find_serial_ports(device_paths);

	if (!ports->len)
	{
		gchar *patterns = dlist_to_string(device_paths);
		string = g_strdup_printf(_("No serial devices found!\n"
					   "\n"
					   "Searched the following device path patterns:\n"
					   "%s\n"
					   "Enter a different device path in the 'Port' box.\n"), patterns);
		g_free(patterns);
		show_message(string, MSG_WRN);
		g_free(string);
	}

	free_device_paths(device_paths);

	Dialogue = gtk_dialog_new();
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(Dialogue));

	gtk_window_set_title(GTK_WINDOW(Dialogue), _("Configuration"));
	gtk_window_set_resizable(GTK_WINDOW(Dialogue), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

	/* --- Transport type selector --- */
	Frame = gtk_frame_new(_("Transport"));
	gtk_box_pack_start(GTK_BOX(content_area), Frame, FALSE, TRUE, 5);

	Table = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(Table), 5);
	gtk_grid_set_column_spacing(GTK_GRID(Table), 5);
	gtk_container_add(GTK_CONTAINER(Frame), Table);

	Label = gtk_label_new(_("Type:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 0, 1, 1);

	TransportCombo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(TransportCombo), _("Serial"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(TransportCombo), _("TCP Client"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(TransportCombo), _("TCP Server"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(TransportCombo), config.transport_type);
	gtk_grid_attach(GTK_GRID(Table), TransportCombo, 1, 0, 1, 1);
	Combos[10] = TransportCombo;

	/* --- TCP Frame --- */
	tcp_frame = gtk_frame_new(_("TCP"));
	gtk_box_pack_start(GTK_BOX(content_area), tcp_frame, FALSE, TRUE, 5);

	Table = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(Table), 5);
	gtk_grid_set_column_spacing(GTK_GRID(Table), 5);
	gtk_container_add(GTK_CONTAINER(tcp_frame), Table);

	Label = gtk_label_new(_("Host:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 0, 1, 1);
	Label = gtk_label_new(_("Port:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 1, 1, 1);

	GtkWidget *host_entry = gtk_entry_new();
	gtk_widget_set_hexpand(host_entry, TRUE);
	if(config.socket_host[0] != '\0')
		gtk_entry_set_text(GTK_ENTRY(host_entry), config.socket_host);
	else
		gtk_entry_set_text(GTK_ENTRY(host_entry), "localhost");
	gtk_grid_attach(GTK_GRID(Table), host_entry, 1, 0, 1, 1);
	Combos[11] = host_entry;

	GtkWidget *port_entry = gtk_entry_new();
	gtk_widget_set_hexpand(port_entry, TRUE);
	gtk_entry_set_input_purpose(GTK_ENTRY(port_entry), GTK_INPUT_PURPOSE_DIGITS);
	if(config.socket_port[0] != '\0')
		gtk_entry_set_text(GTK_ENTRY(port_entry), config.socket_port);
	else
		gtk_entry_set_text(GTK_ENTRY(port_entry), "2323");
	g_signal_connect(GTK_ENTRY(port_entry), "insert-text",
	                 G_CALLBACK(check_text_input), isdigit);
	gtk_grid_attach(GTK_GRID(Table), port_entry, 1, 1, 1, 1);
	Combos[12] = port_entry;

	/* --- Serial port box --- */
	serial_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(content_area), serial_box, FALSE, TRUE, 5);

	Frame = gtk_frame_new(_("Serial port"));
	gtk_box_pack_start(GTK_BOX(serial_box), Frame, FALSE, TRUE, 5);

	Table = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(Table), 5);
	gtk_grid_set_column_spacing(GTK_GRID(Table), 5);
	gtk_container_add(GTK_CONTAINER(Frame), Table);

	Label = gtk_label_new(_("Port:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 0, 1, 1);
	Label = gtk_label_new(_("Baud Rate:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 1, 0, 1, 1);
	Label = gtk_label_new(_("Parity:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 2, 0, 1, 1);

	Combo = gtk_combo_box_text_new_with_entry();

	prev = "";
	for (i = 0; i < ports->len; i++)
	{
		char *curr = ports->pdata[i];
		if (strcmp(prev, curr))
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), curr);
		prev = curr;
	}
	for (i = 0; i < ports->len; i++)
		g_free(ports->pdata[i]);
	g_ptr_array_free(ports, TRUE);

	if(config.port[0] != '\0')
	{
		GtkWidget *tmp_entry;
		tmp_entry = gtk_bin_get_child(GTK_BIN(Combo));
		gtk_entry_set_text(GTK_ENTRY(tmp_entry), config.port);
	}
	else
	{
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);
	}

	gtk_widget_set_hexpand(Combo, TRUE);
	gtk_widget_set_vexpand(Combo, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Combo, 0, 1, 1, 1);
	Combos[0] = Combo;

	Combo = gtk_combo_box_text_new_with_entry();
	gtk_entry_set_max_length(GTK_ENTRY(gtk_bin_get_child (GTK_BIN (Combo))), 10);

	speed_index = -1;
	if (!config.vitesse)
		config.vitesse = DEFAULT_SPEED;

	for(i = 0; i < baudrate_count; i++)
	{
		if (baudrate_list[i].baud == config.vitesse)
			speed_index = i;

		string = g_strdup_printf("%u", baudrate_list[i].baud);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), string);
		g_free(string);
	}

	if (speed_index < 0)
	{
		string = g_strdup_printf("%u", config.vitesse);
		gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child (GTK_BIN (Combo))), string);
		g_free(string);
	} else {
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), speed_index);
	}

	g_signal_connect(GTK_ENTRY(gtk_bin_get_child (GTK_BIN (Combo))),
			 "insert-text",
			 G_CALLBACK(check_text_input), isdigit);

	gtk_widget_set_hexpand(Combo, TRUE);
	gtk_widget_set_vexpand(Combo, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Combo, 1, 1, 1, 1);
	Combos[1] = Combo;

	Combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "none");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "odd");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "even");

	switch(config.parite)
	{
	case 0:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);
		break;
	case 1:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 1);
		break;
	case 2:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 2);
		break;
	}
	gtk_widget_set_hexpand(Combo, TRUE);
	gtk_widget_set_vexpand(Combo, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Combo, 2, 1, 1, 1);
	Combos[2] = Combo;

	Label = gtk_label_new(_("Bits:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 2, 1, 1);
	Label = gtk_label_new(_("Stopbits:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 1, 2, 1, 1);
	Label = gtk_label_new(_("Flow control:"));
	gtk_grid_attach(GTK_GRID(Table), Label, 2, 2, 1, 1);

	Combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "5");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "6");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "7");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "8");
	gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 3);

	if(config.bits >= 5 && config.bits <= 8)
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), config.bits - 5);
	gtk_widget_set_hexpand(Combo, TRUE);
	gtk_widget_set_vexpand(Combo, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Combo, 0, 3, 1, 1);
	Combos[3] = Combo;

	Combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "1");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "2");
	gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);

	if(config.stops == 1 || config.stops == 2)
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), config.stops - 1);
	gtk_widget_set_hexpand(Combo, TRUE);
	gtk_widget_set_vexpand(Combo, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Combo, 1, 3, 1, 1);
	Combos[4] = Combo;

	Combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "none");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "RTS/CTS");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "Xon/Xoff");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), "RS485-HalfDuplex(RTS)");
	gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);

	switch(config.flux)
	{
	case 0:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);
		break;
	case 1:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 2);
		break;
	case 2:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 1);
		break;
	case 3:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 3);
		break;
	}
	gtk_widget_set_hexpand(Combo, TRUE);
	gtk_widget_set_vexpand(Combo, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Combo, 2, 3, 1, 1);
	Combos[5] = Combo;

	/* Advanced expander */
	Expander = gtk_expander_new_with_mnemonic(_("Advanced Configuration Options"));
	ExpanderVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(Expander), ExpanderVbox);
	gtk_box_pack_start(GTK_BOX(serial_box), Expander, FALSE, TRUE, 5);

	Frame = gtk_frame_new(_("ASCII file transfer"));
	gtk_container_add(GTK_CONTAINER(ExpanderVbox), Frame);

	Table = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(Table), 5);
	gtk_grid_set_column_spacing(GTK_GRID(Table), 5);
	gtk_container_add(GTK_CONTAINER(Frame), Table);

	Label = gtk_label_new(_("End of line delay (milliseconds):"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 0, 1, 1);

	adj = gtk_adjustment_new(0.0, 0.0, 500.0, 10.0, 20.0, 0.0);
	Spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(Spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Spin), (gfloat)config.delai);
	gtk_widget_set_hexpand(Spin, TRUE);
	gtk_widget_set_vexpand(Spin, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Spin, 1, 0, 1, 1);
	Combos[6] = Spin;

	Entry = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(Entry), 1);

	gtk_widget_set_sensitive(GTK_WIDGET(Entry), FALSE);
	gtk_widget_set_hexpand(Entry, TRUE);
	gtk_widget_set_vexpand(Entry, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Entry, 1, 1, 1, 1);

	CheckBouton = gtk_check_button_new_with_label(_("Wait for this special character before passing to next line:"));

	g_signal_connect(GTK_WIDGET(CheckBouton), "clicked", G_CALLBACK(Grise_Degrise), (gpointer)Spin);

	if(config.car != -1)
	{
		gchar car_str[2] = {config.car, '\0'};
		gtk_entry_set_text(GTK_ENTRY(Entry), car_str);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(CheckBouton), TRUE);
	}
	gtk_grid_attach(GTK_GRID(Table), CheckBouton, 0, 1, 1, 1);
	Combos[7] = CheckBouton;

	Frame = gtk_frame_new(_("RS-485 half-duplex parameters (RTS signal used to send)"));
	gtk_container_add(GTK_CONTAINER(ExpanderVbox), Frame);

	Table = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(Table), 5);
	gtk_grid_set_column_spacing(GTK_GRID(Table), 5);
	gtk_container_add(GTK_CONTAINER(Frame), Table);

	Label = gtk_label_new(_("Time with RTS 'on' before transmit (milliseconds):"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 0, 1, 1);
	Label = gtk_label_new(_("Time with RTS 'on' after transmit (milliseconds):"));
	gtk_grid_attach(GTK_GRID(Table), Label, 0, 1, 1, 1);

	adj = gtk_adjustment_new(0.0, 0.0, 500.0, 10.0, 20.0, 0.0);
	Spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(Spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Spin), (gfloat)config.rs485_rts_time_before_transmit);
	gtk_widget_set_hexpand(Spin, TRUE);
	gtk_widget_set_vexpand(Spin, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Spin, 1, 0, 1, 1);
	Combos[8] = Spin;

	adj = gtk_adjustment_new(0.0, 0.0, 500.0, 10.0, 20.0, 0.0);
	Spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(Spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Spin), (gfloat)config.rs485_rts_time_after_transmit);
	gtk_widget_set_hexpand(Spin, TRUE);
	gtk_widget_set_vexpand(Spin, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Spin, 1, 1, 1, 1);
	Combos[9] = Spin;

	/* Toggle visibility based on transport type */
	g_object_set_data(G_OBJECT(TransportCombo), "serial-box", serial_box);
	g_object_set_data(G_OBJECT(TransportCombo), "tcp-frame", tcp_frame);
	g_signal_connect(TransportCombo, "changed", G_CALLBACK(transport_toggle_cb), NULL);

	Bouton_OK = gtk_button_new_with_label(_("OK"));
	g_signal_connect(GTK_WIDGET(Bouton_OK), "clicked", G_CALLBACK(Lis_Config), (gpointer)Combos);
	g_signal_connect_swapped(GTK_WIDGET(Bouton_OK), "clicked", G_CALLBACK(gtk_widget_destroy), GTK_WIDGET(Dialogue));
	gtk_dialog_add_action_widget(GTK_DIALOG(Dialogue), Bouton_OK, GTK_RESPONSE_ACCEPT);
	Bouton_annule = gtk_button_new_with_label(_("Cancel"));
	g_signal_connect_swapped(GTK_WIDGET(Bouton_annule), "clicked", G_CALLBACK(gtk_widget_destroy), GTK_WIDGET(Dialogue));
	gtk_dialog_add_action_widget(GTK_DIALOG(Dialogue), Bouton_annule, GTK_RESPONSE_REJECT);

	gtk_widget_show_all(Dialogue);
	transport_toggle_cb(TransportCombo, NULL);
}

void transport_toggle_cb(GtkWidget *combo, gpointer user_data)
{
	(void)user_data;
	GtkWidget *serial_box = g_object_get_data(G_OBJECT(combo), "serial-box");
	GtkWidget *tcp_frame = g_object_get_data(G_OBJECT(combo), "tcp-frame");
	gint type = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	gtk_widget_set_visible(serial_box, type == TRANSPORT_SERIAL);
	gtk_widget_set_visible(tcp_frame, type != TRANSPORT_SERIAL);
}

gint Lis_Config(GtkWidget *bouton, GtkWidget **Combos)
{
	gchar *message;

	config.transport_type = gtk_combo_box_get_active(GTK_COMBO_BOX(Combos[10]));

	if(config.transport_type == TRANSPORT_SERIAL)
	{
		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[0]));
		g_strlcpy(config.port, message, sizeof(config.port));
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[1]));
		config.vitesse = atoi(message);
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[3]));
		config.bits = atoi(message);
		g_free(message);

		config.delai = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(Combos[6]));
		config.rs485_rts_time_before_transmit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(Combos[8]));
		config.rs485_rts_time_after_transmit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(Combos[9]));

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[2]));
		if(!strcmp(message, "odd"))
			config.parite = 1;
		else if(!strcmp(message, "even"))
			config.parite = 2;
		else
			config.parite = 0;
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[4]));
		config.stops = atoi(message);
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[5]));
		if(!strcmp(message, "Xon/Xoff"))
			config.flux = 1;
		else if(!strcmp(message, "RTS/CTS"))
			config.flux = 2;
		else if(!strncmp(message, "RS485",5))
			config.flux = 3;
		else
			config.flux = 0;
		g_free(message);

		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(Combos[7])))
		{
			config.car = *gtk_entry_get_text(GTK_ENTRY(Entry));
			config.delai = 0;
		}
		else
			config.car = -1;
	}
	else
	{
		const gchar *host = gtk_entry_get_text(GTK_ENTRY(Combos[11]));
		g_strlcpy(config.socket_host, host, sizeof(config.socket_host));

		const gchar *port = gtk_entry_get_text(GTK_ENTRY(Combos[12]));
		g_strlcpy(config.socket_port, port, sizeof(config.socket_port));
	}

	Config_port();
	ConfigFlags();

	message = get_port_string();
	Set_status_message(message);
	Set_window_title(message);
	g_free(message);

	return FALSE;
}
gint Grise_Degrise(GtkWidget *bouton, gpointer pointeur)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bouton)))
	{
		gtk_widget_set_sensitive(GTK_WIDGET(Entry), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(pointeur), FALSE);
	}
	else
	{
		gtk_widget_set_sensitive(GTK_WIDGET(Entry), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(pointeur), TRUE);
	}
	return FALSE;
}

void clear_scrollback(void){
    vte_terminal_set_scrollback_lines (VTE_TERMINAL(display), 0);
    vte_terminal_set_scrollback_lines (VTE_TERMINAL(display), term_conf.scrollback);
}

void read_font_button(GtkFontButton *fontButton)
{
	g_free(term_conf.font);
	term_conf.font = g_strdup(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(fontButton)));

	if(term_conf.font != NULL)
	{
		PangoFontDescription *font_desc = pango_font_description_from_string(term_conf.font);
		vte_terminal_set_font(VTE_TERMINAL(display), font_desc);
		pango_font_description_free(font_desc);
	}
}


void select_config_callback(GtkAction *action, gpointer data)
{
	Select_config(_("Load configuration"), G_CALLBACK(load_config));
}

void save_config_silent(void)
{
	int max, cfg_num, i;
	const gchar *name = active_config_name ? active_config_name : "default";
	const gchar *path = get_config_file_path();

	gchar *kf_backup = cfg_strip_kf_sections();

	max = cfgParse(path, cfg, CFG_INI);
	if(max == -1)
	{
		cfg_restore_kf_sections(kf_backup);
		g_free(kf_backup);
		show_message(_("Cannot save configuration file!\nConfig file may contain invalid parameter.\n"), MSG_ERR);
		return;
	}

	cfg_num = -1;
	for(i = 0; i < max; i++)
	{
		if(!strcmp(name, cfgSectionNumberToName(i)))
			cfg_num = i;
	}

	if(cfg_num == -1)
	{
		max = cfgAllocForNewSection(cfg, (char *)name);
		cfg_num = max - 1;
	}
	else
	{
		if(remove_section(path, (char *)name) == -1)
		{
			cfg_restore_kf_sections(kf_backup);
			g_free(kf_backup);
			show_message(_("Cannot overwrite section!"), MSG_ERR);
			return;
		}
		if(max == cfgParse(path, cfg, CFG_INI))
		{
			cfg_restore_kf_sections(kf_backup);
			g_free(kf_backup);
			show_message(_("Cannot read configuration file!"), MSG_ERR);
			return;
		}
		max = cfgAllocForNewSection(cfg, (char *)name);
		cfg_num = max - 1;
	}

	Copy_configuration(cfg_num);
	cfgDump(path, cfg, CFG_INI, max);
	cfg_restore_kf_sections(kf_backup);
	g_free(kf_backup);
}

void save_config_callback(GtkAction *action, gpointer data)
{
	Save_config_file();
}

void delete_config_callback(GtkAction *action, gpointer data)
{
	Select_config(_("Delete configuration"), G_CALLBACK(delete_config));
}

void load_macros_file_callback(GtkAction *action, gpointer data)
{
	GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Load macros file"),
	                                                  NULL,
	                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
	                                                  "_Cancel", GTK_RESPONSE_CANCEL,
	                                                  "_Open", GTK_RESPONSE_ACCEPT,
	                                                  NULL);

	{
		const gchar *cur = macros_file_get_path();
		if (cur && g_file_test (cur, G_FILE_TEST_EXISTS))
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_path_get_dirname (cur));
		else
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_get_user_config_dir ());
	}

	if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));
		macros_file_load (path);
		add_shortcuts ();
		rebuild_macro_buttons ();
		save_config_silent ();
		g_free (path);
	}

	gtk_widget_destroy (dialog);
}

void save_macros_file_callback(GtkAction *action, gpointer data)
{
	GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Save macros file"),
	                                                  NULL,
	                                                  GTK_FILE_CHOOSER_ACTION_SAVE,
	                                                  "_Cancel", GTK_RESPONSE_CANCEL,
	                                                  "_Save", GTK_RESPONSE_ACCEPT,
	                                                  NULL);

	{
		const gchar *cur = macros_file_get_path();
		if (cur && g_file_test (cur, G_FILE_TEST_EXISTS))
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_path_get_dirname (cur));
		else
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_get_user_config_dir ());
	}
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(dialog), "gtkterm_macros.ini");

	if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));
		macros_file_save (path);
		save_config_silent ();
		g_free (path);
	}

	gtk_widget_destroy (dialog);
}

void Select_config(gchar *title, void *callback)
{
	GtkWidget *dialog;
	GtkWidget *content_area;
	gint i, max;

	GtkWidget *Frame, *Scroll, *Liste, *Label;
	gchar *texte_label;

	GtkListStore *Modele_Liste;
	GtkTreeIter iter_Liste;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *Colonne;
	GtkTreeSelection *Selection_Liste;

	enum
	{
		N_texte,
		N_COLONNES
	};

	/* Parse the config file */

	max = cfgParse(g_file_get_path(config_file), cfg, CFG_INI);

	if(max == -1)
	{
		show_message(_("Cannot read configuration file!\nConfig file may contain invalid parameter.\n"), MSG_ERR);
		return;
	}

	else
	{
		gchar *Titre[]= {_("Configurations")};

		dialog = gtk_dialog_new_with_buttons (title,
		                                      NULL,
		                                      GTK_DIALOG_DESTROY_WITH_PARENT,
		                                      "_Cancel",
		                                      GTK_RESPONSE_NONE,
		                                      "_OK",
		                                      GTK_RESPONSE_ACCEPT,
		                                      NULL);

		content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

		Modele_Liste = gtk_list_store_new(N_COLONNES, G_TYPE_STRING);

		Liste = gtk_tree_view_new_with_model(GTK_TREE_MODEL(Modele_Liste));
		gtk_tree_view_set_search_column(GTK_TREE_VIEW(Liste), N_texte);

		Selection_Liste = gtk_tree_view_get_selection(GTK_TREE_VIEW(Liste));
		gtk_tree_selection_set_mode(Selection_Liste, GTK_SELECTION_SINGLE);

		Frame = gtk_frame_new(NULL);

		Scroll = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(Scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(Frame), Scroll);
		gtk_container_add(GTK_CONTAINER(Scroll), Liste);

		renderer = gtk_cell_renderer_text_new();

		g_object_set(G_OBJECT(renderer), "xalign", (gfloat)0.5, NULL);
		Colonne = gtk_tree_view_column_new_with_attributes(Titre[0], renderer, "text", 0, NULL);
		gtk_tree_view_column_set_sort_column_id(Colonne, 0);

		Label=gtk_label_new("");
		texte_label = g_strdup_printf("<span weight=\"bold\" style=\"italic\">%s</span>", Titre[0]);
		gtk_label_set_markup(GTK_LABEL(Label), texte_label);
		g_free(texte_label);
		gtk_tree_view_column_set_widget(GTK_TREE_VIEW_COLUMN(Colonne), Label);
		gtk_widget_show(Label);

		gtk_tree_view_column_set_alignment(GTK_TREE_VIEW_COLUMN(Colonne), 0.5f);
		gtk_tree_view_column_set_resizable(GTK_TREE_VIEW_COLUMN(Colonne), FALSE);
		gtk_tree_view_append_column(GTK_TREE_VIEW(Liste), Colonne);


		for(i = 0; i < max; i++)
		{
			gtk_list_store_append(Modele_Liste, &iter_Liste);
			gtk_list_store_set(Modele_Liste, &iter_Liste, N_texte, cfgSectionNumberToName(i), -1);
		}

		gtk_widget_set_size_request(GTK_WIDGET(dialog), 200, 200);

		g_signal_connect(GTK_WIDGET(dialog), "response", G_CALLBACK (callback), GTK_TREE_SELECTION(Selection_Liste));
		g_signal_connect_swapped(GTK_WIDGET(dialog), "response", G_CALLBACK(gtk_widget_destroy), GTK_WIDGET(dialog));

		gtk_box_pack_start (GTK_BOX (content_area), Frame, TRUE, TRUE, 0);

		gtk_widget_show_all (dialog);
	}
}

void Save_config_file(void)
{
	GtkWidget *dialog, *content_area, *label, *box, *entry;

	dialog = gtk_dialog_new_with_buttons (_("Save configuration"),
	                                      NULL,
	                                      GTK_DIALOG_DESTROY_WITH_PARENT,
	                                      "_Cancel",
	                                      GTK_RESPONSE_NONE,
	                                      "_OK",
	                                      GTK_RESPONSE_ACCEPT,
	                                      NULL);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	label = gtk_label_new(_("Configuration name: "));

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "default");
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);

	//validate input text (alpha-numeric only)
	g_signal_connect(GTK_ENTRY(entry),
	                 "insert-text",
	                 G_CALLBACK(check_text_input), isalnum);
	g_signal_connect(GTK_WIDGET(dialog), "response", G_CALLBACK(save_config), GTK_ENTRY(entry));
	g_signal_connect_swapped(GTK_WIDGET(dialog), "response", G_CALLBACK(gtk_widget_destroy), GTK_WIDGET(dialog));

	gtk_box_pack_start(GTK_BOX(content_area), box, TRUE, FALSE, 5);

	gtk_widget_show_all (dialog);
}

void really_save_config(GtkDialog *Fenetre, gint id, gpointer data)
{
	int max, cfg_num, i;

	cfg_num = -1;

	if(id == GTK_RESPONSE_ACCEPT)
	{
		const gchar *path = get_config_file_path();
		gchar *kf_backup = cfg_strip_kf_sections();

		max = cfgParse(path, cfg, CFG_INI);

		if(max == -1)
		{
			cfg_restore_kf_sections(kf_backup);
			g_free(kf_backup);
			show_message(_("Cannot save configuration file!\nConfig file may contain invalid parameter.\n"), MSG_ERR);
			return;
		}

		for(i = 0; i < max; i++)
		{
			if(!strcmp((char *)data, cfgSectionNumberToName(i)))
				cfg_num = i;
		}

		/* not overwriting */
		if(cfg_num == -1)
		{
			max = cfgAllocForNewSection(cfg, (char *)data);
			cfg_num = max - 1;
		}
		else
		{
			if(remove_section(path, (char *)data) == -1)
			{
				cfg_restore_kf_sections(kf_backup);
				g_free(kf_backup);
				show_message(_("Cannot overwrite section!"), MSG_ERR);
				return;
			}
			if(max == cfgParse(path, cfg, CFG_INI))
			{
				cfg_restore_kf_sections(kf_backup);
				g_free(kf_backup);
				show_message(_("Cannot read configuration file!"), MSG_ERR);
				return;
			}
			max = cfgAllocForNewSection(cfg, (char *)data);
			cfg_num = max - 1;
		}

		Copy_configuration(cfg_num);
		cfgDump(path, cfg, CFG_INI, max);
		cfg_restore_kf_sections(kf_backup);
		g_free(kf_backup);
	}
	else
		Save_config_file();
}

void save_config(GtkDialog *Fenetre, gint id, GtkWidget *edit)
{
	int max, i;
	const gchar *config_name;

	if(id == GTK_RESPONSE_ACCEPT)
	{
		max = cfgParse(g_file_get_path(config_file), cfg, CFG_INI);

		if(max == -1)
		{
			show_message(_("Cannot write configuration file!\nConfig file may contain invalid parameter.\n"), MSG_ERR);
			return;
		}

		config_name = gtk_entry_get_text(GTK_ENTRY(edit));

		for(i = 0; i < max; i++)
		{
			if(!strcmp(config_name, cfgSectionNumberToName(i)))
			{
				GtkWidget *message_dialog;
				message_dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(Fenetre),
				                 GTK_DIALOG_DESTROY_WITH_PARENT,
				                 GTK_MESSAGE_QUESTION,
				                 GTK_BUTTONS_NONE,
				                 _("<b>Section [%s] already exists.</b>\n\nDo you want to overwrite it ?"),
				                 config_name);

				gtk_dialog_add_buttons(GTK_DIALOG(message_dialog),
				                       "_Cancel",
				                       GTK_RESPONSE_NONE,
				                       "_Yes",
				                       GTK_RESPONSE_ACCEPT,
				                       NULL);

				if (gtk_dialog_run(GTK_DIALOG(message_dialog)) == GTK_RESPONSE_ACCEPT)
					really_save_config(Fenetre, GTK_RESPONSE_ACCEPT, (gpointer)config_name);

				gtk_widget_destroy(message_dialog);

				i = max + 1;
			}
		}
		if(i == max) /* Section does not exist */
			really_save_config(Fenetre, GTK_RESPONSE_ACCEPT, (gpointer)config_name);
	}
}

void load_config(GtkDialog *Fenetre, gint id, GtkTreeSelection *Selection_Liste)
{
	GtkTreeIter iter;
	GtkTreeModel *Modele;
	gchar *txt, *message;

	if(id == GTK_RESPONSE_ACCEPT)
	{
		if(gtk_tree_selection_get_selected(Selection_Liste, &Modele, &iter))
		{
			gtk_tree_model_get(GTK_TREE_MODEL(Modele), &iter, 0, &txt, -1);
			Load_configuration_from_file(txt);
			Verify_configuration();
			Config_port();
			ConfigFlags();
			add_shortcuts();

			message = get_port_string();
			Set_status_message(message);
			Set_window_title(message);
			g_free(message);
		}
	}
}

void delete_config(GtkDialog *Fenetre, gint id, GtkTreeSelection *Selection_Liste)
{
	GtkTreeIter iter;
	GtkTreeModel *Modele;
	gchar *txt;

	if(id == GTK_RESPONSE_ACCEPT)
	{
		if(gtk_tree_selection_get_selected(Selection_Liste, &Modele, &iter))
		{
			gtk_tree_model_get(GTK_TREE_MODEL(Modele), &iter, 0, &txt, -1);
			if(remove_section(g_file_get_path(config_file), txt) == -1)
				show_message(_("Cannot delete section!"), MSG_ERR);
		}
	}
}

gint Load_configuration_from_file(gchar *config_name)
{
	int max, i;
	g_free(active_config_name);
	active_config_name = g_strdup(config_name);
	gchar *string = NULL;

	max = cfgParse(g_file_get_path(config_file), cfg, CFG_INI);

	if(max == -1)
	{
		show_message(_("Cannot read configuration file!\nConfig file may contain invalid parameter.\n"), MSG_ERR);
		return -1;
	}

	else
	{
		for(i = 0; i < max; i++)
		{
			if(!strcmp(config_name, cfgSectionNumberToName(i)))
			{
				Hard_default_configuration();

				if(port[i] != NULL)
					g_strlcpy(config.port, port[i], sizeof(config.port));
				if(speed[i] != 0)
					config.vitesse = speed[i];
				if(bits[i] != 0)
					config.bits = bits[i];
				if(stopbits[i] != 0)
					config.stops = stopbits[i];
				if(parity[i] != NULL)
				{
					if(!g_ascii_strcasecmp(parity[i], "none"))
						config.parite = 0;
					else if(!g_ascii_strcasecmp(parity[i], "odd"))
						config.parite = 1;
					else if(!g_ascii_strcasecmp(parity[i], "even"))
						config.parite = 2;
				}
				if(flow[i] != NULL)
				{
					if(!g_ascii_strcasecmp(flow[i], "none"))
						config.flux = 0;
					else if(!g_ascii_strcasecmp(flow[i], "xon"))
						config.flux = 1;
					else if(!g_ascii_strcasecmp(flow[i], "rts"))
						config.flux = 2;
					else if(!g_ascii_strcasecmp(flow[i], "rs485"))
						config.flux = 3;
				}

				config.delai = wait_delay[i];

				if(wait_char[i] != 0)
					config.car = (signed char)wait_char[i];
				else
					config.car = -1;

				config.rs485_rts_time_before_transmit = rts_time_before_tx[i];
				config.rs485_rts_time_after_transmit = rts_time_after_tx[i];

				if(echo[i] != -1)
					config.echo = (gboolean)echo[i];
				else
					config.echo = FALSE;

				if(crlfauto[i] != -1)
					config.crlfauto = (gboolean)crlfauto[i];
				else
					config.crlfauto = FALSE;

				if(autoreconnect_enabled[i] != -1)
					config.autoreconnect_enabled = (gboolean)autoreconnect_enabled[i];
				else
					config.autoreconnect_enabled = FALSE;

				if(esc_clear_screen[i] != -1)
					config.esc_clear_screen = (gboolean)esc_clear_screen[i];
				else
					config.esc_clear_screen = FALSE;

			if(timestamp[i] != -1)
				config.timestamp = (gboolean)timestamp[i];
			else
				config.timestamp = FALSE;

			if(transport_type_cfg[i] != 0)
				config.transport_type = transport_type_cfg[i];
			else
				config.transport_type = TRANSPORT_SERIAL;

			if(socket_host[i] != NULL)
				g_strlcpy(config.socket_host, socket_host[i], sizeof(config.socket_host));
			else
				config.socket_host[0] = '\0';

			if(socket_port[i] != NULL)
				g_strlcpy(config.socket_port, socket_port[i], sizeof(config.socket_port));
			else
				config.socket_port[0] = '\0';

			if(update_url_cfg != NULL && update_url_cfg[i] != NULL)
				g_strlcpy(config.update_url, update_url_cfg[i], sizeof(config.update_url));
			else
				g_strlcpy(config.update_url, "https://github.com/Mula-Gabriel/gtkterm.git", sizeof(config.update_url));

			if(update_check_startup_cfg != NULL && update_check_startup_cfg[i] != -1)
				config.update_check_startup = (gboolean)update_check_startup_cfg[i];
			else
				config.update_check_startup = TRUE;

			if(update_prefix_cfg != NULL && update_prefix_cfg[i] != NULL)
				g_strlcpy(config.update_prefix, update_prefix_cfg[i], sizeof(config.update_prefix));
			else
				g_strlcpy(config.update_prefix, "/usr/local", sizeof(config.update_prefix));

		g_free(term_conf.font);
			term_conf.font = (font[i] != NULL) ? g_strdup(font[i]) : g_strdup(DEFAULT_FONT);

			/* Restaurer le chemin du fichier macros */
			if (macros_file[i] != NULL)
				macros_file_set_path(macros_file[i]);

			/* Chargement des macros et listes depuis le fichier séparé */
				macros_file_load (NULL);
				add_shortcuts ();
				rebuild_macro_buttons ();

				if(block_cursor[i] != -1)
					term_conf.block_cursor = (gboolean)block_cursor[i];
				else
					term_conf.block_cursor = TRUE;

				if(rows[i] != 0)
					term_conf.rows = rows[i];

				if(columns[i] != 0)
					term_conf.columns = columns[i];

				if(scrollback[i] != 0)
					term_conf.scrollback = scrollback[i];

				if(visual_bell[i] != -1)
					term_conf.visual_bell = (gboolean)visual_bell[i];
				else
					term_conf.visual_bell = FALSE;

				term_conf.foreground_color.red = foreground_red[i];
		  		term_conf.foreground_color.green = foreground_green[i];
				term_conf.foreground_color.blue = foreground_blue[i];
				term_conf.foreground_color.alpha = foreground_alpha[i];

				term_conf.background_color.red = background_red[i];
				term_conf.background_color.green = background_green[i];
				term_conf.background_color.blue = background_blue[i];
				term_conf.background_color.alpha = background_alpha[i];

				/* rows and columns are empty when the conf is autogenerate in the
				   first save; so set term to default */
				if(rows[i] == 0 || columns[i] == 0)
				{
					term_conf.block_cursor = TRUE;
					term_conf.rows = 80;
					term_conf.columns = 25;
					term_conf.scrollback = DEFAULT_SCROLLBACK;
					term_conf.visual_bell = FALSE;
                                  	Selec_couleur(&term_conf.foreground_color, DEFAULT_FOREGROUNDCOLOR);
	                                Selec_couleur(&term_conf.background_color, DEFAULT_BACKGROUDCOLOR);
				}

				i = max + 1;
			}
		}
		if(i == max)
		{
			string = g_strdup_printf(_("No section \"%s\" in configuration file\n"), config_name);
			show_message(string, MSG_ERR);
			g_free(string);
			return -1;
		}
	}

	{
		PangoFontDescription *font_desc = pango_font_description_from_string(term_conf.font);
		vte_terminal_set_font(VTE_TERMINAL(display), font_desc);
		pango_font_description_free(font_desc);
	}

	vte_terminal_set_size (VTE_TERMINAL(display), term_conf.rows, term_conf.columns);
	vte_terminal_set_scrollback_lines (VTE_TERMINAL(display), term_conf.scrollback);
	vte_terminal_set_color_foreground (VTE_TERMINAL(display), &term_conf.foreground_color);
	vte_terminal_set_color_background (VTE_TERMINAL(display), &term_conf.background_color);
	vte_terminal_set_cursor_shape(VTE_TERMINAL(display), term_conf.block_cursor ? VTE_CURSOR_SHAPE_BLOCK : VTE_CURSOR_SHAPE_IBEAM);
	gtk_widget_queue_draw(display);

	return 0;
}

void Verify_configuration(void)
{
	gchar *string = NULL;

	if (find_standard_baudrate(config.vitesse) == B0)
	{
		/* Is this really meaningful? Plenty of the whitelisted
		   rates are not supported on standard hardware... */
		string = g_strdup_printf(_("Baud rate %u may not be supported by all hardware"), config.vitesse);
		show_message(string, MSG_ERR);
		g_free(string);
	}

	if(config.stops != 1 && config.stops != 2)
	{
		string = g_strdup_printf(_("Invalid number of stop-bits: %d\nFalling back to default number of stop-bits number: %d\n"), config.stops, DEFAULT_STOP);
		show_message(string, MSG_ERR);
		config.stops = DEFAULT_STOP;
		g_free(string);
	}

	if(config.bits < 5 || config.bits > 8)
	{
		string = g_strdup_printf(_("Invalid number of bits: %d\nFalling back to default number of bits: %d\n"), config.bits, DEFAULT_BITS);
		show_message(string, MSG_ERR);
		config.bits = DEFAULT_BITS;
		g_free(string);
	}

	if(config.delai < 0 || config.delai > 500)
	{
		string = g_strdup_printf(_("Invalid delay: %d ms\nFalling back to default delay: %d ms\n"), config.delai, DEFAULT_DELAY);
		show_message(string, MSG_ERR);
		config.delai = DEFAULT_DELAY;
		g_free(string);
	}

	if(term_conf.font == NULL)
		term_conf.font = g_strdup(DEFAULT_FONT);

}

gint Check_configuration_file(void)
{
	struct stat my_stat;
	gchar *string = NULL;

	/* is configuration file present ? */
	if(stat(g_file_get_path(config_file), &my_stat) == 0)
	{
		/* If bad configuration file, fallback to _hardcoded_ defaults! */
		if(Load_configuration_from_file("default") == -1)
		{
			Hard_default_configuration();
			return -1;
		}
	}

	/* if not, create it, with the [default] section */
	else
	{
		string = g_strdup_printf(_("Configuration file (%s) with\n[default] configuration has been created.\n"), g_file_get_path(config_file));
		show_message(string, MSG_WRN);
		cfgAllocForNewSection(cfg, "default");
		Hard_default_configuration();
		Copy_configuration(0);
		cfgDump(g_file_get_path(config_file), cfg, CFG_INI, 1);
		g_free(string);
	}
	return 0;
}

void Hard_default_configuration(void)
{
	strcpy(config.port, DEFAULT_PORT);
	config.vitesse = DEFAULT_SPEED;
	config.parite = DEFAULT_PARITY;
	config.bits = DEFAULT_BITS;
	config.stops = DEFAULT_STOP;
	config.flux = DEFAULT_FLOW;
	config.delai = DEFAULT_DELAY;
	config.rs485_rts_time_before_transmit = DEFAULT_DELAY_RS485;
	config.rs485_rts_time_after_transmit = DEFAULT_DELAY_RS485;
	config.car = DEFAULT_CHAR;
	config.echo = DEFAULT_ECHO;
	config.crlfauto = FALSE;
	config.autoreconnect_enabled = FALSE;
	config.esc_clear_screen = FALSE;
	config.timestamp = FALSE;
	config.disable_port_lock = FALSE;
	config.transport_type = TRANSPORT_SERIAL;
	config.socket_host[0] = '\0';
	config.socket_port[0] = '\0';
	g_strlcpy(config.update_url, "https://github.com/Mula-Gabriel/gtkterm.git", sizeof(config.update_url));
	config.update_check_startup = TRUE;
	g_strlcpy(config.update_prefix, "/usr/local", sizeof(config.update_prefix));

	term_conf.font = g_strdup_printf(DEFAULT_FONT);

	term_conf.block_cursor = TRUE;
	term_conf.rows = 80;
	term_conf.columns = 25;
	term_conf.scrollback = DEFAULT_SCROLLBACK;
	term_conf.visual_bell = TRUE;

	Selec_couleur(&term_conf.foreground_color, DEFAULT_FOREGROUNDCOLOR);
	Selec_couleur(&term_conf.background_color, DEFAULT_BACKGROUDCOLOR);
  	vte_terminal_set_color_foreground (VTE_TERMINAL(display), &term_conf.foreground_color);
	vte_terminal_set_color_background (VTE_TERMINAL(display), &term_conf.background_color);
}

void Copy_configuration(int pos)
{
	gchar *string = NULL;

	string = g_strdup(config.port);
	cfgStoreValue(cfg, "port", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.vitesse);
	cfgStoreValue(cfg, "speed", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.bits);
	cfgStoreValue(cfg, "bits", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.stops);
	cfgStoreValue(cfg, "stopbits", string, CFG_INI, pos);
	g_free(string);

	switch(config.parite)
	{
	case 0:
		string = g_strdup_printf("none");
		break;
	case 1:
		string = g_strdup_printf("odd");
		break;
	case 2:
		string = g_strdup_printf("even");
		break;
	default:
		string = g_strdup_printf("none");
	}
	cfgStoreValue(cfg, "parity", string, CFG_INI, pos);
	g_free(string);

	switch(config.flux)
	{
	case 0:
		string = g_strdup_printf("none");
		break;
	case 1:
		string = g_strdup_printf("xon");
		break;
	case 2:
		string = g_strdup_printf("rts");
		break;
	case 3:
		string = g_strdup_printf("rs485");
		break;
	default:
		string = g_strdup_printf("none");
	}

	cfgStoreValue(cfg, "flow", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.delai);
	cfgStoreValue(cfg, "wait_delay", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.car);
	cfgStoreValue(cfg, "wait_char", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.rs485_rts_time_before_transmit);
	cfgStoreValue(cfg, "rs485_rts_time_before_tx", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%d", config.rs485_rts_time_after_transmit);
	cfgStoreValue(cfg, "rs485_rts_time_after_tx", string, CFG_INI, pos);
	g_free(string);

	if(config.echo == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");

	cfgStoreValue(cfg, "echo", string, CFG_INI, pos);
	g_free(string);

	if(config.crlfauto == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");

	cfgStoreValue(cfg, "crlfauto", string, CFG_INI, pos);
	g_free(string);

	if(config.autoreconnect_enabled == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");

	cfgStoreValue(cfg, "autoreconnect_enabled", string, CFG_INI, pos);
	g_free(string);

	if(config.esc_clear_screen == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");

	cfgStoreValue(cfg, "esc_clear_screen", string, CFG_INI, pos);
	g_free(string);

	if(config.timestamp == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");

	cfgStoreValue(cfg, "timestamp", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup(term_conf.font);
	cfgStoreValue(cfg, "font", string, CFG_INI, pos);
	g_free(string);

	if(term_conf.block_cursor == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");
	cfgStoreValue(cfg, "term_block_cursor", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", term_conf.rows);
	cfgStoreValue(cfg, "term_rows", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", term_conf.columns);
	cfgStoreValue(cfg, "term_columns", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", term_conf.scrollback);
	cfgStoreValue(cfg, "term_scrollback", string, CFG_INI, pos);
	g_free(string);

	if(term_conf.visual_bell == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");
	cfgStoreValue(cfg, "term_visual_bell", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%f", term_conf.foreground_color.red);
	cfgStoreValue(cfg, "term_foreground_red", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%f", term_conf.foreground_color.green);
	cfgStoreValue(cfg, "term_foreground_green", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%f", term_conf.foreground_color.blue);
	cfgStoreValue(cfg, "term_foreground_blue", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%f", term_conf.foreground_color.alpha);
	cfgStoreValue(cfg, "term_foreground_alpha", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%f", term_conf.background_color.red);
	cfgStoreValue(cfg, "term_background_red", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%f", term_conf.background_color.green);
	cfgStoreValue(cfg, "term_background_green", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%f", term_conf.background_color.blue);
	cfgStoreValue(cfg, "term_background_blue", string, CFG_INI, pos);
	g_free(string);
	string = g_strdup_printf("%f", term_conf.background_color.alpha);
	cfgStoreValue(cfg, "term_background_alpha", string, CFG_INI, pos);
	g_free(string);

	string = g_strdup_printf("%d", config.transport_type);
	cfgStoreValue(cfg, "transport_type", string, CFG_INI, pos);
	g_free(string);

	cfgStoreValue(cfg, "socket_host", config.socket_host, CFG_INI, pos);
	cfgStoreValue(cfg, "socket_port", config.socket_port, CFG_INI, pos);

	cfgStoreValue(cfg, "update_url", config.update_url, CFG_INI, pos);

	if(config.update_check_startup == FALSE)
		string = g_strdup_printf("False");
	else
		string = g_strdup_printf("True");
	cfgStoreValue(cfg, "update_check_startup", string, CFG_INI, pos);
	g_free(string);

	cfgStoreValue(cfg, "update_prefix", config.update_prefix, CFG_INI, pos);

	const gchar *mfp = macros_file_get_path();
	if (mfp)
	{
		string = g_strdup(mfp);
		cfgStoreValue(cfg, "macros_file", string, CFG_INI, pos);
		g_free(string);
	}
}


gint remove_section(const gchar *cfg_file, gchar *section)
{
	FILE *f = NULL;
	char *buffer = NULL;
	char *buf;
	long size;
	gchar *to_search;
	long i, j, length, sect;

	f = fopen(cfg_file, "r");
	if(f == NULL)
	{
		perror(cfg_file);
		return -1;
	}

	fseek(f, 0L, SEEK_END);
	size = ftell(f);
	rewind(f);

	buffer = g_malloc(size);
	if(buffer == NULL)
	{
		perror("malloc");
		return -1;
	}

	if(fread(buffer, 1, size, f) != size)
	{
		perror(cfg_file);
		fclose(f);
		return -1;
	}

	to_search = g_strdup_printf("[%s]", section);
	length = strlen(to_search);

	/* Search section */
	for(i = 0; i < size - length; i++)
	{
		for(j = 0; j < length; j++)
		{
			if(to_search[j] != buffer[i + j])
				break;
		}
		if(j == length)
			break;
	}

	if(i == size - length)
	{
		i18n_printf(_("Cannot find section %s\n"), to_search);
		return -1;
	}

	sect = i;

	/* Search for next section */
	for(i = sect + length; i < size; i++)
	{
		if(buffer[i] == '[')
			break;
	}

	f = fopen(cfg_file, "w");
	if(f == NULL)
	{
		perror(cfg_file);
		return -1;
	}

	fwrite(buffer, 1, sect, f);
	if(i < size)
	{
		buf = buffer + i;
		fwrite(buf, 1, size - i, f);
	}
	fclose(f);

	g_free(to_search);
	g_free(buffer);

	return 0;
}


void Config_Terminal(GtkAction *action, gpointer data)
{
	GtkBuilder *builder;
	builder = gtk_builder_new_from_resource("/org/gtk/gtkterm/config_terminal_dialog.ui");

	GtkWidget *dialog;
	dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(Fenetre));
	gtk_window_set_title(GTK_WINDOW(dialog), _("Main Window"));

	/** Connect signals **/
	// Font Selection Button
	GtkWidget *cfg_terminal_font;
	cfg_terminal_font = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_terminal_font"));
	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(cfg_terminal_font), term_conf.font);
	g_signal_connect(cfg_terminal_font, "font-set", G_CALLBACK(read_font_button), 0);

	// Scrollback Lines
	GtkAdjustment *cfg_scrollback_lines;
	cfg_scrollback_lines = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "cfg_scrollback_lines"));
	gtk_adjustment_set_value(cfg_scrollback_lines, term_conf.scrollback);
	g_signal_connect(G_OBJECT(cfg_scrollback_lines), "value_changed", G_CALLBACK(scrollback_set), 0);

	// Show cursor
	GtkWidget *cfg_block_cursor;
	cfg_block_cursor = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_block_cursor"));
	gtk_switch_set_active(GTK_SWITCH(cfg_block_cursor), term_conf.block_cursor);
	g_signal_connect(cfg_block_cursor, "state_set", G_CALLBACK(cursor_block), 0);

	// Text color
	GtkWidget *cfg_text_color;
	cfg_text_color = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_text_color"));
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cfg_text_color), &term_conf.foreground_color);
	g_signal_connect(cfg_text_color, "color-set", G_CALLBACK (config_fg_color), 0);

	// Background color
	GtkWidget *cfg_background_color;
	cfg_background_color = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_background_color"));
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cfg_background_color), &term_conf.background_color);
	g_signal_connect(cfg_background_color, "color-set", G_CALLBACK (config_bg_color), 0);

	// Close button
	GtkWidget *close;
	close = GTK_WIDGET(gtk_builder_get_object(builder, "close"));
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), GTK_WIDGET(dialog));

	gtk_widget_show_all(dialog);
	g_object_unref(builder);
}

gboolean cursor_block(GtkSwitch *ToggleSwitch, gboolean state, gpointer data)
{
	term_conf.block_cursor = state;
	vte_terminal_set_cursor_shape(VTE_TERMINAL(display), term_conf.block_cursor ? VTE_CURSOR_SHAPE_BLOCK : VTE_CURSOR_SHAPE_IBEAM);
	return FALSE;
}

void Selec_couleur(GdkRGBA *color, gfloat R, gfloat G, gfloat B, gfloat A)
{
	color->red = R;
	color->green = G;
	color->blue = B;
	color->alpha = A;
}

void config_fg_color(GtkWidget *button, gpointer data)
{
	gchar *string;

	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &term_conf.foreground_color);

	vte_terminal_set_color_foreground (VTE_TERMINAL(display), &term_conf.foreground_color);
	gtk_widget_queue_draw (display);

	string = g_strdup_printf ("%f", term_conf.foreground_color.red);
	cfgStoreValue (cfg, "term_foreground_red", string, CFG_INI, 0);
	g_free (string);
	string = g_strdup_printf ("%f", term_conf.foreground_color.green);
	cfgStoreValue (cfg, "term_foreground_green", string, CFG_INI, 0);
	g_free (string);
	string = g_strdup_printf ("%f", term_conf.foreground_color.blue);
	cfgStoreValue (cfg, "term_foreground_blue", string, CFG_INI, 0);
	g_free (string);
	string = g_strdup_printf ("%f", term_conf.foreground_color.alpha);
	cfgStoreValue (cfg, "term_foreground_alpha", string, CFG_INI, 0);
	g_free (string);
}

void config_bg_color(GtkWidget *button, gpointer data)
{
	gchar *string;

	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &term_conf.background_color);

	vte_terminal_set_color_background (VTE_TERMINAL(display), &term_conf.background_color);
	gtk_widget_queue_draw (display);

	string = g_strdup_printf ("%f", term_conf.background_color.red);
	cfgStoreValue (cfg, "term_background_red", string, CFG_INI, 0);
	g_free (string);
	string = g_strdup_printf ("%f", term_conf.background_color.green);
	cfgStoreValue (cfg, "term_background_green", string, CFG_INI, 0);
	g_free (string);
	string = g_strdup_printf ("%f", term_conf.background_color.blue);
	cfgStoreValue (cfg, "term_background_blue", string, CFG_INI, 0);
	g_free (string);
	string = g_strdup_printf ("%f", term_conf.background_color.alpha);
	cfgStoreValue (cfg, "term_background_alpha", string, CFG_INI, 0);
	g_free (string);
}

void scrollback_set(GtkAdjustment *Adjustment, gpointer data)
{
	gint scrollback = gtk_adjustment_get_value(Adjustment);
	term_conf.scrollback = scrollback;
	vte_terminal_set_scrollback_lines (VTE_TERMINAL(display), term_conf.scrollback);
}

/**
 *  Filter user data entry on a GTK entry
 *
 *  user_data must be a function that takes an int and returns an int
 *  != 0 if the input is valid.  For instance, 'isdigit()'.
 */
void check_text_input(GtkEditable *editable,
                      gchar       *new_text,
                      gint         new_text_length,
                      gint        *position,
                      gpointer     user_data)
{
	int i;
	int (*check_func)(int) = NULL;

	if(user_data == NULL)
	{
		return;
	}

	g_signal_handlers_block_by_func(editable,
	                                (gpointer)check_text_input, user_data);
	check_func = (int (*)(int))user_data;

	for(i = 0; i < new_text_length; i++)
	{
		if(!check_func(new_text[i]))
			goto invalid_input;
	}

	gtk_editable_insert_text(editable, new_text, new_text_length, position);

invalid_input:
	g_signal_handlers_unblock_by_func(editable,
	                                  (gpointer)check_text_input, user_data);
	g_signal_stop_emission_by_name(editable, "insert-text");
}
