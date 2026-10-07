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

#include "term_config.h"
#include "config_file.h"
#include "config_store.h"
#include "interface.h"
#include "macros.h"
#include "i18n.h"
#include "config.h"

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#define CONFIGURATION_FILENAME ".gtktermrc"

static GtConfigStore *store = NULL;
static gchar *store_load_error = NULL;	/* erreur de lecture (contient le chemin), affichée une fois la fenêtre créée */
static gchar *active_config_name = NULL;	/* nom du profil chargé ; « default » par défaut */

GtProfile config;
GtkTermSettings settings;

GtkWidget *Entry;

gint Grise_Degrise(GtkWidget *bouton, gpointer pointeur);
void transport_toggle_cb(GtkWidget *combo, gpointer user_data);
void read_font_button(GtkFontButton *fontButton);
void Hard_default_configuration(void);

static void Select_config(gchar *, void *);
static void Save_config_file(void);
static void load_config(GtkDialog *, gint, GtkTreeSelection *);
static void delete_config(GtkDialog *, gint, GtkTreeSelection *);
static void save_config(GtkDialog *, gint, GtkWidget *);
static void really_save_config(GtkDialog *, gint, gpointer);
static gboolean cursor_block(GtkSwitch *, gboolean, gpointer);
void config_fg_color(GtkWidget *button, gpointer data);
void config_bg_color(GtkWidget *button, gpointer data);
static void scrollback_set(GtkAdjustment *, gpointer);

extern GtkWidget *display;

static const gchar *active_name(void)
{
	return active_config_name != NULL ? active_config_name : "default";
}

void config_file_init(void)
{
	/* $XDG_CONFIG_HOME/.gtktermrc ; ancien $HOME/.gtktermrc migré (config_file_prepare) */
	gchar *path = config_file_prepare(g_get_user_config_dir(), g_get_home_dir());
	GError *error = NULL;

	store = gt_config_store_load(path, &error);
	/* Appelé avant bindtextdomain : le message traduit est composé par Check_configuration_file */
	if(error != NULL)
	{
		store_load_error = g_strdup(error->message);
		g_error_free(error);
	}
	g_free(path);
}

GtConfigStore *config_get_store(void)
{
	return store;
}

void config_reload_store(void)
{
	GError *error = NULL;

	if(!gt_config_store_reload_if_changed(store, &error))
	{
		/* Fichier devenu illisible : magasin en lecture seule ; l'enregistrement qui suit le signale (boîte ou g_printerr) */
		g_printerr("%s\n", error->message);
		g_error_free(error);
	}
}

gboolean config_save_store(gboolean interactive)
{
	GError *error = NULL;

	if(gt_config_store_save(store, &error))
		return TRUE;
	if(interactive)
		show_message(error->message, MSG_ERR);
	else
		g_printerr("%s\n", error->message);
	g_error_free(error);
	return FALSE;
}

static void settings_load(void)
{
	g_free(settings.update_url);
	settings.update_url = gt_config_store_get_general_string(store, "update_url", GTKTERM_DEFAULT_UPDATE_URL);
	g_free(settings.update_prefix);
	settings.update_prefix = gt_config_store_get_general_string(store, "update_prefix", GTKTERM_DEFAULT_UPDATE_PREFIX);
	settings.update_check_startup = gt_config_store_get_general_boolean(store, "update_check_startup", TRUE);
}

void config_save_update_url(const gchar *url)
{
	/* URL inchangée (boîte À propos seulement fermée) : rien à écrire, pas de message en lecture seule */
	if(g_strcmp0(url, settings.update_url) == 0)
		return;
	gt_profile_set_string(&settings.update_url, url);
	/* Seule la clé modifiée est écrite : update_prefix et update_check_startup ne se règlent pas dans l'interface, les réécrire
	 * depuis la mémoire effacerait une modification faite entre-temps (autre instance, à la main) */
	config_reload_store();
	gt_config_store_set_general_string(store, "update_url", settings.update_url);
	config_save_store(TRUE);
}

