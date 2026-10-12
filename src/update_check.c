/* Lancement du script de mise à jour : voir update_check.h. */

#include <glib.h>
#include <string.h>

#include "update_check.h"
#include "update_versions.h"

/* Script fixe passé à « sh -c » : les valeurs arrivent en $0…$4, jamais dans la chaîne (M34). « read _ » : sous dash, « read » sans nom
 * échoue (« arg count ») et le terminal se fermait avant que l'on puisse lire la sortie. */
#define GT_UPDATE_INNER_SCRIPT "sh \"$0\" \"$1\" \"$2\" \"$3\" \"$4\"; echo; echo '>>> Appuyez sur Entrée pour fermer...'; read _"

/* ---- terminal visible pour le script ------------------------------- */

typedef struct
{
	const gchar *name;
	const gchar *const *options;	/* police, zoom… ; NULL : aucune */
	const gchar *exec_flag;	/* introduit la commande et ses arguments ; NULL : la commande suit les options (kitty) */
} TerminalDef;

static const gchar *const xterm_options[] = {"-fa", "Monospace", "-fs", "12", NULL};
static const gchar *const gnome_terminal_options[] = {"--zoom=1.5", NULL};
static const gchar *const xfce4_terminal_options[] = {"--font", "Monospace 12", NULL};
static const gchar *const urxvt_options[] = {"-fn", "xft:Monospace:size=12", NULL};
static const gchar *const kitty_options[] = {"-o", "font_size=14", NULL};

/* Ordre de recherche d'aujourd'hui. Chaque drapeau prend le reste de la ligne comme programme et arguments (xfce4-terminal : -x, son -e
 * prend une seule chaîne). */
static const TerminalDef terminals[] = {
	{"xterm", xterm_options, "-e"},
	{"x-terminal-emulator", NULL, "-e"},
	{"gnome-terminal", gnome_terminal_options, "--"},
	{"xfce4-terminal", xfce4_terminal_options, "-x"},
	{"konsole", NULL, "-e"},
	{"lxterminal", NULL, "-e"},
	{"qterminal", NULL, "-e"},
	{"urxvt", urxvt_options, "-e"},
	{"kitty", kitty_options, NULL},
};

static const TerminalDef *
find_def(const gchar *name)
{
	for(gsize i = 0; i < G_N_ELEMENTS(terminals); i++)
	{
		if(g_strcmp0(terminals[i].name, name) == 0)
			return &terminals[i];
	}
	return NULL;
}

gchar *
gt_update_find_terminal(void)
{
	for(gsize i = 0; i < G_N_ELEMENTS(terminals); i++)
	{
		gchar *path = g_find_program_in_path(terminals[i].name);

		if(path != NULL)
		{
			g_free(path);
			return g_strdup(terminals[i].name);
		}
	}
	return NULL;
}

gchar **
gt_update_terminal_argv(const gchar *terminal, const gchar *script, const gchar *url, const gchar *prefix, GPid pid, const gchar *tag)
{
	const TerminalDef *def;
	GPtrArray *argv;
	gchar *base;

	if(terminal == NULL || script == NULL || url == NULL || prefix == NULL || !gt_version_is_release_tag(tag))
		return NULL;
	base = g_path_get_basename(terminal);
	def = find_def(base);
	g_free(base);
	if(def == NULL)
		return NULL;
	argv = g_ptr_array_new();
	g_ptr_array_add(argv, g_strdup(terminal));
	for(const gchar *const *o = def->options; o != NULL && *o != NULL; o++)
		g_ptr_array_add(argv, g_strdup(*o));
	if(def->exec_flag != NULL)
		g_ptr_array_add(argv, g_strdup(def->exec_flag));
	g_ptr_array_add(argv, g_strdup("sh"));
	g_ptr_array_add(argv, g_strdup("-c"));
	g_ptr_array_add(argv, g_strdup(GT_UPDATE_INNER_SCRIPT));
	g_ptr_array_add(argv, g_strdup(script));
	g_ptr_array_add(argv, g_strdup(url));
	g_ptr_array_add(argv, g_strdup(prefix));
	g_ptr_array_add(argv, g_strdup_printf("%d", (gint)pid));
	g_ptr_array_add(argv, g_strdup(tag));
	g_ptr_array_add(argv, NULL);
	return (gchar **)g_ptr_array_free(argv, FALSE);
}
