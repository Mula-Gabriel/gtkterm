/* Bouchon de get_shortcuts : renvoie le tableau fourni par le test et mémorise le thread appelant. */

#include "stub_macros.h"

static macro_t *macros_test = NULL;
static gint nb_macros_test = 0;
static GThread *thread_get_shortcuts = NULL;

void
stub_set_macros(macro_t *tab, gint n)
{
	macros_test = tab;
	nb_macros_test = n;
}

GThread *
stub_get_shortcuts_thread(void)
{
	return thread_get_shortcuts;
}

macro_t *
get_shortcuts(gint *size)
{
	thread_get_shortcuts = g_thread_self();
	*size = nb_macros_test;
	return macros_test;
}
