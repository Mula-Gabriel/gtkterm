/* Fichier projet .gtkterm (6c, spec §3.2-1 et §4.2) : GKeyFile, [gtkterm-project] version=1 active=N, [tab:N] (profil + profile_name + état de page +
 * script_layout + script + macros_template d'un onglet lié), [tab:N:macro:M], [tab:N:list:K], [tab:N:override:K] (onglet lié : valeurs propres,
 * spec 2026-10-15-modeles-macros §2.4). Sans GTK. */
#ifndef PROJECT_FILE_H_
#define PROJECT_FILE_H_

#include <glib.h>

#include "file_stamp.h"
#include "macro_override.h"
#include "macro_set.h"
#include "profile.h"
#include "window_state.h"

#define GT_PROJECT_FILE_VERSION 1

#define GT_PROJECT_ERROR (gt_project_error_quark())
typedef enum
{
	GT_PROJECT_ERROR_NOT_PROJECT,	/* syntaxe invalide ou pas de [gtkterm-project] */
	GT_PROJECT_ERROR_VERSION,	/* version absente ou ≠ 1 */
	GT_PROJECT_ERROR_NO_TAB,	/* aucun [tab:0] */
	GT_PROJECT_ERROR_BAD_TAB	/* transport absent ou inconnu dans un [tab:N] */
} GtProjectError;
GQuark gt_project_error_quark(void);

typedef struct
{
	GtProfile profile;	/* macros_file toujours NULL ; lock_port TRUE */
	gchar *profile_name;	/* jamais NULL (« default ») */
	GtWindowState page;	/* h_paned, v_paned, visibilités ; width/height/maximized ignorés (0) */
	gchar *script_layout;	/* "topbottom" | "leftright" */
	GtMacroSet *macros;	/* jamais NULL ; onglet lié : la base (copie du modèle), pas le jeu effectif */
	gchar *macros_template;	/* chemin absolu du modèle lié ; NULL : non lié */
	GtMacroOverrides *overrides;	/* valeurs propres de l'onglet lié ; jamais NULL (vide si non lié) */
	gchar *script;		/* jamais NULL ("" : aucun) */
} GtProjectTab;

typedef struct
{
	GPtrArray *tabs;	/* GtProjectTab *, possédés */
	guint active;		/* < tabs->len dès qu'il y a un onglet */
} GtProjectData;

GtProjectTab  *gt_project_tab_new(void);	/* profil par défaut, « default », page par défaut (-1, -1, TRUE, FALSE, FALSE), "topbottom", jeu vide, "", non lié */
void           gt_project_tab_free(GtProjectTab *t);
GtProjectData *gt_project_data_new(void);	/* aucun onglet, active 0 */
void           gt_project_data_free(GtProjectData *d);	/* NULL accepté */
/* Tout ou rien : FALSE + GError (G_FILE_ERROR, G_KEY_FILE_ERROR ou GT_PROJECT_ERROR, message traduit préfixé du chemin), *out non écrit.
 * stamp (peut être NULL) : identité du fichier lu. active hors bornes → 0. */
gboolean       gt_project_data_load(const gchar *path, GtProjectData **out, GtFileStamp *stamp, GError **error);
/* Écriture atomique (0644) ; stamp (peut être NULL) : identité du fichier écrit */
gboolean       gt_project_data_save(const GtProjectData *d, const gchar *path, GtFileStamp *stamp, GError **error);
gchar         *gt_project_data_to_text(const GtProjectData *d);	/* contenu exact du fichier */
gchar         *gt_project_data_snapshot(const GtProjectData *d);	/* même sérialisation sans état de page, sans script_layout ni active,
								 * ni base d'un onglet lié */

#endif
