/* Projet de la fenêtre (6c, spec §4.3) : chemin, instantané enregistré, état « modifié ». Couche application ; ne crée ni ne ferme d'onglet
 * (l'interface le fait). Fil principal seulement. */
#ifndef PROJECT_H_
#define PROJECT_H_

#include <gio/gio.h>

#include "file_stamp.h"
#include "project_file.h"
#include "session_manager.h"

G_BEGIN_DECLS

#define GT_TYPE_PROJECT (gt_project_get_type())
G_DECLARE_FINAL_TYPE(GtProject, gt_project, GT, PROJECT, GObject)

typedef enum { GT_PROJECT_SAVED, GT_PROJECT_FAILED, GT_PROJECT_CONFLICT } GtProjectSaveResult;
/* Rappel de l'interface : remplit tab->page (h_paned, v_paned, visibilités) et tab->script_layout pour la page de s */
typedef void (*GtProjectPageFunc)(GtSession *s, GtProjectTab *tab, gpointer user_data);

/* Propriétés : « open » (b), « path » (s, NULL : sans fichier), « modified » (b), notifiées seulement si elles changent */
GtProject     *gt_project_new(GtSessionManager *sessions);	/* aucun projet ouvert */
void           gt_project_set_page_func(GtProject *p, GtProjectPageFunc func, gpointer user_data);
gboolean       gt_project_is_open(GtProject *p);
const gchar   *gt_project_get_path(GtProject *p);
gchar         *gt_project_get_name(GtProject *p);	/* nom de base sans .gtkterm, « Untitled project » sans fichier ; "" si fermé */
gboolean       gt_project_is_modified(GtProject *p);
/* Sessions du gestionnaire dans l'ordre : profil (options et auto-reconnect recopiés, macros_file NULL), nom de profil, copie du jeu, texte du script,
 * état de page (rappel) ; active = index de la session active */
GtProjectData *gt_project_capture(GtProject *p);
/* Projet ouvert : chaque session (et chaque session ajoutée ensuite) passe en in-project ; instantané de référence = celui de saved (données lues
 * du fichier), ou de la capture actuelle si saved est NULL ; path NULL : sans fichier ; stamp NULL : inconnu. Puis calcul de « modified ». */
void           gt_project_begin(GtProject *p, const gchar *path, const GtFileStamp *stamp, const GtProjectData *saved);
void           gt_project_end(GtProject *p);	/* plus de projet : open FALSE, path NULL, modified FALSE (les sessions restent comme elles sont) */
/* path NULL : chemin courant (obligatoire). CONFLICT : même chemin, fichier changé sur le disque, !force ; rien n'est écrit. SAVED : chemin et
 * identité retenus, instantané = capture enregistrée, modified FALSE. FAILED : GError ; projet fermé : FAILED (« No project is open »), rien écrit ni changé
 * (appeler gt_project_begin d'abord). Identité inconnue (begin sans stamp) : aucun contrôle de conflit jusqu'au premier enregistrement réussi. */
GtProjectSaveResult gt_project_save(GtProject *p, const gchar *path, gboolean force, GError **error);
void           gt_project_check_now(GtProject *p);	/* recalcule « modified » tout de suite */

G_END_DECLS

#endif