void config_remember_macros_file(void)
{
	const gchar *path = macros_file_get_path();

	/* Fichier de macros inchangé (perte de focus d'un argument, même fichier rechoisi) : rien à écrire */
	if(g_strcmp0(path, config.macros_file) == 0)
		return;
	gt_profile_set_string(&config.macros_file, path);
	config_reload_store();
	/* Profil actif supprimé entre-temps : pas de profil fantôme réduit à macros_file, mémoire seulement */
	if(!gt_config_store_has_profile(store, active_name()))
		return;
	gt_config_store_set_profile_string(store, active_name(), "macros_file", config.macros_file);
	config_save_store(TRUE);
}

void save_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean script_visible)
{
	GKeyFile *kf;
	gint x, y, w, h;

	/* Relire avant d'écrire : un profil enregistré par une autre instance depuis le démarrage n'est pas écrasé */
	config_reload_store();
	kf = gt_config_store_get_key_file(store);
	gtk_window_get_position(GTK_WINDOW(window), &x, &y);
	gtk_window_get_size(GTK_WINDOW(window), &w, &h);
	g_key_file_set_integer(kf, "window", "x", x);
	g_key_file_set_integer(kf, "window", "y", y);
	g_key_file_set_integer(kf, "window", "width", w);
	g_key_file_set_integer(kf, "window", "height", h);
	g_key_file_set_integer(kf, "window", "paned_position", gtk_paned_get_position(GTK_PANED(paned)));
	g_key_file_set_boolean(kf, "window", "script_panel_visible", script_visible);
	if(script_paned)
		g_key_file_set_integer(kf, "window", "v_paned_position", gtk_paned_get_position(GTK_PANED(script_paned)));
	/* Magasin en lecture seule (fichier illisible) : rien à écrire, l'erreur a déjà été montrée au démarrage */
	if(!gt_config_store_is_read_only(store))
		config_save_store(FALSE);
}

void load_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean *script_visible_out)
{
	GKeyFile *kf = gt_config_store_get_key_file(store);

	gint x    = g_key_file_get_integer(kf, "window", "x",              NULL);
	gint y    = g_key_file_get_integer(kf, "window", "y",              NULL);
	gint w    = g_key_file_get_integer(kf, "window", "width",          NULL);
	gint h    = g_key_file_get_integer(kf, "window", "height",         NULL);
	gint pos  = g_key_file_get_integer(kf, "window", "paned_position", NULL);
	gint spos = g_key_file_get_integer(kf, "window", "v_paned_position", NULL);

	if (script_visible_out)
		*script_visible_out = g_key_file_get_boolean(kf, "window", "script_panel_visible", NULL);

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
	Set_local_echo(config.echo);
	Set_crlfauto(config.crlf_auto);
	Set_autoreconnect_enabled(config.auto_reconnect);
	Set_esc_clear_screen(config.esc_clear_screen);
	Set_timestamp(config.timestamp);
}

void configure_echo(gboolean echo)
{
	config.echo = echo;
}

void configure_crlfauto(gboolean crlfauto)
{
	config.crlf_auto = crlfauto;
}

void configure_autoreconnect_enable(gboolean autoreconnect)
{
	config.auto_reconnect = autoreconnect;
}

void configure_esc_clear_screen(gboolean esc_clear_screen)
{
	config.esc_clear_screen = esc_clear_screen;
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

/*
 * Liens de /dev vers un port deja trouve (ex. /dev/ttyM5 -> ttyACM0, pose par une
 * regle udev) : aucun motif de /proc/tty/drivers ne les voit. Seuls les alias de
 * ports reels sont gardes, ce qui ecarte /dev/stdin, /dev/fd et consorts.
 */
static void find_serial_port_links(GPtrArray *ports)
{
	DIR *dir = opendir("/dev");
	struct dirent *entry;
	guint nb_ports = ports->len;

	if (dir == NULL)
		return;

	while ((entry = readdir(dir)) != NULL)
	{
		gchar *link;
		char *target;
		gboolean found = FALSE;
		guint i;

		if (entry->d_type != DT_LNK)
			continue;

		link = g_build_filename("/dev", entry->d_name, NULL);
		target = realpath(link, NULL);
		for (i = 0; target != NULL && i < nb_ports && !found; i++)
			found = (strcmp(target, g_ptr_array_index(ports, i)) == 0);

		if (found)
			g_ptr_array_add(ports, link);
		else
			g_free(link);
		free(target);
	}
	closedir(dir);
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
		globfree(&gl);
	}

	find_serial_port_links(ports);
	g_ptr_array_sort_values(ports, compare_seminum);
	return ports;
}

