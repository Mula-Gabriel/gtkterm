/* Session d'onglet (sous-projet 6b, spec §4.2) : voir session.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "app_ui.h"
#include "session.h"

struct _GtSession
{
	GObject parent_instance;
	GtPortActivity *activity;
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
	gboolean in_project;
	gchar *script_text;	/* jamais NULL */
};

enum { PROP_0, PROP_SHORT_NAME, PROP_STATUS_TEXT, PROP_IN_PROJECT, PROP_SCRIPT_TEXT, N_PROPS };
enum { SIGNAL_CLOSED, SIGNAL_EDITED, N_SIGNALS };
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

static void on_detach_macros_template(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_macros_detach_template(GT_SESSION(user_data)->macros);
}

/* « Détacher du modèle » activée seulement quand l'onglet est lié ; jamais réactivée après l'arrêt */
static void update_detach_action(GtSession *s)
{
	if(!s->shut_down)
		set_action_enabled(s, "detach-macros-template", gt_macros_get_template(s->macros) != NULL);
}

/* Rappel « edited » des macros : l'état lié a pu changer (lier, détacher, ouverture), puis signal edited de la session */
static void on_macros_edited(gpointer data)
{
	GtSession *s = data;

	update_detach_action(s);
	gt_session_edited(s);
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
		{ .name = "detach-macros-template", .activate = on_detach_macros_template },
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
	update_detach_action(s);
}

static GtPortActivityLink activity_link_state(GtkTermLink *link)
{
	switch(gt_link_get_state(link))
	{
	case GT_LINK_CONNECTING: return GT_PORT_LINK_CONNECTING;
	case GT_LINK_OPEN: return GT_PORT_LINK_OPEN;
	case GT_LINK_LISTENING: return GT_PORT_LINK_LISTENING;
	case GT_LINK_LOST: return GT_PORT_LINK_LOST;
	case GT_LINK_RECONNECTING: return GT_PORT_LINK_RECONNECTING;
	default: return GT_PORT_LINK_CLOSED;
	}
}

/* notify::state de la liaison (peut tourner sous la surveillance du transport : ne fait que mettre à jour le modèle) */
static void on_link_state_activity(GObject *link, GParamSpec *pspec, gpointer user_data)
{
	GtSession *s = user_data;

	gt_port_activity_set_link_state(s->activity, activity_link_state(s->link), g_get_monotonic_time());
}

/* received de la liaison : même contrainte de réentrance */
static void on_link_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	GtSession *s = user_data;

	gt_port_activity_received(s->activity, g_get_monotonic_time());
}

static void apply_activity_timeouts(GtSession *s)
{
	gt_port_activity_set_timeouts(s->activity, (guint)MAX(s->profile.idle_timeout_s, 0), (guint)MAX(s->profile.no_data_timeout_s, 0),
	                              g_get_monotonic_time());
}

GtPortActivity *gt_session_get_activity(GtSession *s)
{
	return s->activity;
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
	/* Suivi « modifié » (plan, décision 2) : un seul canal, le signal edited ; déconnecté à l'arrêt */
	g_signal_connect_swapped(s->options, "changed", G_CALLBACK(gt_session_edited), s);
	g_signal_connect_swapped(s->link, "notify::auto-reconnect", G_CALLBACK(gt_session_edited), s);
	gt_macros_set_edited_func(s->macros, on_macros_edited, s);
	install_actions(s);
	s->activity = gt_port_activity_new();
	apply_activity_timeouts(s);
	g_signal_connect(s->link, "notify::state", G_CALLBACK(on_link_state_activity), s);
	g_signal_connect(s->link, "received", G_CALLBACK(on_link_received), s);
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
	apply_activity_timeouts(s);
	gt_session_edited(s);
}

void gt_session_open_port(GtSession *s)
{
	TransportParams params;

	if(s->shut_down)
		return;
	update_short_name(s);
	apply_activity_timeouts(s);
	gt_port_activity_reset(s->activity, g_get_monotonic_time());
	gt_profile_to_transport_params(&s->profile, &params);
	gt_link_open(s->link, &params, NULL);	/* échec : open-failed de la liaison (l'interface affiche le message) */
	transport_params_clear(&params);
	gt_session_edited(s);
}

gboolean gt_session_get_in_project(GtSession *s)
{
	return s->in_project;
}

void gt_session_set_in_project(GtSession *s, gboolean in_project)
{
	in_project = in_project != FALSE;
	if(in_project == s->in_project)
		return;
	s->in_project = in_project;
	gt_macros_set_embedded(s->macros, in_project);
	g_object_notify_by_pspec(G_OBJECT(s), props[PROP_IN_PROJECT]);
	gt_session_edited(s);
}

const gchar *gt_session_get_script_text(GtSession *s)
{
	return s->script_text;
}

void gt_session_set_script_text(GtSession *s, const gchar *text)
{
	if(text == NULL)
		text = "";
	if(g_strcmp0(text, s->script_text) == 0)
		return;
	g_free(s->script_text);
	s->script_text = g_strdup(text);
	g_object_notify_by_pspec(G_OBJECT(s), props[PROP_SCRIPT_TEXT]);
	gt_session_edited(s);
}

void gt_session_edited(GtSession *s)
{
	if(!s->shut_down)
		g_signal_emit(s, signals[SIGNAL_EDITED], 0);
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
	g_signal_handlers_disconnect_by_func(s->options, G_CALLBACK(gt_session_edited), s);
	g_signal_handlers_disconnect_by_func(s->link, G_CALLBACK(gt_session_edited), s);
	gt_macros_set_edited_func(s->macros, NULL, NULL);
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
	case PROP_IN_PROJECT:
		g_value_set_boolean(value, s->in_project);
		break;
	case PROP_SCRIPT_TEXT:
		g_value_set_string(value, s->script_text);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_session_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtSession *s = GT_SESSION(object);

	switch(prop_id)
	{
	case PROP_IN_PROJECT:
		gt_session_set_in_project(s, g_value_get_boolean(value));
		break;
	case PROP_SCRIPT_TEXT:
		gt_session_set_script_text(s, g_value_get_string(value));
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
	g_clear_object(&s->activity);
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
	g_free(s->script_text);
	G_OBJECT_CLASS(gt_session_parent_class)->finalize(object);
}

static void gt_session_class_init(GtSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = gt_session_get_property;
	object_class->set_property = gt_session_set_property;
	object_class->dispose = gt_session_dispose;
	object_class->finalize = gt_session_finalize;
	props[PROP_SHORT_NAME] = g_param_spec_string("short-name", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_STATUS_TEXT] = g_param_spec_string("status-text", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_IN_PROJECT] = g_param_spec_boolean("in-project", NULL, NULL, FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	props[PROP_SCRIPT_TEXT] = g_param_spec_string("script-text", NULL, NULL, "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(object_class, N_PROPS, props);
	signals[SIGNAL_CLOSED] = g_signal_new("closed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_EDITED] = g_signal_new("edited", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void gt_session_init(GtSession *s)
{
	s->script_text = g_strdup("");
}
