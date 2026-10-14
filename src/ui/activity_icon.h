/* Indicateur d'activité d'un port (onglet, barre d'état) : emplacement 18 × 16 px montrant l'état d'un GtPortActivity (pastille, spinner,
 * Zzz, tête de mort). Animé par le code (rappel par image, minuterie), pas en CSS : les animations CSS s'arrêtent quand le bureau les réduit. */
#ifndef GT_UI_ACTIVITY_ICON_H_
#define GT_UI_ACTIVITY_ICON_H_

#include <gtk/gtk.h>

#include "port_activity.h"

G_BEGIN_DECLS

/* name : nom stable du widget (tests). La classe CSS « activity-<nick> » suit l'état ; nom accessible traduit selon l'état. Le widget garde une
 * référence sur activity jusqu'à sa destruction ; minuterie et rappel par image n'existent que pour l'état animé affiché et un widget
 * cartographié. */
GtkWidget *gt_activity_icon_new(GtPortActivity *activity, const char *name);

G_END_DECLS

#endif
