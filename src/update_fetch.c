/* Versions publiées et notes de version d'une source de mise à jour : voir update_fetch.h. */

#include <glib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
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
	gboolean existed;	/* le cache existait au départ de cette tentative (pas fraîchement cloné par elle) */
	gint lock_fd;	/* verrou consultatif du cache (<cache>.lock), tenu pendant toute l'opération ; -1 : pas encore pris */
	gint64 lock_deadline;	/* date limite (monotone, µs) de l'attente du verrou */
	gboolean retried;	/* le cache a déjà été écarté une fois : plus de nouvelle tentative */
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

	if(st->lock_fd >= 0)
		close(st->lock_fd);	/* libère le verrou */
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

/* Dossier non vide ? (clone refusé parce que la cible est occupée) */
static gboolean
dir_not_empty(const gchar *path)
{
	GDir *d = g_dir_open(path, 0, NULL);
	gboolean occupe;

	if(d == NULL)
		return g_file_test(path, G_FILE_TEST_EXISTS) && !g_file_test(path, G_FILE_TEST_IS_DIR);
	occupe = g_dir_read_name(d) != NULL;
	g_dir_close(d);
	return occupe;
}

/* Le message d'erreur de git d'un fetch trahit-il un problème du dépôt local (et non du réseau) ? L'URL, que git répète dans ses messages, est
 * retirée avant l'examen ; seules des phrases précises sont reconnues, ligne par ligne. */
static gboolean
erreur_depot_local(const gchar *err_text, const gchar *url, const gchar *cache_dir)
{
	gchar **morceaux;
	gchar *sans_url;
	gchar *bas;
	gchar *cache_bas;
	gchar **lignes;
	gboolean local = FALSE;

	if(err_text == NULL)
		return FALSE;
	morceaux = g_strsplit(err_text, url, -1);
	sans_url = g_strjoinv("", morceaux);
	g_strfreev(morceaux);
	bas = g_ascii_strdown(sans_url, -1);
	cache_bas = g_ascii_strdown(cache_dir, -1);
	lignes = g_strsplit(bas, "\n", -1);
	for(gchar **l = lignes; *l != NULL && !local; l++)
	{
		local = (strstr(*l, "unable to create '") != NULL && strstr(*l, "lock': file exists") != NULL) || strstr(*l, "cannot lock ref") != NULL ||
		        strstr(*l, "not a git repository") != NULL || strstr(*l, "fatal: bad config") != NULL ||
		        (strstr(*l, "is corrupt") != NULL && strstr(*l, cache_bas) != NULL);
	}
	g_strfreev(lignes);
	g_free(cache_bas);
	g_free(bas);
	g_free(sans_url);
	return local;
}

/* Auto-réparation du cache : un cache dont une étape purement LOCALE échoue (set-url, for-each-ref), un fetch dont l'erreur désigne le dépôt local
 * (verrous « .lock » laissés par un git interrompu, configuration de clone partiel perdue…) ou une cible de clone occupée par autre chose qu'un
 * dépôt ferait échouer toutes les actualisations suivantes. Le verrou consultatif du cache étant tenu, tout « .lock » du dépôt est périmé par
 * construction. Rien n'est supprimé : le dossier est renommé à côté (« <cache>.abandonne-<horodatage en µs> »), puis on repart une seule fois
 * d'un clone. Une erreur de communication avec git (annulation exclue) n'écarte jamais le cache. Si le renommage échoue, l'erreur d'origine est rendue. Un échec
 * réseau (DNS, connexion, authentification, source introuvable) n'écarte jamais un cache valide : hors ligne, il reste en place. */
