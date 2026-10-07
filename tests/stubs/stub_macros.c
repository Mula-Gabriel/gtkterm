/* Bouchon de l'état des macros (macros.c) pour test_script_engine : jeu fourni par le test, fil appelant mémorisé. */

#include "link.h"
#include "stub_macros.h"

static GtMacroSet *jeu = NULL;
static GThread *thread_acces = NULL;

void
stub_set_macro_set(GtMacroSet *set)
{
	gt_macro_set_free(jeu);
	jeu = set;
}

GThread *
stub_macros_thread(void)
{
	return thread_acces;
}

GtMacroSet *
macros_get_set(void)
{
	thread_acces = g_thread_self();
	if(jeu == NULL)
		jeu = gt_macro_set_new();
	return jeu;
}

/* Même contrat que macros.c : macro inconnue ou non envoyable → FALSE ; n_args < 0 → arguments enregistrés */
gboolean
macros_send(guint id, const gchar *const *args, gint n_args)
{
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	GBytes *bytes;
	const guint8 *data;
	gsize len = 0;
	gssize written = 0;

	if(!gt_macro_is_sendable(m))
		return FALSE;
	bytes = gt_macro_expand(jeu, m, args, n_args);
	data = g_bytes_get_data(bytes, &len);
	if(len > 0)
		written = gt_link_send(gt_link_get_default(), data, len, GT_SEND_MACRO);
	g_bytes_unref(bytes);
	return len == 0 || (written >= 0 && (gsize)written == len);
}
