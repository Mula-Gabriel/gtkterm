/* Journal des appels aux bouchons (sûr entre threads : le moteur Lua peut appeler un bouchon depuis son thread). */

#include <string.h>

#include "stubs.h"

static GMutex journal_mutex;
static GPtrArray *journal = NULL;	/* chaînes « nom\ttexte » */

void
stub_record(const gchar *nom, const gchar *texte)
{
	g_mutex_lock(&journal_mutex);
	if(journal == NULL)
		journal = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(journal, g_strdup_printf("%s\t%s", nom, texte ? texte : ""));
	g_mutex_unlock(&journal_mutex);
}

gint
stub_count(const gchar *nom)
{
	gint n = 0;
	gchar *prefixe = g_strdup_printf("%s\t", nom);

	g_mutex_lock(&journal_mutex);
	for(guint i = 0; journal != NULL && i < journal->len; i++)
		if(g_str_has_prefix(g_ptr_array_index(journal, i), prefixe))
			n++;
	g_mutex_unlock(&journal_mutex);
	g_free(prefixe);
	return n;
}

const gchar *
stub_last(const gchar *nom)
{
	const gchar *texte = NULL;
	gchar *prefixe = g_strdup_printf("%s\t", nom);

	g_mutex_lock(&journal_mutex);
	for(guint i = 0; journal != NULL && i < journal->len; i++)
		if(g_str_has_prefix(g_ptr_array_index(journal, i), prefixe))
			texte = (const gchar *)g_ptr_array_index(journal, i) + strlen(prefixe);
	g_mutex_unlock(&journal_mutex);
	g_free(prefixe);
	return texte;
}

void stub_link_reset(void);

void
stub_reset(void)
{
	g_mutex_lock(&journal_mutex);
	if(journal != NULL)
		g_ptr_array_set_size(journal, 0);
	g_mutex_unlock(&journal_mutex);
	stub_link_reset();
}
