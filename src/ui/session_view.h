/* Page d'onglet (sous-projet 6b, spec §4.6) : tout ce qui est sous la barre de menus pour une session. De haut en bas : emplacement « search-bar »,
 * GtkPaned vertical [GtkPaned horizontal (terminal | emplacement « macro-panel ») / emplacement « script-panel »], emplacements « hex-bar » et
 * « status-bar ». Ajoute au groupe d'actions de la session les actions de page (plan, décision 4). */
#ifndef GT_UI_SESSION_VIEW_H_
#define GT_UI_SESSION_VIEW_H_

#include <gtk/gtk.h>

#include "app_config.h"
#include "project_file.h"
#include "session.h"

G_BEGIN_DECLS

/* GtkBox verticale nommée « session-view » ; st : séparateurs et visibilités initiaux (h_paned, v_paned, *_visible ; les autres champs ignorés).
 * Crée aussi le panneau de macros, les raccourcis de macros et le panneau de script de la session. layout_from : page dont le panneau de script
 * donne sa disposition (nouvel onglet) ; NULL pour la première page ([layout] et [session] lus). */
GtkWidget *gt_session_view_new(GtkApplication *app, GtSession *s, const GtWindowState *st, GtkWidget *layout_from);
GtSession *gt_session_view_get_session(GtkWidget *view);
GtkWidget *gt_session_view_lookup(GtSession *s);	/* page de la session, NULL si aucune ou arrêtée */
GtkWidget *gt_session_view_get_terminal_view(GtkWidget *view);
void       gt_session_view_focus_terminal(GtkWidget *view);	/* focus clavier au VteTerminal (fermeture d'un popover, d'une barre, d'un dialogue) */
/* slots : "macro-panel", "script-panel", "hex-bar", "status-bar", "search-bar" ; child remplace le précédent (NULL : vide).
 * « macro-panel », « script-panel » et « hex-bar » prennent la visibilité initiale (st). */
void       gt_session_view_set_slot(GtkWidget *view, const char *slot, GtkWidget *child);
GtkWidget *gt_session_view_get_slot(GtkWidget *view, const char *slot);
/* Remplit h_paned, v_paned et les trois visibilités seulement ; un emplacement vide garde la valeur initiale (st) */
void       gt_session_view_get_state(GtkWidget *view, GtWindowState *out);
/* Onglet de projet : h_paned, v_paned et visibilités (gt_session_view_get_state) et disposition du panneau de script ("topbottom" | "leftright") */
void       gt_session_view_fill_project_tab(GtkWidget *view, GtProjectTab *tab);
/* Disposition du panneau de script seulement : séparateurs et visibilités sont déjà passés à gt_session_view_new */
void       gt_session_view_apply_project_tab(GtkWidget *view, const GtProjectTab *tab);
void       gt_session_view_apply_profile(GtkWidget *view);	/* terminal ← profil de la session, couleurs du panneau de script */
/* Plan, décision 3, étape a : envoi de fichier et journal arrêtés, panneau de macros, raccourcis et panneau de script arrêtés, barre d'état
 * détachée, actions de page retirées du groupe de la session.
 * Idempotente ; à appeler avant gt_session_shutdown. */
void       gt_session_view_shutdown(GtkWidget *view);
gboolean   gt_session_view_is_shut_down(GtkWidget *view);

G_END_DECLS

#endif
