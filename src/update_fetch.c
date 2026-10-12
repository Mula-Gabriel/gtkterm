/* Versions publiées et notes de version d'une source de mise à jour : voir update_fetch.h. */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "update_fetch.h"
#include "update_versions.h"

typedef enum
{
	STEP_CLONE,	/* cache sans HEAD : clone nu filtré */
	STEP_SET_URL,	/* cache présent : l'URL a pu changer */
	STEP_FETCH,	/* tags de la source, tags disparus supprimés */
	STEP_LIST,	/* for-each-ref refs/tags */
	STEP_NEWS	/* show <tag le plus récent>:NEWS */
} Step;

typedef struct
{
	gchar *url;
	gchar *cache_dir;
	Step step;
	GtReleases *releases;	/* résultat en construction */
} FetchState;

void
gt_releases_free(GtReleases *r)
{
	if(r == NULL)
		return;
	if(r->tags != NULL)
		g_ptr_array_unref(r->tags);
	g_free(r->news);
	g_free(r);
}

static void
fetch_state_free(gpointer p)
{
	FetchState *st = p;

	g_free(st->url);
	g_free(st->cache_dir);
	gt_releases_free(st->releases);
	g_free(st);
}

gchar *
gt_releases_cache_dir(void)
{
	return g_build_filename(g_get_user_cache_dir(), "gtkterm", "versions.git", NULL);
}

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

static void on_step_done(GObject *source, GAsyncResult *res, gpointer user_data);

static void
argv_add(GPtrArray *argv, const gchar *arg)
{
	g_ptr_array_add(argv, g_strdup(arg));
}

/* Lance l'étape courante ; la référence de la tâche passe au rappel */
static void
run_step(GTask *task)
{
	FetchState *st = g_task_get_task_data(task);
	GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
	GSubprocessLauncher *launcher;
	GSubprocess *proc;
	GError *error = NULL;

	argv_add(argv, "git");
	/* protocol.ext.allow=never : une URL « ext:: » ne lance aucune commande */
	argv_add(argv, "-c");
	argv_add(argv, "protocol.ext.allow=never");
	if(st->step != STEP_CLONE)
	{
		/* --git-dir et non -C : un dossier qui n'est pas un dépôt échoue au lieu d'agir sur le dépôt parent */
		g_ptr_array_add(argv, g_strconcat("--git-dir=", st->cache_dir, NULL));
	}
	switch(st->step)
	{
		case STEP_CLONE:
			/* « -- » : l'URL n'est jamais prise pour une option ; tree:0 : historique et tags, sans arbres ni fichiers */
			argv_add(argv, "clone");
			argv_add(argv, "--bare");
			argv_add(argv, "--quiet");
			argv_add(argv, "--filter=tree:0");
			argv_add(argv, "--");
			argv_add(argv, st->url);
			argv_add(argv, st->cache_dir);
			break;
		case STEP_SET_URL:
			argv_add(argv, "remote");
			argv_add(argv, "set-url");
			argv_add(argv, "origin");
			argv_add(argv, "--");
			argv_add(argv, st->url);
			break;
		case STEP_FETCH:
			argv_add(argv, "fetch");
			argv_add(argv, "--quiet");
			argv_add(argv, "--prune");
			argv_add(argv, "--force");
			argv_add(argv, "origin");
			argv_add(argv, "+refs/tags/*:refs/tags/*");
			break;
		case STEP_LIST:
			argv_add(argv, "for-each-ref");
			argv_add(argv, "--format=%(refname:strip=2)");
			argv_add(argv, "refs/tags");
			break;
		case STEP_NEWS:
			/* Tag validé par gt_version_list_from_refs : « vX.Y.Z » seulement */
			argv_add(argv, "show");
			g_ptr_array_add(argv, g_strdup_printf("refs/tags/%s:NEWS", (const gchar *)g_ptr_array_index(st->releases->tags, 0)));
			break;
	}
	g_ptr_array_add(argv, NULL);
	/* GIT_TERMINAL_PROMPT=0 : un dépôt qui demande une authentification échoue au lieu d'attendre une saisie invisible ; LC_ALL=C : message
	 * d'erreur en ASCII (communicate_utf8) */
	launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_setenv(launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);
	g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
	proc = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv->pdata, &error);
	g_object_unref(launcher);
	g_ptr_array_unref(argv);
	if(proc == NULL)
	{
		g_task_return_error(task, error);
		g_object_unref(task);
		return;
	}
	/* Entrée fermée aussitôt (stdin_buf NULL) : git ne lit jamais l'entrée de GTKTerm */
	g_subprocess_communicate_utf8_async(proc, NULL, g_task_get_cancellable(task), on_step_done, task);
	g_object_unref(proc);
}

