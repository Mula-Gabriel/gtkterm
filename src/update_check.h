/* Vérification de mise à jour (sous-projet 6a) : révision distante par « git ls-remote », comparaison avec la révision construite (M33),
 * révision ignorée (M34), commande du script de mise à jour en liste d'arguments (M34). GLib/GIO seulement (cœur) : aucune globale,
 * l'appelant passe GIT_REVISION, l'URL, le préfixe et la révision ignorée. */

#ifndef UPDATE_CHECK_H_
#define UPDATE_CHECK_H_

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
	GT_UPDATE_UP_TO_DATE,	/* même révision, locale plus récente, ou réponse distante inexploitable */
	GT_UPDATE_AVAILABLE,	/* distante différente, pas plus ancienne que la locale, non ignorée */
	GT_UPDATE_SKIPPED,	/* distante == révision ignorée (« Skip this version ») */
	GT_UPDATE_LOCAL_UNKNOWN	/* révision construite inconnue (« unknown ») : aucune proposition au démarrage */
} GtUpdateVerdict;

/* Ordre : locale inconnue → LOCAL_UNKNOWN ; distante invalide → UP_TO_DATE ; égales (sans casse) → UP_TO_DATE ; local_is_newer → UP_TO_DATE ;
 * distante == skipped_rev (sans casse ; NULL ou "" : aucune) → SKIPPED ; sinon AVAILABLE. Révision valide : 7 à 64 chiffres hexadécimaux. */
GtUpdateVerdict gt_update_compare(const gchar *local_rev, const gchar *remote_rev, const gchar *skipped_rev, gboolean local_is_newer);

/* git -c protocol.ext.allow=never ls-remote -- <url> HEAD (GIT_TERMINAL_PROMPT=0, LC_ALL=C), asynchrone ; annulation : git est tué.
 * Le rappel est toujours appelé (jamais pendant cet appel) ; finish rend le SHA distant (40 ou 64 hex, minuscules, g_free) ou NULL + erreur
 * (G_IO_ERROR_INVALID_ARGUMENT : URL vide ; G_IO_ERROR_FAILED : message de git ; G_IO_ERROR_INVALID_DATA : réponse sans HEAD ; G_IO_ERROR_CANCELLED). */
void   gt_update_fetch_remote_async(const gchar *url, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data);
gchar *gt_update_fetch_remote_finish(GAsyncResult *res, GError **error);

/* TRUE seulement si <cache_src_dir>/.git existe et que remote_rev est un ancêtre de local_rev (git merge-base --is-ancestor, synchrone).
 * FALSE sans dépôt cache, pour une révision non hexadécimale (« unknown ») ou inconnue du dépôt cache. */
gboolean gt_update_local_is_newer(const gchar *cache_src_dir, const gchar *local_rev, const gchar *remote_rev);

/* $XDG_CACHE_HOME/gtkterm/src : la copie de travail de data/gtkterm-update.sh (g_free). */
gchar *gt_update_cache_src_dir(void);

/* argv (g_strfreev) : <terminal> <options> <drapeau> sh -c <script fixe> <script> <url> <prefix> <pid> ; aucune valeur n'est interprétée par un
 * shell. terminal : nom ou chemin d'un émulateur pris en charge ; NULL si inconnu ou si un argument est NULL. */
gchar **gt_update_terminal_argv(const gchar *terminal, const gchar *script, const gchar *url, const gchar *prefix, GPid pid);

/* Premier émulateur pris en charge trouvé dans le PATH (ordre : xterm, x-terminal-emulator, gnome-terminal, xfce4-terminal, konsole,
 * lxterminal, qterminal, urxvt, kitty), son nom (g_free) ; NULL si aucun. */
gchar *gt_update_find_terminal(void);

G_END_DECLS

#endif /* UPDATE_CHECK_H_ */