/* Les liens udev (/dev/ttyM5_1...) nomment l'appareil : en gras dans la liste, pour les distinguer des ttyACMx. */
#define PORT_ID_LINK	"lien"

static void port_cell_weight(GtkCellLayout *layout, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gchar *id = NULL;

	gtk_tree_model_get(model, iter, gtk_combo_box_get_id_column(GTK_COMBO_BOX(layout)), &id, -1);
	g_object_set(cell, "weight", (g_strcmp0(id, PORT_ID_LINK) == 0) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, NULL);
	g_free(id);
}

static void port_combo_bold_links(GtkWidget *combo)
{
	GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(combo));

	for (GList *c = cells; c != NULL; c = c->next)
		gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(combo), c->data, port_cell_weight, NULL, NULL);
	g_list_free(cells);
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

	static GtkWidget *port_dialog = NULL;	/* dialogue ouvert, remis à NULL à sa destruction (pointeur faible) */
	GtkWidget **Combos;
	GtkAdjustment *adj;
	gchar *string;
	char *prev;
	int i;
	const struct device_path *device_paths;
	GPtrArray *ports;
	int speed_index;

	/* Un seul dialogue Port à la fois (C12) : une seconde demande le ramène au premier plan au lieu d'en créer un autre */
	if(port_dialog != NULL)
	{
		gtk_window_present(GTK_WINDOW(port_dialog));
		return;
	}

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
	port_dialog = Dialogue;
	g_object_add_weak_pointer(G_OBJECT(Dialogue), (gpointer *)&port_dialog);
	gtk_window_set_modal(GTK_WINDOW(Dialogue), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(Dialogue), GTK_WINDOW(Fenetre));

	/* Widgets lus par Lis_Config : tableau propre à ce dialogue et libéré avec lui, il ne peut plus être relu après sa destruction */
	Combos = g_new0(GtkWidget *, 13);
	g_object_set_data_full(G_OBJECT(Dialogue), "combos", Combos, g_free);

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
	gtk_combo_box_set_active(GTK_COMBO_BOX(TransportCombo), config.transport);
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
	if(config.host != NULL && config.host[0] != '\0')
		gtk_entry_set_text(GTK_ENTRY(host_entry), config.host);
	else
		gtk_entry_set_text(GTK_ENTRY(host_entry), "localhost");
	gtk_grid_attach(GTK_GRID(Table), host_entry, 1, 0, 1, 1);
	Combos[11] = host_entry;

	GtkWidget *port_entry = gtk_entry_new();
	gtk_widget_set_hexpand(port_entry, TRUE);
	gtk_entry_set_input_purpose(GTK_ENTRY(port_entry), GTK_INPUT_PURPOSE_DIGITS);
	if(config.tcp_port != NULL && config.tcp_port[0] != '\0')
		gtk_entry_set_text(GTK_ENTRY(port_entry), config.tcp_port);
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
	port_combo_bold_links(Combo);

	prev = "";
	for (i = 0; i < ports->len; i++)
	{
		char *curr = ports->pdata[i];
		struct stat st;
		if (strcmp(prev, curr))
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(Combo), (lstat(curr, &st) == 0 && S_ISLNK(st.st_mode)) ? PORT_ID_LINK : NULL, curr);
		prev = curr;
	}
	for (i = 0; i < ports->len; i++)
		g_free(ports->pdata[i]);
	g_ptr_array_free(ports, TRUE);

	if(config.port != NULL && config.port[0] != '\0')
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
	if (!config.baudrate)
		config.baudrate = 115200;

	for(i = 0; i < baudrate_count; i++)
	{
		if (baudrate_list[i].baud == config.baudrate)
			speed_index = i;

		string = g_strdup_printf("%u", baudrate_list[i].baud);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(Combo), string);
		g_free(string);
	}

	if (speed_index < 0)
	{
		string = g_strdup_printf("%u", config.baudrate);
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

	switch(config.parity)
	{
	case GT_PARITY_NONE:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);
		break;
	case GT_PARITY_ODD:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 1);
		break;
	case GT_PARITY_EVEN:
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

	if(config.stopbits == 1 || config.stopbits == 2)
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), config.stopbits - 1);
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

	switch(config.flow)
	{
	case GT_FLOW_NONE:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 0);
		break;
	case GT_FLOW_XONXOFF:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 2);
		break;
	case GT_FLOW_RTSCTS:
		gtk_combo_box_set_active(GTK_COMBO_BOX(Combo), 1);
		break;
	case GT_FLOW_RS485:
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
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Spin), (gfloat)config.wait_delay_ms);
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

	if(config.wait_char != -1)
	{
		gchar car_str[2] = {(gchar)config.wait_char, '\0'};
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
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Spin), (gfloat)config.rs485_before_ms);
	gtk_widget_set_hexpand(Spin, TRUE);
	gtk_widget_set_vexpand(Spin, TRUE);
	gtk_grid_attach(GTK_GRID(Table), Spin, 1, 0, 1, 1);
	Combos[8] = Spin;

	adj = gtk_adjustment_new(0.0, 0.0, 500.0, 10.0, 20.0, 0.0);
	Spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(Spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Spin), (gfloat)config.rs485_after_ms);
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

	config.transport = gtk_combo_box_get_active(GTK_COMBO_BOX(Combos[10]));

	if(config.transport == TRANSPORT_SERIAL)
	{
		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[0]));
		gt_profile_set_string(&config.port, message);
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[1]));
		config.baudrate = atoi(message);
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[3]));
		config.bits = atoi(message);
		g_free(message);

		config.wait_delay_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(Combos[6]));
		config.rs485_before_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(Combos[8]));
		config.rs485_after_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(Combos[9]));

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[2]));
		if(!strcmp(message, "odd"))
			config.parity = GT_PARITY_ODD;
		else if(!strcmp(message, "even"))
			config.parity = GT_PARITY_EVEN;
		else
			config.parity = GT_PARITY_NONE;
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[4]));
		config.stopbits = atoi(message);
		g_free(message);

		message = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(Combos[5]));
		if(!strcmp(message, "Xon/Xoff"))
			config.flow = GT_FLOW_XONXOFF;
		else if(!strcmp(message, "RTS/CTS"))
			config.flow = GT_FLOW_RTSCTS;
		else if(!strncmp(message, "RS485",5))
			config.flow = GT_FLOW_RS485;
		else
			config.flow = GT_FLOW_NONE;
		g_free(message);

		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(Combos[7])))
		{
			config.wait_char = (guchar)*gtk_entry_get_text(GTK_ENTRY(Entry));
			config.wait_delay_ms = 0;
		}
		else
			config.wait_char = -1;
	}
	else
	{
		const gchar *host = gtk_entry_get_text(GTK_ENTRY(Combos[11]));
		gt_profile_set_string(&config.host, host);

		const gchar *port = gtk_entry_get_text(GTK_ENTRY(Combos[12]));
		gt_profile_set_string(&config.tcp_port, port);
	}

	interface_open_port();
	ConfigFlags();

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
    vte_terminal_set_scrollback_lines (VTE_TERMINAL(display), config.scrollback);
}

