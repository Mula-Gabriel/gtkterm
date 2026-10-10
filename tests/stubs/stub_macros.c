/* Bouchon de l'état des macros (macros.c) pour test_script_engine : une instance par moteur, jeu fourni par le test, fil appelant
 * mémorisé, appels notés dans le journal du bouchon (« macros_send_quiet »). Seules fonctions de macros appelées par le moteur :
 * gt_macros_get_set et gt_macros_send_quiet. */

#include "link.h"
#include "stub_macros.h"
#include "stubs.h"

struct _GtMacros
{
	GtMacroSet *set;
};

static GtMacros *instance_defaut = NULL;
static GPtrArray *instances = NULL;	/* toutes les instances : atteignables jusqu'à la fin du processus */
static GThread *thread_acces = NULL;

GtMacros *
stub_macros_new(void)
{
	GtMacros *m = g_new0(GtMacros, 1);

	if(instances == NULL)
		instances = g_ptr_array_new();
	g_ptr_array_add(instances, m);
	return m;
}

GtMacros *
stub_macros_default(void)
{
	if(instance_defaut == NULL)
		instance_defaut = stub_macros_new();
	return instance_defaut;
}

void
stub_macros_set_set(GtMacros *m, GtMacroSet *set)
{
	gt_macro_set_free(m->set);
	m->set = set;
}

GThread *
stub_macros_thread(void)
{
	return thread_acces;
}

GtMacroSet *
gt_macros_get_set(GtMacros *m)
{
	thread_acces = g_thread_self();
	if(m->set == NULL)
		m->set = gt_macro_set_new();
	return m->set;
}

/* Même contrat que macros.c : macro inconnue ou non envoyable → FALSE ; n_args < 0 → arguments enregistrés */
gboolean
gt_macros_send_quiet(GtMacros *m, guint id, const gchar *const *args, gint n_args)
{
	const GtMacro *macro = gt_macro_set_find(gt_macros_get_set(m), id);
	GBytes *bytes;
	const guint8 *data;
	gsize len = 0;
	gssize written = 0;

	stub_record("macros_send_quiet", NULL);
	if(!gt_macro_is_sendable(macro))
		return FALSE;
	bytes = gt_macro_expand(m->set, macro, args, n_args);
	data = g_bytes_get_data(bytes, &len);
	if(len > 0)
		written = gt_link_send(stub_link_default(), data, len, GT_SEND_MACRO);
	g_bytes_unref(bytes);
	return len == 0 || (written >= 0 && (gsize)written == len);
}
