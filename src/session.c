/* Session d'onglet (sous-projet 6b, spec §4.2) : voir session.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "app_ui.h"
#include "session.h"

struct _GtSession
{
	GObject parent_instance;
	guint id;
	GtkTermLink *link;
	GtTermOptions *options;
	GtProfile profile;
	gchar *profile_name;	/* NULL : « default » */
	GtMacros *macros;
	GtScriptEngine *engine;
	GSimpleActionGroup *actions;
	gchar *short_name;
	gboolean file_sending;
	gboolean shut_down;
};

enum { PROP_0, PROP_SHORT_NAME, PROP_STATUS_TEXT, N_PROPS };
enum { SIGNAL_CLOSED, N_SIGNALS };
static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];
static guint next_id = 1;	/* identifiants jamais réutilisés (compteur du processus, pas un état d'onglet) */

G_DEFINE_TYPE(GtSession, gt_session, G_TYPE_OBJECT)

gchar *gt_session_short_name_for(const GtProfile *p)
{
	switch(p->transport)
	{
	case TRANSPORT_TCP_CLIENT:
		return g_strdup_printf("%s:%s", p->host != NULL ? p->host : "localhost", p->tcp_port != NULL ? p->tcp_port : "");
	case TRANSPORT_TCP_SERVER:
		return g_strdup_printf(":%s", p->tcp_port != NULL ? p->tcp_port : "");
	default:
		return p->port != NULL && p->port[0] != '\0' ? g_path_get_basename(p->port) : g_strdup("?");
	}
}

static void update_short_name(GtSession *s)
{
	gchar *name = gt_session_short_name_for(&s->profile);

	if(g_strcmp0(name, s->short_name) == 0)
	{
		g_free(name);
		return;
	}
	g_free(s->short_name);
	s->short_name = name;
	g_object_notify_by_pspec(G_OBJECT(s), props[PROP_SHORT_NAME]);
}

/* notify::status-text de la liaison (peut tourner sous la surveillance du transport : ne fait que relayer la notification) */
static void on_link_status(GObject *link, GParamSpec *pspec, gpointer user_data)
{
	g_object_notify_by_pspec(G_OBJECT(user_data), props[PROP_STATUS_TEXT]);
}

/* ---- Actions d'onglet de la session (plan, décision 4) ---- */

static void set_action_enabled(GtSession *s, const gchar *name, gboolean enabled)
{
	GAction *a = g_action_map_lookup_action(G_ACTION_MAP(s->actions), name);

	if(G_IS_SIMPLE_ACTION(a))
		g_simple_action_set_enabled(G_SIMPLE_ACTION(a), enabled);
}

/* Send break, DTR, RTS : seulement sur une liaison série ouverte (d'après la liaison, pas d'après le profil) */
static void update_serial_actions(GtSession *s)
{
	gboolean serial = !s->shut_down && gt_link_is_serial_open(s->link);

	set_action_enabled(s, "send-break", serial);
	set_action_enabled(s, "toggle-dtr", serial);
	set_action_enabled(s, "toggle-rts", serial);
}

/* notify::state et notify::status-text de la liaison : peut tourner sous la surveillance du transport, ne fait que changer des sensibilités */
static void on_link_notify_actions(GObject *link, GParamSpec *pspec, gpointer user_data)
{
	update_serial_actions(GT_SESSION(user_data));
}

static void on_clear_screen(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_link_clear_history(GT_SESSION(user_data)->link);
}

static void on_open_port(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_session_open_port(GT_SESSION(user_data));
}

static void on_close_port(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_session_close_port(GT_SESSION(user_data));
}

static void on_send_break(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSession *s = GT_SESSION(user_data);

	if(!gt_link_is_serial_open(s->link))
		return;
	gt_link_send_break(s->link);
	gt_app_status_flash(s, 800, "%s", _("Break signal sent!"));
}

static void on_toggle_dtr(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSession *s = GT_SESSION(user_data);

	if(gt_link_is_serial_open(s->link))
		gt_link_toggle_line(s->link, GT_MODEM_DTR);
}

