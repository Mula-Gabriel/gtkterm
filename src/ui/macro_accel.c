/* macro_accel.c — raccourcis des macros : forme canonique, capture d'une touche, propriétaires (sans affichage). */

#include <string.h>

#include "macro_accel.h"

/* Modificateurs qui comptent dans un raccourci (Ctrl, Maj, Alt, Super, Hyper, Meta) ; verrouillages exclus */
static GdkModifierType accel_mods(GdkModifierType state)
{
	return state & gtk_accelerator_get_default_mod_mask();
}

gchar *gt_macro_accel_normalize(const gchar *accel)
{
	guint key = 0;
	GdkModifierType mods = 0;

	if(accel == NULL || accel[0] == '\0' || !gtk_accelerator_parse(accel, &key, &mods) || key == 0)
		return NULL;
	return gtk_accelerator_name(gdk_keyval_to_lower(key), accel_mods(mods));
}

static gboolean is_modifier_key(guint keyval)
{
	switch(keyval)
	{
	case GDK_KEY_Shift_L:
	case GDK_KEY_Shift_R:
	case GDK_KEY_Control_L:
	case GDK_KEY_Control_R:
	case GDK_KEY_Caps_Lock:
	case GDK_KEY_Shift_Lock:
	case GDK_KEY_Meta_L:
	case GDK_KEY_Meta_R:
	case GDK_KEY_Alt_L:
	case GDK_KEY_Alt_R:
	case GDK_KEY_Super_L:
	case GDK_KEY_Super_R:
	case GDK_KEY_Hyper_L:
	case GDK_KEY_Hyper_R:
	case GDK_KEY_ISO_Level3_Shift:
	case GDK_KEY_ISO_Level5_Shift:
	case GDK_KEY_Mode_switch:
	case GDK_KEY_Num_Lock:
		return TRUE;
	default:
		return FALSE;
	}
}

GtAccelKeyResult gt_macro_accel_from_key(guint keyval, GdkModifierType state, gchar **accel_out)
{
	GdkModifierType mods = accel_mods(state);

	*accel_out = NULL;
	if(is_modifier_key(keyval))
		return GT_ACCEL_KEY_MODIFIER;
	if(keyval == GDK_KEY_Escape && mods == 0)
		return GT_ACCEL_KEY_CANCEL;
	/* Sans Ctrl, Alt ni Super, seules les touches de fonction : les autres (caractères, Entrée, flèches…) servent au terminal */
	if((mods & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) == 0 && !(keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F35))
		return GT_ACCEL_KEY_NEEDS_MODIFIER;
	*accel_out = gtk_accelerator_name(gdk_keyval_to_lower(keyval), mods);
	return GT_ACCEL_KEY_ACCEPTED;
}

static void owner_clear(gpointer data)
{
	GtAccelOwner *o = data;

	g_free(o->accel);
	g_free(o->owner);
}

GArray *gt_accel_owners_new(void)
{
	GArray *owners = g_array_new(FALSE, TRUE, sizeof(GtAccelOwner));

	g_array_set_clear_func(owners, owner_clear);
	return owners;
}

void gt_accel_owners_add(GArray *owners, const gchar *accel, const gchar *owner, gboolean is_action)
{
	GtAccelOwner o;

	o.accel = gt_macro_accel_normalize(accel);
	if(o.accel == NULL)
		return;
	o.owner = g_strdup(owner != NULL ? owner : "");
	o.is_action = is_action;
	g_array_append_val(owners, o);
}

void gt_accel_owners_add_app(GArray *owners, GtkApplication *app)
{
	gchar **actions = gtk_application_list_action_descriptions(app);

	for(guint i = 0; actions != NULL && actions[i] != NULL; i++)
	{
		gchar **accels = gtk_application_get_accels_for_action(app, actions[i]);
		const gchar *name = g_str_has_prefix(actions[i], "app.") ? actions[i] + 4 : actions[i];

		for(guint k = 0; accels != NULL && accels[k] != NULL; k++)
			gt_accel_owners_add(owners, accels[k], name, TRUE);
		g_strfreev(accels);
	}
	g_strfreev(actions);
}

const GtAccelOwner *gt_accel_owners_find(GArray *owners, const gchar *accel)
{
	gchar *key = gt_macro_accel_normalize(accel);
	const GtAccelOwner *found = NULL;

	for(guint i = 0; key != NULL && found == NULL && i < owners->len; i++)
	{
		const GtAccelOwner *o = &g_array_index(owners, GtAccelOwner, i);

		if(strcmp(o->accel, key) == 0)
			found = o;
	}
	g_free(key);
	return found;
}