static void
return_releases(GTask *task, FetchState *st)
{
	GtReleases *r = st->releases;

	st->releases = NULL;
	g_task_return_pointer(task, r, (GDestroyNotify)gt_releases_free);
}

static void
on_step_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	FetchState *st = g_task_get_task_data(task);
	GSubprocess *proc = G_SUBPROCESS(source);
	gchar *out = NULL;
	gchar *err_text = NULL;
	GError *error = NULL;
	gboolean ok = g_subprocess_communicate_utf8_finish(proc, res, &out, &err_text, &error);

	/* Annulation (fenêtre fermée) : git peut encore attendre le réseau, il ne survit pas à l'actualisation */
	if(!ok)
		g_subprocess_force_exit(proc);
	if(g_task_return_error_if_cancelled(task))
		g_clear_error(&error);
	else if(st->step == STEP_NEWS)
	{
		/* NEWS absent ou illisible (pas en UTF-8) : pas une erreur, zone de notes vide */
		if(ok && g_subprocess_get_successful(proc))
		{
			st->releases->news = out;
			out = NULL;
		}
		g_clear_error(&error);
		return_releases(task, st);
	}
	else if(!ok)
		g_task_return_error(task, error);
	else if(!g_subprocess_get_successful(proc))
	{
		gchar *line = first_line(err_text);

		if(line != NULL)
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", line);
		else
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, _("git failed (exit status %d)"), g_subprocess_get_exit_status(proc));
		g_free(line);
	}
	else if(st->step == STEP_LIST)
	{
		st->releases->tags = gt_version_list_from_refs(out);
		if(st->releases->tags->len == 0)
			return_releases(task, st);
		else
		{
			st->step = STEP_NEWS;
			g_free(out);
			g_free(err_text);
			run_step(task);
			return;
		}
	}
	else
	{
		/* CLONE → FETCH (le fetch explicite aligne les tags dans tous les cas), SET_URL → FETCH, FETCH → LIST */
		st->step = st->step == STEP_FETCH ? STEP_LIST : STEP_FETCH;
		g_free(out);
		g_free(err_text);
		run_step(task);
		return;
	}
	g_free(out);
	g_free(err_text);
	g_object_unref(task);
}

void
gt_releases_fetch_async(const gchar *url, const gchar *cache_dir, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data)
{
	GTask *task = g_task_new(NULL, cancellable, cb, user_data);
	FetchState *st;
	gchar *head;

	g_task_set_source_tag(task, gt_releases_fetch_async);
	if(url == NULL || *url == '\0' || cache_dir == NULL)
	{
		/* GTask diffère le rappel à l'itération suivante : jamais d'appel pendant cette fonction */
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("No update source URL."));
		g_object_unref(task);
		return;
	}
	if(*url == '-')
	{
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Invalid update source URL: %s"), url);
		g_object_unref(task);
		return;
	}
	st = g_new0(FetchState, 1);
	st->url = g_strdup(url);
	st->cache_dir = g_strdup(cache_dir);
	st->releases = g_new0(GtReleases, 1);
	g_task_set_task_data(task, st, fetch_state_free);
	head = g_build_filename(cache_dir, "HEAD", NULL);
	st->step = g_file_test(head, G_FILE_TEST_EXISTS) ? STEP_SET_URL : STEP_CLONE;
	g_free(head);
	if(st->step == STEP_CLONE)
	{
		gchar *parent = g_path_get_dirname(cache_dir);

		g_mkdir_with_parents(parent, 0700);
		g_free(parent);
	}
	run_step(task);
}

GtReleases *
gt_releases_fetch_finish(GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(g_task_is_valid(res, NULL), NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}