void read_font_button(GtkFontButton *fontButton)
{
	g_free(config.font);
	config.font = g_strdup(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(fontButton)));

	if(config.font != NULL)
	{
		PangoFontDescription *font_desc = pango_font_description_from_string(config.font);
		vte_terminal_set_font(VTE_TERMINAL(display), font_desc);
		pango_font_description_free(font_desc);
	}
}


void select_config_callback(GtkAction *action, gpointer data)
{
	Select_config(_("Load configuration"), G_CALLBACK(load_config));
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
		{
			gchar *dir = g_path_get_dirname (cur);

			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), dir);
			g_free (dir);
		}
		else
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_get_user_config_dir ());
	}

	if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));
		if (macros_load_file (path, TRUE))
			config_remember_macros_file ();
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
		{
			gchar *dir = g_path_get_dirname (cur);

			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), dir);
			g_free (dir);
		}
		else
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_get_user_config_dir ());
	}
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(dialog), "gtkterm_macros.ini");

	if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));
		/* Enregistrement explicite : un échec déjà signalé par un enregistrement implicite est de nouveau affiché */
		macros_forget_save_error ();
		if (macros_save_file (path, TRUE))
			config_remember_macros_file ();
		g_free (path);
	}

	gtk_widget_destroy (dialog);
}