static void on_toggle_rts(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSession *s = GT_SESSION(user_data);

	if(gt_link_is_serial_open(s->link))
		gt_link_toggle_line(s->link, GT_MODEM_RTS);
}

static void add_property_action(GtSession *s, const gchar *name, gpointer object, const gchar *property)
{
	GPropertyAction *action = g_property_action_new(name, object, property);

	g_action_map_add_action(G_ACTION_MAP(s->actions), G_ACTION(action));
	g_object_unref(action);
}

static void install_actions(GtSession *s)
{
	static const GActionEntry entries[] =
	{
		{ .name = "clear-screen", .activate = on_clear_screen },
		{ .name = "open-port", .activate = on_open_port },
		{ .name = "close-port", .activate = on_close_port },
		{ .name = "send-break", .activate = on_send_break },
		{ .name = "toggle-dtr", .activate = on_toggle_dtr },
		{ .name = "toggle-rts", .activate = on_toggle_rts },
	};

	/* Actions à état : GPropertyAction sur les options et la liaison de la session, aucune copie d'état */
	add_property_action(s, "local-echo", s->options, "echo");
	add_property_action(s, "crlf-auto", s->options, "crlf-auto");
	add_property_action(s, "timestamp", s->options, "timestamp");
	add_property_action(s, "esc-clear-screen", s->options, "esc-clear-screen");
	add_property_action(s, "hex-index", s->options, "hex-show-index");
	add_property_action(s, "view-mode", s->options, "view-mode");	/* état chaîne : nick « ascii » / « hex » */
	add_property_action(s, "hex-bytes", s->options, "hex-bytes-per-line");	/* état « u » */
	add_property_action(s, "auto-reconnect", s->link, "auto-reconnect");
	g_action_map_add_action_entries(G_ACTION_MAP(s->actions), entries, G_N_ELEMENTS(entries), s);
	g_signal_connect(s->link, "notify::state", G_CALLBACK(on_link_notify_actions), s);
	g_signal_connect(s->link, "notify::status-text", G_CALLBACK(on_link_notify_actions), s);
	update_serial_actions(s);
}

GtSession *gt_session_new(const GtProfile *profile, const gchar *profile_name)
{
	GtSession *s = g_object_new(GT_TYPE_SESSION, NULL);

	s->id = next_id++;
	s->link = gt_link_new();
	s->options = gt_term_options_new();
	if(profile != NULL)
		gt_profile_copy(&s->profile, profile);
	else
		gt_profile_init_defaults(&s->profile);
	s->profile_name = g_strdup(profile_name);
	s->macros = gt_macros_new(s->link, s);
	s->engine = gt_script_engine_new(s->link, s->macros);
	s->actions = g_simple_action_group_new();
	s->short_name = gt_session_short_name_for(&s->profile);
	g_signal_connect(s->link, "notify::status-text", G_CALLBACK(on_link_status), s);
	install_actions(s);
	return s;
}

guint gt_session_get_id(GtSession *s)
{
	return s->id;
}

GtkTermLink *gt_session_get_link(GtSession *s)
{
	return s->link;
}

GtTermOptions *gt_session_get_options(GtSession *s)
{
	return s->options;
}

GtProfile *gt_session_get_profile(GtSession *s)
{
	return &s->profile;
}

const gchar *gt_session_get_profile_name(GtSession *s)
{
	return s->profile_name != NULL ? s->profile_name : "default";
}

void gt_session_set_profile_name(GtSession *s, const gchar *name)
{
	gt_profile_set_string(&s->profile_name, name);
}

GtMacros *gt_session_get_macros(GtSession *s)
{
	return s->macros;
}

GtScriptEngine *gt_session_get_script_engine(GtSession *s)
{
	return s->engine;
}

GActionGroup *gt_session_get_actions(GtSession *s)
{
	return G_ACTION_GROUP(s->actions);
}

const gchar *gt_session_get_short_name(GtSession *s)
{
	return s->short_name;
}

void gt_session_set_file_sending(GtSession *s, gboolean sending)
{
	s->file_sending = sending != FALSE;
}

