/* Versions publiées et notes de version d'une source de mise à jour (sous-projet « mise à jour par versions taguées ») : dépôt nu du cache
 * ($XDG_CACHE_HOME/gtkterm/versions.git), cloné avec --filter=tree:0 (historique et tags, sans fichiers) puis mis à jour par fetch des tags ;
 * liste des tags vX.Y.Z ; NEWS du tag le plus récent. GLib/GIO seulement, git en sous-processus (jamais de shell). Partagé par les deux lignes. */

#ifndef UPDATE_FETCH_H_
#define UPDATE_FETCH_H_

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct
{
	GPtrArray *tags;	/* gchar * : versions publiées, de la plus récente à la plus ancienne ; jamais NULL dans un résultat */
	gchar *news;	/* texte de NEWS du tag le plus récent ; NULL si absent ou illisible (pas une erreur) */
} GtReleases;

void gt_releases_free(GtReleases *r);
/* $XDG_CACHE_HOME/gtkterm/versions.git (g_free) */
gchar *gt_releases_cache_dir(void);
/* cache_dir sans HEAD : git clone --bare --filter=tree:0 -- <url> <cache_dir> (dossiers parents créés) ; sinon remote set-url origin -- <url> ;
 * puis fetch --prune --force origin (refspec forcé de tous les tags) ; for-each-ref refs/tags ; show refs/tags/<plus récent>:NEWS.
 * git -c protocol.ext.allow=never, GIT_TERMINAL_PROMPT=0, LC_ALL=C. Le rappel est toujours appelé, jamais pendant cet appel ; annulation :
 * le git en cours reçoit SIGTERM (pas SIGKILL : il nettoie ses verrous et son clone inachevé).
 * Verrou : tout accès au cache est sérialisé par un verrou consultatif (flock) sur « <cache>.lock », tenu pendant toute l'opération ; occupé, on
 * réessaie toutes les 200 ms sans bloquer la boucle principale (annulable), au plus 10 s, puis G_IO_ERROR_BUSY « Another update check is in
 * progress. ». GTKTERM_UPDATE_LOCK_TIMEOUT_MS (en ms, POUR LES TESTS SEULEMENT, lue à chaque appel) remplace ces 10 s.
 * Cache abîmé : le verrou étant tenu, tout « .lock » du cache est périmé. Si set-url ou for-each-ref échoue, si le fetch échoue avec une erreur de
 * dépôt local (« Unable to create '….lock': File exists », « cannot lock ref », « not a git repository », « fatal: bad config », « is corrupt »
 * sur un chemin du cache ; l'URL est retirée du message avant l'examen), ou si le clone échoue parce que la cible est un dossier non vide, le
 * dossier est renommé à côté (« <cache>.abandonne-<horodatage en µs> », rien n'est supprimé) et l'opération repart UNE fois d'un clone (un second
 * échec rend l'erreur de cette seconde tentative, un renommage impossible l'erreur d'origine). Un échec réseau du fetch, ou une erreur de
 * communication avec git, rend l'erreur et garde le cache en place. */
void gt_releases_fetch_async(const gchar *url, const gchar *cache_dir, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data);
/* Résultat (gt_releases_free) ou NULL + erreur : G_IO_ERROR_INVALID_ARGUMENT (URL vide ou commençant par « - »), G_IO_ERROR_FAILED (première
 * ligne du message de git), G_IO_ERROR_CANCELLED, ou erreur de lancement de git (G_SPAWN_ERROR). */
GtReleases *gt_releases_fetch_finish(GAsyncResult *res, GError **error);

G_END_DECLS

#endif /* UPDATE_FETCH_H_ */