void Select_config(gchar *title, void *callback)
{
	GtkWidget *dialog;
	GtkWidget *content_area;
	gint i;
	gchar **names;

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

	/* Liste à jour : profils enregistrés ou supprimés par une autre instance depuis le démarrage */
	config_reload_store();
	names = gt_config_store_list_profiles(store);
	for(i = 0; names[i] != NULL; i++)
	{
		gtk_list_store_append(Modele_Liste, &iter_Liste);
		gtk_list_store_set(Modele_Liste, &iter_Liste, N_texte, names[i], -1);
	}
	g_strfreev(names);

	gtk_widget_set_size_request(GTK_WIDGET(dialog), 200, 200);

	g_signal_connect(GTK_WIDGET(dialog), "response", G_CALLBACK (callback), GTK_TREE_SELECTION(Selection_Liste));
	g_signal_connect_swapped(GTK_WIDGET(dialog), "response", G_CALLBACK(gtk_widget_destroy), GTK_WIDGET(dialog));

	gtk_box_pack_start (GTK_BOX (content_area), Frame, TRUE, TRUE, 0);

	gtk_widget_show_all (dialog);
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
	const gchar *name = data;

	if(id != GTK_RESPONSE_ACCEPT)
	{
		Save_config_file();
		return;
	}
	/* Le profil enregistré prend le fichier de macros en cours */
	gt_profile_set_string(&config.macros_file, macros_file_get_path());
	config_reload_store();
	gt_config_store_put_profile(store, name, &config);
	/* Le nom actif ne change qu'après réussite de l'enregistrement */
	if(config_save_store(TRUE))
	{
		g_free(active_config_name);
		active_config_name = g_strdup(name);
	}
}

void save_config(GtkDialog *Fenetre, gint id, GtkWidget *edit)
{
	const gchar *config_name;

	if(id == GTK_RESPONSE_ACCEPT)
	{
		config_name = gtk_entry_get_text(GTK_ENTRY(edit));
		/* Nom vide : pas de groupe [profile:] */
		if(config_name[0] == '\0')
		{
			show_message(_("The configuration name cannot be empty."), MSG_ERR);
			return;
		}

		/* Comparaison sans casse : « Default » propose d'écraser « default » (C6) ; fichier relu pour une réponse à jour */
		config_reload_store();
		if(gt_config_store_has_profile(store, config_name))
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
		}
		else
			really_save_config(Fenetre, GTK_RESPONSE_ACCEPT, (gpointer)config_name);
	}
}

void load_config(GtkDialog *Fenetre, gint id, GtkTreeSelection *Selection_Liste)
{
	GtkTreeIter iter;
	GtkTreeModel *Modele;
	gchar *txt;

	if(id == GTK_RESPONSE_ACCEPT)
	{
		if(gtk_tree_selection_get_selected(Selection_Liste, &Modele, &iter))
		{
			gtk_tree_model_get(GTK_TREE_MODEL(Modele), &iter, 0, &txt, -1);
			Load_configuration_from_file(txt);
			Verify_configuration();
			interface_open_port();
			ConfigFlags();
			g_free(txt);
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
			config_reload_store();
			if(!gt_config_store_delete_profile(store, txt))
				show_message(_("Cannot delete section!"), MSG_ERR);
			else
				config_save_store(TRUE);
			g_free(txt);
		}
	}
}