gboolean gt_session_is_busy(GtSession *s)
{
	return !s->shut_down && (gt_script_engine_is_running(s->engine) || s->file_sending);
}

gboolean gt_session_is_shut_down(GtSession *s)
{
	return s->shut_down;
}

void gt_session_apply_profile(GtSession *s)
{
	if(s->shut_down)
		return;
	gt_term_options_apply_profile(s->options, &s->profile);	/* un seul « changed » : un seul redessin du terminal */
	g_object_set(s->link, "auto-reconnect", s->profile.auto_reconnect, NULL);
	update_short_name(s);
}

void gt_session_open_port(GtSession *s)
{
	TransportParams params;

	if(s->shut_down)
		return;
	update_short_name(s);
	gt_profile_to_transport_params(&s->profile, &params);
	gt_link_open(s->link, &params, NULL);	/* échec : open-failed de la liaison (l'interface affiche le message) */
	transport_params_clear(&params);
}

void gt_session_close_port(GtSession *s)
{
	gt_link_close(s->link);
}

void gt_session_shutdown(GtSession *s)
{
	gchar **names;

	if(s->shut_down)
		return;
	s->shut_down = TRUE;
	/* Jusqu'à 3 s d'attente (interface figée) ; au-delà, thread Lua abandonné : à la fermeture d'un onglet, lui et ses ports Lua 1-15
	 * vivent jusqu'au retour de l'appel C qui le bloque, l'application continue (voir gt_script_engine_free) */
	gt_script_engine_shutdown(s->engine);
	gt_macros_shutdown(s->macros);
	/* Actions désactivées : un relais qui vise encore ce groupe devient insensible. Les GPropertyAction (sans « enabled » modifiable)
	 * sont retirées : elles gardent leur objet en vie et resteraient actives. */
	names = g_action_group_list_actions(G_ACTION_GROUP(s->actions));
	for(gchar **n = names; *n != NULL; n++)
	{
		GAction *a = g_action_map_lookup_action(G_ACTION_MAP(s->actions), *n);

		if(G_IS_SIMPLE_ACTION(a))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), FALSE);
		else
			g_action_map_remove_action(G_ACTION_MAP(s->actions), *n);
	}
	g_strfreev(names);
	gt_link_close(s->link);
	g_signal_emit(s, signals[SIGNAL_CLOSED], 0);
}

static void gt_session_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtSession *s = GT_SESSION(object);

	switch(prop_id)
	{
	case PROP_SHORT_NAME:
		g_value_set_string(value, s->short_name);
		break;
	case PROP_STATUS_TEXT:
		g_value_set_string(value, gt_link_get_status_text(s->link));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_session_dispose(GObject *object)
{
	GtSession *s = GT_SESSION(object);

	/* La liaison n'est rendue qu'à la finalisation : toujours présente ici, même si dispose est appelé plusieurs fois */
	gt_session_shutdown(s);
	g_signal_handlers_disconnect_by_data(s->link, s);
	g_clear_object(&s->actions);
	G_OBJECT_CLASS(gt_session_parent_class)->dispose(object);
}

static void gt_session_finalize(GObject *object)
{
	GtSession *s = GT_SESSION(object);

	gt_script_engine_free(s->engine);	/* avant les macros qu'il emprunte */
	gt_macros_free(s->macros);
	g_clear_object(&s->link);
	g_clear_object(&s->options);
	gt_profile_clear(&s->profile);
	g_free(s->profile_name);
	g_free(s->short_name);
	G_OBJECT_CLASS(gt_session_parent_class)->finalize(object);
}

static void gt_session_class_init(GtSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = gt_session_get_property;
	object_class->dispose = gt_session_dispose;
	object_class->finalize = gt_session_finalize;
	props[PROP_SHORT_NAME] = g_param_spec_string("short-name", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_STATUS_TEXT] = g_param_spec_string("status-text", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(object_class, N_PROPS, props);
	signals[SIGNAL_CLOSED] = g_signal_new("closed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void gt_session_init(GtSession *s)
{
}
