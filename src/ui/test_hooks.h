/* Crochet de la suite graphique GTK 4 (tests/gui/) : introspection des widgets et texte du terminal servis par org.gtk.Actions.
 * Sous broadway, GTK 4 n'a pas d'AT-SPI : ces actions donnent au harnais ce qu'AT-SPI lui donnait. Actif seulement si
 * GTKTERM_TEST_HOOKS=1 (et si la construction l'inclut : -Dtest_hooks=true, défaut). */
#ifndef GT_UI_TEST_HOOKS_H_
#define GT_UI_TEST_HOOKS_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Vrai si la variable d'environnement GTKTERM_TEST_HOOKS vaut « 1 ». */
gboolean gt_test_hooks_enabled(void);
/* Ajoute à l'application les actions test-dump (état aa{sv}), test-terminal-text (état s), test-terminal-props (état a{sv}), test-sessions
 * (état aa{sv}, un dict par onglet), test-project (état a{sv}), test-activate (s) et test-set-text ((ss)).
 * À appeler dans « startup », seulement si gt_test_hooks_enabled(). */
void     gt_test_hooks_install(GtkApplication *app);

G_END_DECLS

#endif
