/***********************************************************************/
/* term_options.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      État unique des options du terminal (sous-projet 6a)           */
/*                                                                     */
/***********************************************************************/

#include "term_options.h"

enum
{
	PROP_0,
	PROP_ECHO,
	PROP_CRLF_AUTO,
	PROP_TIMESTAMP,
	PROP_ESC_CLEAR_SCREEN,
	PROP_HEX_SHOW_INDEX,
	PROP_VIEW_MODE,
	PROP_HEX_BYTES_PER_LINE,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];

struct _GtTermOptions
{
	GObject parent_instance;
	gboolean echo;
	gboolean crlf_auto;
	gboolean timestamp;
	gboolean esc_clear_screen;
	gboolean hex_show_index;
	GtViewMode view_mode;
	guint hex_bytes_per_line;
	gboolean changed_pending;	/* modification effective dont le « notify » n'a pas encore été distribué */
};

G_DEFINE_FINAL_TYPE(GtTermOptions, gt_term_options, G_TYPE_OBJECT)

GType gt_view_mode_get_type(void)
{
	static gsize type_id = 0;

	if(g_once_init_enter(&type_id))
	{
		static const GEnumValue values[] =
		{
			{ GT_VIEW_ASCII, "GT_VIEW_ASCII", "ascii" },
			{ GT_VIEW_HEX, "GT_VIEW_HEX", "hex" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static(g_intern_static_string("GtViewMode"), values);

		g_once_init_leave(&type_id, t);
	}
	return (GType)type_id;
}

static gboolean bytes_per_line_valid(guint v)
{
	return v == 8 || v == 10 || v == 16 || v == 24 || v == 32;
}

/* Une modification effective : « changed » est dû, puis « notify » de la propriété (mis en file par GObject pendant un
 * g_object_set ou un freeze_notify, distribué tout de suite sinon) ; « changed » part de dispatch_properties_changed. */
static void mark_changed(GtTermOptions *o, guint prop_id)
{
	o->changed_pending = TRUE;
	g_object_notify_by_pspec(G_OBJECT(o), props[prop_id]);
}

/* GObject appelle dispatch_properties_changed une fois par distribution des « notify » (fin de g_object_set, dégel, notify hors
 * gel) — y compris quand aucun gestionnaire « notify » n'est connecté (vérifié avec GLib 2.80). On émet « changed » après les
 * « notify » de la classe parente. Le drapeau est relevé avant : une modification faite par un gestionnaire « notify » relance
 * sa propre distribution, donc son propre « changed », sans en perdre ni en doubler. */
static void gt_term_options_dispatch_properties_changed(GObject *object, guint n_pspecs, GParamSpec **pspecs)
{
	GtTermOptions *o = GT_TERM_OPTIONS(object);
	gboolean pending = o->changed_pending;

	o->changed_pending = FALSE;
	G_OBJECT_CLASS(gt_term_options_parent_class)->dispatch_properties_changed(object, n_pspecs, pspecs);
	if(pending)
		g_signal_emit(o, signals[SIGNAL_CHANGED], 0);
}

static void set_flag(GtTermOptions *o, gboolean *field, gboolean value, guint prop_id)
{
	value = value ? TRUE : FALSE;
	if(*field == value)
		return;
	*field = value;
	mark_changed(o, prop_id);
}

static void gt_term_options_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtTermOptions *o = GT_TERM_OPTIONS(object);

	switch(prop_id)
	{
		case PROP_ECHO:
			set_flag(o, &o->echo, g_value_get_boolean(value), prop_id);
			break;
		case PROP_CRLF_AUTO:
			set_flag(o, &o->crlf_auto, g_value_get_boolean(value), prop_id);
			break;
		case PROP_TIMESTAMP:
			set_flag(o, &o->timestamp, g_value_get_boolean(value), prop_id);
			break;
		case PROP_ESC_CLEAR_SCREEN:
			set_flag(o, &o->esc_clear_screen, g_value_get_boolean(value), prop_id);
			break;
		case PROP_HEX_SHOW_INDEX:
			set_flag(o, &o->hex_show_index, g_value_get_boolean(value), prop_id);
			break;
		case PROP_VIEW_MODE:
		{
			GtViewMode mode = (GtViewMode)g_value_get_enum(value);

			if(o->view_mode != mode)
			{
				o->view_mode = mode;
				mark_changed(o, prop_id);
			}
			break;
		}
		case PROP_HEX_BYTES_PER_LINE:
		{
			guint v = g_value_get_uint(value);

			if(!bytes_per_line_valid(v))
			{
				g_warning("hex-bytes-per-line: %u refused (8, 10, 16, 24 or 32)", v);
				break;
			}
			if(o->hex_bytes_per_line != v)
			{
				o->hex_bytes_per_line = v;
				mark_changed(o, prop_id);
			}
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_term_options_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtTermOptions *o = GT_TERM_OPTIONS(object);

	switch(prop_id)
	{
		case PROP_ECHO:
			g_value_set_boolean(value, o->echo);
			break;
		case PROP_CRLF_AUTO:
			g_value_set_boolean(value, o->crlf_auto);
			break;
		case PROP_TIMESTAMP:
			g_value_set_boolean(value, o->timestamp);
			break;
		case PROP_ESC_CLEAR_SCREEN:
			g_value_set_boolean(value, o->esc_clear_screen);
			break;
		case PROP_HEX_SHOW_INDEX:
			g_value_set_boolean(value, o->hex_show_index);
			break;
		case PROP_VIEW_MODE:
			g_value_set_enum(value, o->view_mode);
			break;
		case PROP_HEX_BYTES_PER_LINE:
			g_value_set_uint(value, o->hex_bytes_per_line);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_term_options_class_init(GtTermOptionsClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);
	/* EXPLICIT_NOTIFY : « notify » n'est émis que par mark_changed, donc seulement sur un vrai changement */
	GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	oc->set_property = gt_term_options_set_property;
	oc->get_property = gt_term_options_get_property;
	oc->dispatch_properties_changed = gt_term_options_dispatch_properties_changed;

	props[PROP_ECHO] = g_param_spec_boolean("echo", NULL, NULL, FALSE, flags);
	props[PROP_CRLF_AUTO] = g_param_spec_boolean("crlf-auto", NULL, NULL, FALSE, flags);
	props[PROP_TIMESTAMP] = g_param_spec_boolean("timestamp", NULL, NULL, FALSE, flags);
	props[PROP_ESC_CLEAR_SCREEN] = g_param_spec_boolean("esc-clear-screen", NULL, NULL, FALSE, flags);
	props[PROP_HEX_SHOW_INDEX] = g_param_spec_boolean("hex-show-index", NULL, NULL, FALSE, flags);
	props[PROP_VIEW_MODE] = g_param_spec_enum("view-mode", NULL, NULL, GT_TYPE_VIEW_MODE, GT_VIEW_ASCII, flags);
	/* Bornes larges : le refus d'une valeur hors de {8, 10, 16, 24, 32} est fait par set_property, avec un seul message */
	props[PROP_HEX_BYTES_PER_LINE] = g_param_spec_uint("hex-bytes-per-line", NULL, NULL, 0, G_MAXUINT, 16, flags);
	g_object_class_install_properties(oc, N_PROPS, props);

	signals[SIGNAL_CHANGED] = g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void gt_term_options_init(GtTermOptions *o)
{
	o->view_mode = GT_VIEW_ASCII;
	o->hex_bytes_per_line = 16;
}

GtTermOptions *gt_term_options_new(void)
{
	return g_object_new(GT_TYPE_TERM_OPTIONS, NULL);
}

void gt_term_options_apply_profile(GtTermOptions *o, const GtProfile *p)
{
	g_return_if_fail(GT_IS_TERM_OPTIONS(o));
	g_return_if_fail(p != NULL);

	/* Un seul g_object_set : une seule distribution des « notify », donc un seul « changed », après eux */
	g_object_set(o, "echo", p->echo, "crlf-auto", p->crlf_auto, "timestamp", p->timestamp, "esc-clear-screen", p->esc_clear_screen, NULL);
}

void gt_term_options_store_profile(GtTermOptions *o, GtProfile *p)
{
	g_return_if_fail(GT_IS_TERM_OPTIONS(o));
	g_return_if_fail(p != NULL);

	p->echo = o->echo;
	p->crlf_auto = o->crlf_auto;
	p->timestamp = o->timestamp;
	p->esc_clear_screen = o->esc_clear_screen;
}

void gt_term_options_get_view(GtTermOptions *o, GtViewOptions *out)
{
	g_return_if_fail(GT_IS_TERM_OPTIONS(o));
	g_return_if_fail(out != NULL);

	out->mode = o->view_mode;
	out->echo = o->echo;
	out->crlf_auto = o->crlf_auto;
	out->timestamp = o->timestamp;
	out->bytes_per_line = o->hex_bytes_per_line;
	out->show_index = o->hex_show_index;
}

void gt_term_options_get_rx(GtTermOptions *o, gboolean *crlf_auto, gboolean *timestamp)
{
	g_return_if_fail(GT_IS_TERM_OPTIONS(o));

	if(crlf_auto != NULL)
		*crlf_auto = o->crlf_auto;
	if(timestamp != NULL)
		*timestamp = o->timestamp;
}

gboolean gt_term_options_get_esc_clear_screen(GtTermOptions *o)
{
	g_return_val_if_fail(GT_IS_TERM_OPTIONS(o), FALSE);

	return o->esc_clear_screen;
}
