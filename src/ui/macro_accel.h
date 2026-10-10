/* macro_accel.h — raccourcis des macros dans l'interface GTK 4 : forme canonique, capture d'une touche, propriétaires des raccourcis.
 * Aucune fonction n'a besoin d'un affichage. */

#ifndef GT_UI_MACRO_ACCEL_H_
#define GT_UI_MACRO_ACCEL_H_

#include <gtk/gtk.h>

typedef enum
{
	GT_ACCEL_KEY_ACCEPTED,	/* *accel_out reçoit la forme canonique (g_free) */
	GT_ACCEL_KEY_MODIFIER,	/* touche de modification seule : attendre la suivante */
	GT_ACCEL_KEY_CANCEL,	/* Échap sans modificateur : capture annulée */
	GT_ACCEL_KEY_NEEDS_MODIFIER	/* sans Ctrl, Alt ni Super et pas une touche F1-F35 : la touche appartient au terminal, refusée */
} GtAccelKeyResult;

typedef struct
{
	gchar   *accel;	/* forme canonique */
	gchar   *owner;	/* nom d'action sans « app. », ou libellé de macro */
	gboolean is_action;
} GtAccelOwner;

gchar              *gt_macro_accel_normalize(const gchar *accel);	/* touche en minuscule, modificateurs par défaut ; NULL si vide ou illisible */
GtAccelKeyResult    gt_macro_accel_from_key(guint keyval, GdkModifierType state, gchar **accel_out);	/* *accel_out NULL sauf ACCEPTED */
GArray             *gt_accel_owners_new(void);	/* tableau de GtAccelOwner, éléments libérés par g_array_unref */
void                gt_accel_owners_add(GArray *owners, const gchar *accel, const gchar *owner, gboolean is_action);	/* illisible : ignoré */
void                gt_accel_owners_add_app(GArray *owners, GtkApplication *app);	/* accélérateurs de toutes les actions de l'application */
const GtAccelOwner *gt_accel_owners_find(GArray *owners, const gchar *accel);	/* NULL si libre (ou illisible) */

#endif
