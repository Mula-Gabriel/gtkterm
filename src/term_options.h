/* État unique des options du terminal (sous-projet 6a) : écho, CR/LF auto, horodatage, ESC efface l'écran, mode de vue,
 * octets par ligne et index de la vue hexadécimale. Les menus en sont des GPropertyAction ; la vue, le journal et les captures
 * le lisent. Les champs correspondants du profil ne servent qu'à appliquer un profil et à l'enregistrer.
 *
 * Propriétés (lecture-écriture, « notify » seulement si la valeur change) :
 *   "echo", "crlf-auto", "timestamp", "esc-clear-screen", "hex-show-index"  gboolean, FALSE
 *   "view-mode"           GtViewMode (GEnum GT_TYPE_VIEW_MODE, nicks « ascii » / « hex »), GT_VIEW_ASCII
 *   "hex-bytes-per-line"  guint ∈ {8, 10, 16, 24, 32}, 16 ; une autre valeur est refusée (g_warning, valeur inchangée)
 * Signal "changed" (G_SIGNAL_RUN_LAST, sans argument) : émis APRÈS les « notify » des propriétés changées, une seule fois par
 * dispatch de GObject — donc une fois par g_object_set (même de plusieurs propriétés), une fois par gt_term_options_apply_profile,
 * une fois par lot g_object_freeze_notify/thaw_notify — et jamais quand rien ne change. Dans un gestionnaire « notify », « changed »
 * n'a pas encore été émis ; dans un gestionnaire « changed », tous les « notify » ont eu lieu et les accesseurs rendent les nouvelles
 * valeurs. Fil principal seulement. */
#ifndef TERM_OPTIONS_H_
#define TERM_OPTIONS_H_

#include <glib-object.h>

#include "profile.h"
#include "view_render.h"

G_BEGIN_DECLS

#define GT_TYPE_TERM_OPTIONS (gt_term_options_get_type())
G_DECLARE_FINAL_TYPE(GtTermOptions, gt_term_options, GT, TERM_OPTIONS, GObject)

#define GT_TYPE_VIEW_MODE (gt_view_mode_get_type())
GType          gt_view_mode_get_type(void);	/* GtViewMode de view_render.h, nicks « ascii » / « hex » */

GtTermOptions *gt_term_options_new(void);
/* Recopie les 4 drapeaux du profil (echo, crlf_auto, timestamp, esc_clear_screen) ; un seul « changed », après leurs « notify »,
 * s'il y a eu changement.
 * Le mode de vue, les octets par ligne et l'index ne font pas partie du profil : inchangés. */
void           gt_term_options_apply_profile(GtTermOptions *o, const GtProfile *p);
/* Recopie inverse des 4 drapeaux, juste avant l'enregistrement du profil. */
void           gt_term_options_store_profile(GtTermOptions *o, GtProfile *p);
/* Options de rendu pour view_render.c (mode, écho, CR/LF auto, horodatage, octets par ligne, index). */
void           gt_term_options_get_view(GtTermOptions *o, GtViewOptions *out);
/* Options de GtRxTransform (journal, captures) ; pointeurs NULL acceptés. */
void           gt_term_options_get_rx(GtTermOptions *o, gboolean *crlf_auto, gboolean *timestamp);
gboolean       gt_term_options_get_esc_clear_screen(GtTermOptions *o);

G_END_DECLS

#endif
