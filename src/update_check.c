/* Vérification de mise à jour (sous-projet 6a) : voir update_check.h. Extrait de l'ancien update.c (GTK 3). */

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <string.h>

#include "update_check.h"

/* Script fixe passé à « sh -c » : les valeurs arrivent en $0…$3, jamais dans la chaîne (M34). « read _ » : sous dash, « read » sans nom
 * échoue (« arg count ») et le terminal se fermait avant que l'on puisse lire la sortie. */
#define GT_UPDATE_INNER_SCRIPT "sh \"$0\" \"$1\" \"$2\" \"$3\"; echo; echo '>>> Appuyez sur Entrée pour fermer...'; read _"

static gboolean
is_hex(const gchar *s, gsize min, gsize max)
{
	gsize n;

	if(s == NULL)
		return FALSE;
	n = strlen(s);
	if(n < min || n > max)
		return FALSE;
	for(gsize i = 0; i < n; i++)
	{
		if(!g_ascii_isxdigit(s[i]))
			return FALSE;
	}
	return TRUE;
}

/* Révision utilisable (abrégée acceptée) : 7 à 64 chiffres hexadécimaux ; « unknown » et toute option déguisée sont refusés */
static gboolean
is_revision(const gchar *s)
{
	return is_hex(s, 7, 64);
}

static gboolean
same_revision(const gchar *a, const gchar *b)
{
	return a != NULL && b != NULL && *a != '\0' && g_ascii_strcasecmp(a, b) == 0;
}

GtUpdateVerdict
gt_update_compare(const gchar *local_rev, const gchar *remote_rev, const gchar *skipped_rev, gboolean local_is_newer)
{
	if(!is_revision(local_rev))
		return GT_UPDATE_LOCAL_UNKNOWN;
	if(!is_revision(remote_rev))
		return GT_UPDATE_UP_TO_DATE;
	if(same_revision(local_rev, remote_rev))
		return GT_UPDATE_UP_TO_DATE;
	/* M33 : « plus récente » = différente ET pas plus ancienne que la révision construite */
	if(local_is_newer)
		return GT_UPDATE_UP_TO_DATE;
	if(same_revision(remote_rev, skipped_rev))
		return GT_UPDATE_SKIPPED;
	return GT_UPDATE_AVAILABLE;
}

/* ---- git ls-remote asynchrone ------------------------------------- */

static gchar *
first_line(const gchar *text)
{
	gchar **lines;
	gchar *line = NULL;

	if(text == NULL)
		return NULL;
	lines = g_strsplit(text, "\n", -1);
	for(gchar **l = lines; *l != NULL && line == NULL; l++)
	{
		g_strstrip(*l);
		if(**l != '\0')
			line = g_strdup(*l);
	}
	g_strfreev(lines);
	return line;
}

/* « <sha>\tHEAD\n » → sha complet (SHA-1 ou SHA-256) en minuscules ; NULL sinon */
static gchar *
parse_head(const gchar *out)
{
	gchar **fields;
	gchar *rev = NULL;

	if(out == NULL)
		return NULL;
	fields = g_strsplit_set(out, " \t\r\n", 2);
	if(fields[0] != NULL && (is_hex(fields[0], 40, 40) || is_hex(fields[0], 64, 64)))
		rev = g_ascii_strdown(fields[0], -1);
	g_strfreev(fields);
	return rev;
}

static void
on_ls_remote_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	GSubprocess *proc = G_SUBPROCESS(source);
	gchar *out = NULL;
	gchar *err_text = NULL;
	GError *error = NULL;

	if(!g_subprocess_communicate_utf8_finish(proc, res, &out, &err_text, &error))
	{
		/* Annulation (fenêtre fermée) : git peut encore attendre le réseau, il ne survit pas à la vérification */
		g_subprocess_force_exit(proc);
		g_task_return_error(task, error);
	}
	else if(!g_subprocess_get_successful(proc))
	{
		gchar *line = first_line(err_text);

		if(line != NULL)
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", line);
		else
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, _("git ls-remote failed (exit status %d)"), g_subprocess_get_exit_status(proc));
		g_free(line);
	}
	else
	{
		gchar *rev = parse_head(out);

		if(rev != NULL)
			g_task_return_pointer(task, rev, g_free);
		else
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The remote repository did not report a HEAD revision."));
	}
	g_free(out);
	g_free(err_text);
	g_object_unref(task);
}