gint Load_configuration_from_file(const gchar *config_name)
{
	GtProfile loaded = { 0 };
	gboolean lock_port = config.lock_port;

	config_reload_store();
	if(!gt_config_store_get_profile(store, config_name, &loaded))
	{
		gchar *string = g_strdup_printf(_("No section \"%s\" in configuration file\n"), config_name);

		show_message(string, MSG_ERR);
		g_free(string);
		return -1;
	}

	gt_profile_copy(&config, &loaded);
	gt_profile_clear(&loaded);
	/* Réglage de session jamais enregistré : --disable-port-lock survit au chargement d'un profil */
	config.lock_port = lock_port;
	/* Le nom actif n'est retenu qu'une fois le profil trouvé : un nom inconnu ne crée pas de profil à l'enregistrement (C13) */
	g_free(active_config_name);
	active_config_name = g_strdup(config_name);

	/* Fichier de macros du profil ; sans fichier, celui en cours est relu (non interactif : avertissement seulement) */
	macros_load_file(config.macros_file, FALSE);

	/* Pas d'application au terminal ici : les valeurs ne sont pas encore validées ; Verify_configuration valide puis applique */
	return 0;
}

/* Valide le profil en cours (avertissements dans une seule boîte) puis l'applique au terminal : jamais de valeur invalide dans VTE */
void Verify_configuration(void)
{
	GPtrArray *warnings = g_ptr_array_new_with_free_func(g_free);

	if(!gt_profile_validate(&config, warnings))
	{
		gchar *text;

		g_ptr_array_add(warnings, NULL);
		text = g_strjoinv("\n", (gchar **)warnings->pdata);
		show_message(text, MSG_ERR);
		g_free(text);
	}
	g_ptr_array_unref(warnings);
	config_apply_to_terminal();
}

gint Check_configuration_file(void)
{
	gchar **names;
	gboolean no_profile;

	Hard_default_configuration();
	settings_load();

	/* Fichier illisible : défauts, magasin en lecture seule (rien ne sera écrit) */
	if(store_load_error != NULL)
	{
		/* Le message de lecture contient déjà le chemin du fichier */
		gchar *string = g_strdup_printf(_("Cannot read configuration file: %s\nThe default configuration is used and nothing will be written to this file."),
		                                store_load_error);

		show_message(string, MSG_ERR);
		g_free(string);
		g_clear_pointer(&store_load_error, g_free);
		return -1;
	}

	/* Fichier absent, vide ou sans aucun profil (seulement [window]…) : premier lancement, [profile:default] créé avec les défauts */
	names = gt_config_store_list_profiles(store);
	no_profile = (names[0] == NULL);
	g_strfreev(names);
	if(no_profile)
	{
		gchar *string = g_strdup_printf(_("Configuration file (%s) with\n[default] configuration has been created.\n"), gt_config_store_get_path(store));

		gt_config_store_put_profile(store, "default", &config);
		if(config_save_store(TRUE))
			show_message(string, MSG_WRN);
		g_free(string);
		return 0;
	}

	return Load_configuration_from_file("default") == 0 ? 0 : -1;
}

void Hard_default_configuration(void)
{
	gt_profile_clear(&config);
	gt_profile_init_defaults(&config);
	gt_profile_set_string(&settings.update_url, GTKTERM_DEFAULT_UPDATE_URL);
	gt_profile_set_string(&settings.update_prefix, GTKTERM_DEFAULT_UPDATE_PREFIX);
	settings.update_check_startup = TRUE;
	config_apply_to_terminal();
}