static gboolean
cache_ecarter(FetchState *st, const gchar *err_text)
{
	gboolean candidat;
	gchar *abandon;
	gboolean ok;

	if(st->retried)
		return FALSE;
	switch(st->step)
	{
		case STEP_CLONE:
			candidat = dir_not_empty(st->cache_dir);
			break;
		case STEP_SET_URL:
		case STEP_LIST:
			candidat = st->existed;
			break;
		case STEP_FETCH:
			candidat = st->existed && erreur_depot_local(err_text, st->url, st->cache_dir);
			break;
		default:
			candidat = FALSE;
			break;
	}
	if(!candidat)
		return FALSE;
	abandon = g_strdup_printf("%s.abandonne-%" G_GINT64_FORMAT, st->cache_dir, g_get_real_time());
	ok = g_rename(st->cache_dir, abandon) == 0;
	g_free(abandon);
	if(!ok)
		return FALSE;
	st->retried = TRUE;
	st->existed = FALSE;
	st->step = STEP_CLONE;
	return TRUE;
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
		g_subprocess_send_signal(proc, SIGTERM);	/* SIGTERM et non SIGKILL : git nettoie ses verrous et son clone inachevé */
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
	else if(ok && !g_subprocess_get_successful(proc) && cache_ecarter(st, err_text))
	{
		g_clear_error(&error);
		g_free(out);
		g_free(err_text);
		run_step(task);
		return;
	}
	else if(!ok)
		g_task_return_error(task, error);
	else if(!g_subprocess_get_successful(proc))
	{
		gchar *line = first_line(err_text);

		if(line != NULL)
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", line);
		else if(g_subprocess_get_if_exited(proc))
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, _("git failed (exit status %d)"), g_subprocess_get_exit_status(proc));
		else
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, _("git was killed by signal %d"),
			                        g_subprocess_get_if_signaled(proc) ? g_subprocess_get_term_sig(proc) : 0);
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

/* Délai d'attente du verrou : 10 s ; GTKTERM_UPDATE_LOCK_TIMEOUT_MS (pour les tests seulement), lue à chaque appel, le remplace */
static gint64
lock_timeout_ms(void)
{
	const gchar *v = g_getenv("GTKTERM_UPDATE_LOCK_TIMEOUT_MS");
	gint64 ms = 10000;

	if(v != NULL && *v != '\0')
		ms = g_ascii_strtoll(v, NULL, 10);
	return ms;
}

/* Verrou pris : détermine la première étape (HEAD présent : cache existant) et lance */
static void
demarrer(GTask *task)
{
	FetchState *st = g_task_get_task_data(task);
	gchar *head = g_build_filename(st->cache_dir, "HEAD", NULL);

	st->step = g_file_test(head, G_FILE_TEST_EXISTS) ? STEP_SET_URL : STEP_CLONE;
	g_free(head);
	st->existed = st->step != STEP_CLONE;
	run_step(task);
}

static gboolean attendre_verrou_tic(gpointer data);

/* Prend le verrou du cache sans bloquer la boucle principale : occupé, nouvel essai toutes les 200 ms, jusqu'à la date limite */
static void
attendre_verrou(GTask *task)
{
	FetchState *st = g_task_get_task_data(task);

	if(g_task_return_error_if_cancelled(task))
	{
		g_object_unref(task);
		return;
	}
	if(flock(st->lock_fd, LOCK_EX | LOCK_NB) == 0)
	{
		demarrer(task);
		return;
	}
	if(errno != EWOULDBLOCK && errno != EINTR)
		g_task_return_new_error(task, G_IO_ERROR, g_io_error_from_errno(errno), "flock: %s", g_strerror(errno));
	else if(g_get_monotonic_time() >= st->lock_deadline)
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_BUSY, _("Another update check is in progress."));
	else
	{
		g_timeout_add(200, attendre_verrou_tic, task);	/* la référence de la tâche passe au tic */
		return;
	}
	g_object_unref(task);
}

static gboolean
attendre_verrou_tic(gpointer data)
{
	attendre_verrou(G_TASK(data));
	return G_SOURCE_REMOVE;
}

void
gt_releases_fetch_async(const gchar *url, const gchar *cache_dir, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data)
{
	GTask *task = g_task_new(NULL, cancellable, cb, user_data);
	FetchState *st;

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
	st->lock_fd = -1;
	g_task_set_task_data(task, st, fetch_state_free);
	{
		gchar *parent = g_path_get_dirname(cache_dir);
		gchar *lock = g_strconcat(cache_dir, ".lock", NULL);

		g_mkdir_with_parents(parent, 0700);
		st->lock_fd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if(st->lock_fd < 0)
			g_task_return_new_error(task, G_IO_ERROR, g_io_error_from_errno(errno), "%s: %s", lock, g_strerror(errno));
		g_free(lock);
		g_free(parent);
		if(st->lock_fd < 0)
		{
			g_object_unref(task);
			return;
		}
	}
	st->lock_deadline = g_get_monotonic_time() + lock_timeout_ms() * 1000;
	attendre_verrou(task);
}

GtReleases *
gt_releases_fetch_finish(GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(g_task_is_valid(res, NULL), NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}