void
gt_update_fetch_remote_async(const gchar *url, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data)
{
	GTask *task = g_task_new(NULL, cancellable, cb, user_data);
	GSubprocessLauncher *launcher;
	GSubprocess *proc;
	GError *error = NULL;

	g_task_set_source_tag(task, gt_update_fetch_remote_async);
	if(url == NULL || *url == '\0')
	{
		/* GTask diffère le rappel à l'itération suivante : jamais d'appel pendant cette fonction */
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("No update source URL."));
		g_object_unref(task);
		return;
	}
	/* « -- » : l'URL n'est jamais prise pour une option ; protocol.ext.allow=never : une URL « ext:: » ne lance aucune commande ;
	 * GIT_TERMINAL_PROMPT=0 : un dépôt qui demande une authentification échoue au lieu d'attendre une saisie invisible (M34) ;
	 * LC_ALL=C : message d'erreur en ASCII (communicate_utf8). */
	const gchar *const argv[] = {"git", "-c", "protocol.ext.allow=never", "ls-remote", "--", url, "HEAD", NULL};

	launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_setenv(launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);
	g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
	proc = g_subprocess_launcher_spawnv(launcher, argv, &error);
	g_object_unref(launcher);
	if(proc == NULL)
	{
		g_task_return_error(task, error);
		g_object_unref(task);
		return;
	}
	/* Entrée fermée aussitôt (stdin_buf NULL) : git ne lit jamais l'entrée de GTKTerm */
	g_subprocess_communicate_utf8_async(proc, NULL, cancellable, on_ls_remote_done, task);
	g_object_unref(proc);
}

gchar *
gt_update_fetch_remote_finish(GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(g_task_is_valid(res, NULL), NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

/* ---- révision locale plus récente (M33) ---------------------------- */

gboolean
gt_update_local_is_newer(const gchar *cache_src_dir, const gchar *local_rev, const gchar *remote_rev)
{
	gchar *gitdir;
	gboolean present;
	gint status = 0;

	if(cache_src_dir == NULL || !is_revision(local_rev) || !is_revision(remote_rev))
		return FALSE;
	gitdir = g_build_filename(cache_src_dir, ".git", NULL);
	present = g_file_test(gitdir, G_FILE_TEST_EXISTS);
	g_free(gitdir);
	if(!present)
		return FALSE;
	/* Révisions vérifiées (chiffres hexadécimaux seulement) : aucune ne peut passer pour une option de git */
	const gchar *const argv[] = {"git", "-C", cache_src_dir, "merge-base", "--is-ancestor", remote_rev, local_rev, NULL};

	if(!g_spawn_sync(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
	                 NULL, NULL, NULL, NULL, &status, NULL))
		return FALSE;
	/* 0 : remote est un ancêtre de local ; 1 : non ; 128 : révision inconnue du dépôt cache */
	return g_spawn_check_wait_status(status, NULL);
}

gchar *
gt_update_cache_src_dir(void)
{
	return g_build_filename(g_get_user_cache_dir(), "gtkterm", "src", NULL);
}

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
gt_update_terminal_argv(const gchar *terminal, const gchar *script, const gchar *url, const gchar *prefix, GPid pid)
{
	const TerminalDef *def;
	GPtrArray *argv;
	gchar *base;

	if(terminal == NULL || script == NULL || url == NULL || prefix == NULL)
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
	g_ptr_array_add(argv, NULL);
	return (gchar **)g_ptr_array_free(argv, FALSE);
}
