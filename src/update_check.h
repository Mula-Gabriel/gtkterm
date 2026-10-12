/* Lancement du script de mise à jour (sous-projets 6a puis « mise à jour par versions taguées ») : commande en liste d'arguments (M34), dans
 * un émulateur de terminal détecté. Versions et notes : update_versions.h, update_fetch.h. GLib seulement (cœur). */

#ifndef UPDATE_CHECK_H_
#define UPDATE_CHECK_H_

#include <glib.h>

G_BEGIN_DECLS

/* argv (g_strfreev) : <terminal> <options> <drapeau> sh -c <script fixe> <script> <url> <prefix> <pid> <tag> ; aucune valeur n'est
 * interprétée par un shell. terminal : nom ou chemin d'un émulateur pris en charge ; NULL si inconnu, si un argument est NULL ou si tag n'est
 * pas une version publiée vX.Y.Z. */
gchar **gt_update_terminal_argv(const gchar *terminal, const gchar *script, const gchar *url, const gchar *prefix, GPid pid, const gchar *tag);

/* Premier émulateur pris en charge trouvé dans le PATH (ordre : xterm, x-terminal-emulator, gnome-terminal, xfce4-terminal, konsole,
 * lxterminal, qterminal, urxvt, kitty), son nom (g_free) ; NULL si aucun. */
gchar *gt_update_find_terminal(void);

G_END_DECLS

#endif /* UPDATE_CHECK_H_ */
