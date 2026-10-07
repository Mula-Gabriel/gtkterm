/* macro_poller.c — ordonnanceur de polling (voir macro_poller.h et le §7 de la spec du sous-projet 4). */

#include "macro_poller.h"

typedef struct
{
	GtMacroPoller *poller;
	guint          id;
	guint          period_ms;
	guint          source;
} Running;

struct _GtMacroPoller
{
	GtMacroSet            *set;
	GtMacroPollerSendFunc  send;
	GtMacroPollerStateFunc state_changed;
	gpointer               user_data;
	GHashTable            *running;	/* id → Running * */
};

static void
running_free(gpointer data)
{
	Running *r = data;

	/* Retirer une source pendant sa propre exécution est permis : GLib l'ignore au retour */
	if(r->source != 0)
		g_source_remove(r->source);
	g_free(r);
}

static gboolean
on_tick(gpointer data)
{
	Running *r = data;
	GtMacroPoller *poller = r->poller;

	/* r peut être libéré par l'envoi (macro arrêtée depuis la fonction d'envoi) : ne plus y toucher après */
	poller->send(r->id, poller->user_data);
	return G_SOURCE_CONTINUE;
}

static void
notify(GtMacroPoller *poller, guint id, gboolean running)
{
	if(poller->state_changed != NULL)
		poller->state_changed(id, running, poller->user_data);
}

static void
restart_timer(Running *r, guint period_ms)
{
	if(r->source != 0)
		g_source_remove(r->source);
	r->period_ms = period_ms;
	r->source = g_timeout_add(period_ms, on_tick, r);
}

GtMacroPoller *
gt_macro_poller_new(GtMacroSet *set, GtMacroPollerSendFunc send, GtMacroPollerStateFunc state_changed, gpointer user_data)
{
	GtMacroPoller *poller = g_new0(GtMacroPoller, 1);

	poller->set = set;
	poller->send = send;
	poller->state_changed = state_changed;
	poller->user_data = user_data;
	poller->running = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, running_free);
	return poller;
}

void
gt_macro_poller_free(GtMacroPoller *poller)
{
	if(poller == NULL)
		return;
	poller->state_changed = NULL;
	g_hash_table_destroy(poller->running);
	g_free(poller);
}

void
gt_macro_poller_set_set(GtMacroPoller *poller, GtMacroSet *set)
{
	gt_macro_poller_reset(poller);
	poller->set = set;
}

gboolean
gt_macro_poller_start(GtMacroPoller *poller, guint id)
{
	const GtMacro *m = gt_macro_set_find(poller->set, id);
	Running *r;

	if(m == NULL || !m->polling || !gt_macro_is_sendable(m))
		return FALSE;
	if(gt_macro_poller_is_running(poller, id))
		return TRUE;
	r = g_new0(Running, 1);
	r->poller = poller;
	r->id = id;
	restart_timer(r, m->polling_period_ms);
	g_hash_table_insert(poller->running, GUINT_TO_POINTER(id), r);
	notify(poller, id, TRUE);
	/* Envoi immédiat ; il peut arrêter la macro (rappel d'envoi ré-entrant) */
	poller->send(id, poller->user_data);
	return gt_macro_poller_is_running(poller, id);
}

void
gt_macro_poller_stop(GtMacroPoller *poller, guint id)
{
	if(g_hash_table_remove(poller->running, GUINT_TO_POINTER(id)))
		notify(poller, id, FALSE);
}

gboolean
gt_macro_poller_toggle(GtMacroPoller *poller, guint id)
{
	if(gt_macro_poller_is_running(poller, id))
	{
		gt_macro_poller_stop(poller, id);
		return FALSE;
	}
	return gt_macro_poller_start(poller, id);
}

gboolean
gt_macro_poller_is_running(const GtMacroPoller *poller, guint id)
{
	return poller != NULL && g_hash_table_contains(poller->running, GUINT_TO_POINTER(id));
}

guint
gt_macro_poller_running_count(const GtMacroPoller *poller)
{
	return poller != NULL ? g_hash_table_size(poller->running) : 0;
}

void
gt_macro_poller_reset(GtMacroPoller *poller)
{
	/* Copie des clés : les rappels d'état peuvent toucher à l'ordonnanceur */
	GList *ids = g_hash_table_get_keys(poller->running);

	for(GList *l = ids; l != NULL; l = l->next)
		gt_macro_poller_stop(poller, GPOINTER_TO_UINT(l->data));
	g_list_free(ids);
}

void
gt_macro_poller_sync(GtMacroPoller *poller)
{
	GList *ids = g_hash_table_get_keys(poller->running);

	for(GList *l = ids; l != NULL; l = l->next)
	{
		guint id = GPOINTER_TO_UINT(l->data);
		const GtMacro *m = gt_macro_set_find(poller->set, id);

		if(m == NULL || !m->polling || !gt_macro_is_sendable(m))
			gt_macro_poller_stop(poller, id);
		else
			gt_macro_poller_period_changed(poller, id);
	}
	g_list_free(ids);
}

void
gt_macro_poller_period_changed(GtMacroPoller *poller, guint id)
{
	Running *r = g_hash_table_lookup(poller->running, GUINT_TO_POINTER(id));
	const GtMacro *m = gt_macro_set_find(poller->set, id);

	if(r != NULL && m != NULL && m->polling_period_ms != r->period_ms)
		restart_timer(r, m->polling_period_ms);
}