void Config_Terminal(GtkAction *action, gpointer data)
{
	GtkBuilder *builder;
	GdkRGBA rgba;
	builder = gtk_builder_new_from_resource("/org/gtk/gtkterm/config_terminal_dialog.ui");

	GtkWidget *dialog;
	dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(Fenetre));
	gtk_window_set_title(GTK_WINDOW(dialog), _("Main Window"));

	/** Connect signals **/
	// Font Selection Button
	GtkWidget *cfg_terminal_font;
	cfg_terminal_font = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_terminal_font"));
	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(cfg_terminal_font), config.font);
	g_signal_connect(cfg_terminal_font, "font-set", G_CALLBACK(read_font_button), 0);

	// Scrollback Lines
	GtkAdjustment *cfg_scrollback_lines;
	cfg_scrollback_lines = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "cfg_scrollback_lines"));
	gtk_adjustment_set_value(cfg_scrollback_lines, config.scrollback);
	g_signal_connect(G_OBJECT(cfg_scrollback_lines), "value_changed", G_CALLBACK(scrollback_set), 0);

	// Show cursor
	GtkWidget *cfg_block_cursor;
	cfg_block_cursor = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_block_cursor"));
	gtk_switch_set_active(GTK_SWITCH(cfg_block_cursor), config.block_cursor);
	g_signal_connect(cfg_block_cursor, "state_set", G_CALLBACK(cursor_block), 0);

	// Text color
	GtkWidget *cfg_text_color;
	cfg_text_color = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_text_color"));
	config_color_to_rgba(&config.foreground, &rgba);
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cfg_text_color), &rgba);
	g_signal_connect(cfg_text_color, "color-set", G_CALLBACK (config_fg_color), 0);

	// Background color
	GtkWidget *cfg_background_color;
	cfg_background_color = GTK_WIDGET(gtk_builder_get_object(builder, "cfg_background_color"));
	config_color_to_rgba(&config.background, &rgba);
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cfg_background_color), &rgba);
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
	config.block_cursor = state;
	vte_terminal_set_cursor_shape(VTE_TERMINAL(display), config.block_cursor ? VTE_CURSOR_SHAPE_BLOCK : VTE_CURSOR_SHAPE_IBEAM);
	return FALSE;
}

void config_color_to_rgba(const GtColor *c, GdkRGBA *out)
{
	out->red = c->red;
	out->green = c->green;
	out->blue = c->blue;
	out->alpha = c->alpha;
}

static void config_color_from_rgba(GtColor *c, const GdkRGBA *in)
{
	c->red = in->red;
	c->green = in->green;
	c->blue = in->blue;
	c->alpha = in->alpha;
}

void config_apply_to_terminal(void)
{
	PangoFontDescription *font_desc;
	GdkRGBA fg, bg;

	if(display == NULL)
		return;
	font_desc = pango_font_description_from_string(config.font != NULL ? config.font : GT_PROFILE_DEFAULT_FONT);
	vte_terminal_set_font(VTE_TERMINAL(display), font_desc);
	pango_font_description_free(font_desc);
	vte_terminal_set_size(VTE_TERMINAL(display), config.columns, config.rows);
	vte_terminal_set_scrollback_lines(VTE_TERMINAL(display), config.scrollback);
	config_color_to_rgba(&config.foreground, &fg);
	config_color_to_rgba(&config.background, &bg);
	vte_terminal_set_color_foreground(VTE_TERMINAL(display), &fg);
	vte_terminal_set_color_background(VTE_TERMINAL(display), &bg);
	vte_terminal_set_cursor_shape(VTE_TERMINAL(display), config.block_cursor ? VTE_CURSOR_SHAPE_BLOCK : VTE_CURSOR_SHAPE_IBEAM);
	gtk_widget_queue_draw(display);
}

void config_fg_color(GtkWidget *button, gpointer data)
{
	GdkRGBA rgba;

	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
	config_color_from_rgba(&config.foreground, &rgba);
	vte_terminal_set_color_foreground(VTE_TERMINAL(display), &rgba);
	gtk_widget_queue_draw(display);
}

void config_bg_color(GtkWidget *button, gpointer data)
{
	GdkRGBA rgba;

	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
	config_color_from_rgba(&config.background, &rgba);
	vte_terminal_set_color_background(VTE_TERMINAL(display), &rgba);
	gtk_widget_queue_draw(display);
}

void scrollback_set(GtkAdjustment *Adjustment, gpointer data)
{
	gint scrollback = gtk_adjustment_get_value(Adjustment);
	config.scrollback = scrollback;
	vte_terminal_set_scrollback_lines (VTE_TERMINAL(display), config.scrollback);
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
