/* Crochet de la suite graphique GTK 4 : voir test_hooks.h. Repris de l'essai de faisabilité du 2026-10-09 (spike.c).
 *
 * test-dump : activer l'action recalcule son état, que le harnais relit par org.gtk.Actions.Describe (l'activation D-Bus et le
 * Describe qui la suit sont traités dans l'ordre). Une entrée par widget des fenêtres de premier niveau visibles (popovers compris,
 * enfants de leur widget parent en GTK 4), en profondeur d'abord :
 *   id (s, adresse), depth (i), native (i, rang du widget natif — fenêtre ou popover — qui porte la surface, -1 avant le premier),
 *   type (s), role (s, GtkAccessibleRole), label (s, si non vide), name (s, si posé par gtk_widget_set_name), title (s, fenêtres),
 *   css (as), visible, mapped, sensitive, focus, selected (b), active (b, widgets à propriété « active » booléenne), modal (b, fenêtres),
 *   x, y, w, h (i, en coordonnées de la surface du natif, si mappé), natural_w (i, si mappé : largeur naturelle mesurée par gtk_widget_measure, indépendante de
 *   l'allocation), surface_w, surface_h (i, natifs), popup_x, popup_y (i, popovers),
 *   frame (i, natifs : compteur d'images de l'horloge de la surface ; une géométrie lue avant l'image suivante peut être périmée),
 *   placeholder (s, GtkEntry : texte indicatif, si non vide), tooltip (s, bulle d'aide, si non vide),
 *   markup (s, GtkLabel à balisage Pango : texte balisé, si non vide). */
#include <string.h>
#include <vte/vte.h>

#include "app.h"
#include "link.h"
#include "macros.h"
#include "session_view.h"
#include "terminal_view.h"
#include "test_hooks.h"

gboolean gt_test_hooks_enabled(void)
{
	return g_strcmp0(g_getenv("GTKTERM_TEST_HOOKS"), "1") == 0;
}

/* Libellé sans soulignés de mnémonique : « _OK » → « OK », « __ » → « _ » */
static gchar *strip_mnemonic(const gchar *s)
{
	GString *out = g_string_new(NULL);

	for(const gchar *p = s; *p != '\0'; p++)
	{
		if(*p == '_')
		{
			if(p[1] == '_')
				g_string_append_c(out, *++p);
			continue;
		}
		g_string_append_c(out, *p);
	}
	return g_string_free(out, FALSE);
}

/* Texte affiché par le widget (à libérer), NULL s'il n'en a pas */
static gchar *widget_label(GtkWidget *w)
{
	static const char *props[] = { "label", "text", "title" };
	gchar *s = NULL;

	if(GTK_IS_TEXT_VIEW(w))	/* éditeur et console du panneau de script (GtkSourceView) */
	{
		GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
		GtkTextIter start, end;

		gtk_text_buffer_get_bounds(b, &start, &end);
		return gtk_text_buffer_get_text(b, &start, &end, FALSE);
	}
	if(GTK_IS_COLOR_DIALOG_BUTTON(w))	/* « rgb(r,g,b) » ; boutons du dialogue Terminal et de la fenêtre des couleurs du script */
		return gdk_rgba_to_string(gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(w)));
	if(GTK_IS_LABEL(w))
		return g_strdup(gtk_label_get_text(GTK_LABEL(w)));
	if(GTK_IS_EDITABLE(w))
		return g_strdup(gtk_editable_get_text(GTK_EDITABLE(w)));
	if(GTK_IS_BUTTON(w) && gtk_button_get_label(GTK_BUTTON(w)) != NULL)
	{
		if(gtk_button_get_use_underline(GTK_BUTTON(w)))
			return strip_mnemonic(gtk_button_get_label(GTK_BUTTON(w)));
		return g_strdup(gtk_button_get_label(GTK_BUTTON(w)));
	}
	for(guint i = 0; i < G_N_ELEMENTS(props) && s == NULL; i++)
	{
		GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(w), props[i]);

		if(ps != NULL && ps->value_type == G_TYPE_STRING)
		{
			g_object_get(w, props[i], &s, NULL);
			if(s != NULL && *s == '\0')
				g_clear_pointer(&s, g_free);
		}
	}
	return s;
}

/* Nom posé par gtk_widget_set_name ; NULL si c'est le nom de type (valeur par défaut) */
static const gchar *widget_name(GtkWidget *w)
{
	const gchar *n = gtk_widget_get_name(w);

	return g_strcmp0(n, G_OBJECT_TYPE_NAME(w)) == 0 ? NULL : n;
}

static void add_int(GVariantBuilder *d, const gchar *key, gint v)
{
	g_variant_builder_add(d, "{sv}", key, g_variant_new_int32(v));
}

static void add_bool(GVariantBuilder *d, const gchar *key, gboolean v)
{
	g_variant_builder_add(d, "{sv}", key, g_variant_new_boolean(v));
}

static void dump_widget(GtkWidget *w, GtkNative *nat, gint nat_idx, gint *n_natives, gint depth, GVariantBuilder *b)
{
	GVariantBuilder d;
	GEnumValue *role;
	GParamSpec *active;
	graphene_rect_t r;
	gchar *label;
	const gchar *tooltip;
	gchar **css;

	if(GTK_IS_NATIVE(w) && GTK_NATIVE(w) != nat)
	{
		nat = GTK_NATIVE(w);
		nat_idx = (*n_natives)++;
	}
	g_variant_builder_init(&d, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&d, "{sv}", "id", g_variant_new_take_string(g_strdup_printf("%p", (void *)w)));
	add_int(&d, "depth", depth);
	add_int(&d, "native", nat_idx);
	g_variant_builder_add(&d, "{sv}", "type", g_variant_new_string(G_OBJECT_TYPE_NAME(w)));
	role = g_enum_get_value(g_type_class_peek(GTK_TYPE_ACCESSIBLE_ROLE), gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(w)));
	g_variant_builder_add(&d, "{sv}", "role", g_variant_new_string(role != NULL ? role->value_nick : "?"));
	label = widget_label(w);
	if(label != NULL)
		g_variant_builder_add(&d, "{sv}", "label", g_variant_new_take_string(label));
	if(widget_name(w) != NULL)
		g_variant_builder_add(&d, "{sv}", "name", g_variant_new_string(widget_name(w)));
	if(GTK_IS_WINDOW(w))
	{
		const gchar *title = gtk_window_get_title(GTK_WINDOW(w));

		g_variant_builder_add(&d, "{sv}", "title", g_variant_new_string(title != NULL ? title : ""));
		add_bool(&d, "modal", gtk_window_get_modal(GTK_WINDOW(w)));
	}
	if(GTK_IS_ENTRY(w))
	{
		const gchar *placeholder = gtk_entry_get_placeholder_text(GTK_ENTRY(w));

		if(placeholder != NULL && placeholder[0] != '\0')
			g_variant_builder_add(&d, "{sv}", "placeholder", g_variant_new_string(placeholder));
	}
	if(GTK_IS_LABEL(w) && gtk_label_get_use_markup(GTK_LABEL(w)))
	{
		const gchar *markup = gtk_label_get_label(GTK_LABEL(w));

		if(markup != NULL && markup[0] != '\0')
			g_variant_builder_add(&d, "{sv}", "markup", g_variant_new_string(markup));
	}
	tooltip = gtk_widget_get_tooltip_text(w);
	if(tooltip != NULL && tooltip[0] != '\0')
		g_variant_builder_add(&d, "{sv}", "tooltip", g_variant_new_string(tooltip));
	css = gtk_widget_get_css_classes(w);
	g_variant_builder_add(&d, "{sv}", "css", g_variant_new_strv((const gchar * const *)css, -1));
	g_strfreev(css);
	add_bool(&d, "visible", gtk_widget_get_visible(w));
	add_bool(&d, "mapped", gtk_widget_get_mapped(w));
	add_bool(&d, "sensitive", gtk_widget_is_sensitive(w));
	add_bool(&d, "focus", gtk_widget_has_focus(w));
	add_bool(&d, "selected", (gtk_widget_get_state_flags(w) & GTK_STATE_FLAG_SELECTED) != 0);
	active = g_object_class_find_property(G_OBJECT_GET_CLASS(w), "active");
	if(active != NULL && active->value_type == G_TYPE_BOOLEAN)
	{
		gboolean on = FALSE;

		g_object_get(w, "active", &on, NULL);
		add_bool(&d, "active", on);
	}
	if(nat != NULL && gtk_widget_get_mapped(w) && gtk_widget_compute_bounds(w, GTK_WIDGET(nat), &r))
	{
		double sx, sy;

		/* Origine du widget natif dans sa surface (ombres et marges des décorations CSD) */
		gtk_native_get_surface_transform(nat, &sx, &sy);
		add_int(&d, "x", (gint)(r.origin.x + sx));
		add_int(&d, "y", (gint)(r.origin.y + sy));
		add_int(&d, "w", (gint)r.size.width);
		add_int(&d, "h", (gint)r.size.height);
	}
	if(gtk_widget_get_mapped(w))
	{
		gint nat_w = 0;

		gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &nat_w, NULL, NULL);
		add_int(&d, "natural_w", nat_w);
	}
	if(GTK_IS_NATIVE(w) && gtk_native_get_surface(GTK_NATIVE(w)) != NULL)
	{
		GdkSurface *s = gtk_native_get_surface(GTK_NATIVE(w));

		add_int(&d, "surface_w", gdk_surface_get_width(s));
		add_int(&d, "surface_h", gdk_surface_get_height(s));
		if(gdk_surface_get_frame_clock(s) != NULL)
			add_int(&d, "frame", (gint)gdk_frame_clock_get_frame_counter(gdk_surface_get_frame_clock(s)));
		if(GDK_IS_POPUP(s))
		{
			add_int(&d, "popup_x", gdk_popup_get_position_x(GDK_POPUP(s)));
			add_int(&d, "popup_y", gdk_popup_get_position_y(GDK_POPUP(s)));
		}
	}
	g_variant_builder_add(b, "a{sv}", &d);
	for(GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c))
		dump_widget(c, nat, nat_idx, n_natives, depth + 1, b);
}

static GVariant *build_dump(void)
{
	GListModel *tops = gtk_window_get_toplevels();
	GVariantBuilder b;
	gint n_natives = 0;

	g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));
	for(guint i = 0; i < g_list_model_get_n_items(tops); i++)
	{
		GtkWidget *w = g_list_model_get_item(tops, i);

		if(gtk_widget_get_visible(w))
			dump_widget(w, NULL, -1, &n_natives, 0, &b);
		g_object_unref(w);
	}
	return g_variant_builder_end(&b);
}

/* Premier widget mappé dont l'identifiant (%p du dump), puis le nom, puis le libellé vaut key */
typedef enum { MATCH_ID, MATCH_NAME, MATCH_LABEL } MatchKind;

static gboolean widget_matches(GtkWidget *w, const gchar *key, MatchKind kind)
{
	gchar id[32];
	gchar *label;
	gboolean ok;

	switch(kind)
	{
		case MATCH_ID:
			g_snprintf(id, sizeof(id), "%p", (void *)w);
			return strcmp(id, key) == 0;
		case MATCH_NAME:
			return g_strcmp0(widget_name(w), key) == 0;
		default:
			label = widget_label(w);
			ok = g_strcmp0(label, key) == 0;
			g_free(label);
			return ok;
	}
}

static GtkWidget *find_in(GtkWidget *w, const gchar *key, MatchKind kind)
{
	if(gtk_widget_get_mapped(w) && widget_matches(w, key, kind))
		return w;
	for(GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c))
	{
		GtkWidget *r = find_in(c, key, kind);

		if(r != NULL)
			return r;
	}
	return NULL;
}

static GtkWidget *find_any(const gchar *key)
{
	GListModel *tops = gtk_window_get_toplevels();
	GtkWidget *res = NULL;

	for(MatchKind kind = MATCH_ID; kind <= MATCH_LABEL && res == NULL; kind++)
		for(guint i = 0; i < g_list_model_get_n_items(tops) && res == NULL; i++)
		{
			GtkWidget *w = g_list_model_get_item(tops, i);

			if(gtk_widget_get_visible(w))
				res = find_in(w, key, kind);
			g_object_unref(w);
		}
	return res;
}

/* Demande une image à l'horloge de chaque surface visible : au repos, rien ne se redessine et le compteur « frame » resterait figé ; le harnais
 * (settled de broadway.py) attend qu'il avance pour lire une géométrie postérieure à une mise en page. */
static void request_frames(void)
{
	GListModel *tops = gtk_window_get_toplevels();

	for(guint i = 0; i < g_list_model_get_n_items(tops); i++)
	{
		GtkWidget *w = g_list_model_get_item(tops, i);
		GdkSurface *s = gtk_widget_get_mapped(w) ? gtk_native_get_surface(GTK_NATIVE(w)) : NULL;

		if(s != NULL && gdk_surface_get_frame_clock(s) != NULL)
			gdk_frame_clock_request_phase(gdk_surface_get_frame_clock(s), GDK_FRAME_CLOCK_PHASE_PAINT);
		g_object_unref(w);
	}
}

static void on_test_dump(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	request_frames();
	g_simple_action_set_state(action, build_dump());
}

static void on_test_terminal_text(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtkWidget *view = gt_app_get_active_view();	/* terminal de l'onglet actif */
	gchar *text = NULL;

	if(view != NULL)
		text = vte_terminal_get_text_format(gt_terminal_view_get_vte(gt_session_view_get_terminal_view(view)), VTE_FORMAT_TEXT);
	g_simple_action_set_state(action, g_variant_new_take_string(text != NULL ? text : g_strdup("")));
}

static const char *cursor_shape_name(VteCursorShape shape)
{
	switch(shape)
	{
	case VTE_CURSOR_SHAPE_BLOCK:
		return "block";
	case VTE_CURSOR_SHAPE_IBEAM:
		return "ibeam";
	case VTE_CURSOR_SHAPE_UNDERLINE:
		return "underline";
	}
	return "?";
}

/* test-terminal-props : état a{sv} recalculé à chaque activation (Activate puis Describe, comme test-dump) */
static void on_test_terminal_props(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtkWidget *page = gt_app_get_active_view();	/* terminal de l'onglet actif */
	GVariantBuilder b;

	g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
	if(page != NULL)
	{
		GtkWidget *view = gt_session_view_get_terminal_view(page);
		VteTerminal *vte = gt_terminal_view_get_vte(view);
		const PangoFontDescription *font = vte_terminal_get_font(vte);
		GdkRGBA bg, fg;

		if(font != NULL)
			g_variant_builder_add(&b, "{sv}", "font", g_variant_new_take_string(pango_font_description_to_string(font)));
		/* Par la fonction, pas par la propriété « scrollback-lines » : VTE 0.84 rend toujours G_MAXUINT pour la propriété */
		g_variant_builder_add(&b, "{sv}", "scrollback", g_variant_new_int64(vte_terminal_get_scrollback_lines(vte)));
		g_variant_builder_add(&b, "{sv}", "cursor", g_variant_new_string(cursor_shape_name(vte_terminal_get_cursor_shape(vte))));
		vte_terminal_get_color_background_for_draw(vte, &bg);
		g_variant_builder_add(&b, "{sv}", "background", g_variant_new_take_string(gdk_rgba_to_string(&bg)));
		if(gt_terminal_view_get_foreground(view, &fg))
			g_variant_builder_add(&b, "{sv}", "foreground", g_variant_new_take_string(gdk_rgba_to_string(&fg)));
	}
	g_simple_action_set_state(action, g_variant_builder_end(&b));
}

/* test-sessions (plan, décision 9) : état aa{sv} recalculé à chaque activation (Activate puis Describe, comme test-dump) ; un dict par onglet,
 * dans l'ordre : index (i), id (u), name (s, nom court), state (s, pseudo de GtLinkState), profile (s), macros_file (s, vide si aucun),
 * active (b), busy (b), in_project (b), script (s, texte de l'éditeur de script tenu par la session), activity (s, pseudo de
 * GtPortActivityState), unread (b), template (s, chemin du modèle de macros lié, "" si non lié) */
static void on_test_sessions(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSessionManager *m = gt_app_get_sessions();
	GtSession *active = gt_app_get_active_session();
	GEnumClass *states = g_type_class_ref(GT_TYPE_LINK_STATE);
	GVariantBuilder b;

	g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));
	for(guint i = 0; m != NULL && i < gt_session_manager_get_n(m); i++)
	{
		GtSession *s = gt_session_manager_get_nth(m, i);
		GEnumValue *st = g_enum_get_value(states, gt_link_get_state(gt_session_get_link(s)));
		const gchar *macros = gt_macros_get_path(gt_session_get_macros(s));
		const gchar *tpl;
		GVariantBuilder d;

		g_variant_builder_init(&d, G_VARIANT_TYPE_VARDICT);
		g_variant_builder_add(&d, "{sv}", "index", g_variant_new_int32((gint32)i));
		g_variant_builder_add(&d, "{sv}", "id", g_variant_new_uint32(gt_session_get_id(s)));
		g_variant_builder_add(&d, "{sv}", "name", g_variant_new_string(gt_session_get_short_name(s) != NULL ? gt_session_get_short_name(s) : ""));
		g_variant_builder_add(&d, "{sv}", "state", g_variant_new_string(st != NULL ? st->value_nick : "?"));
		g_variant_builder_add(&d, "{sv}", "profile", g_variant_new_string(gt_session_get_profile_name(s)));
		g_variant_builder_add(&d, "{sv}", "macros_file", g_variant_new_string(macros != NULL ? macros : ""));
		g_variant_builder_add(&d, "{sv}", "active", g_variant_new_boolean(s == active));
		g_variant_builder_add(&d, "{sv}", "busy", g_variant_new_boolean(gt_session_is_busy(s)));
		g_variant_builder_add(&d, "{sv}", "in_project", g_variant_new_boolean(gt_session_get_in_project(s)));
		g_variant_builder_add(&d, "{sv}", "script", g_variant_new_string(gt_session_get_script_text(s)));
		g_variant_builder_add(&d, "{sv}", "activity",
		                      g_variant_new_string(gt_port_activity_state_nick(gt_port_activity_get_state(gt_session_get_activity(s)))));
		g_variant_builder_add(&d, "{sv}", "unread", g_variant_new_boolean(gt_port_activity_get_unread(gt_session_get_activity(s))));
		tpl = gt_macros_get_template(gt_session_get_macros(s));
		g_variant_builder_add(&d, "{sv}", "template", g_variant_new_string(tpl != NULL ? tpl : ""));
		g_variant_builder_add_value(&b, g_variant_builder_end(&d));
	}
	g_type_class_unref(states);
	g_simple_action_set_state(action, g_variant_builder_end(&b));
}

/* test-project (plan, décision 13) : état a{sv} recalculé à chaque activation : open (b), path (s, vide sans fichier), modified (b), name (s) */
static void on_test_project(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtProject *p = gt_app_get_project();
	GVariantBuilder b;

	g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
	if(p != NULL)
	{
		const gchar *path = gt_project_get_path(p);

		g_variant_builder_add(&b, "{sv}", "open", g_variant_new_boolean(gt_project_is_open(p)));
		g_variant_builder_add(&b, "{sv}", "path", g_variant_new_string(path != NULL ? path : ""));
		g_variant_builder_add(&b, "{sv}", "modified", g_variant_new_boolean(gt_project_is_modified(p)));
		g_variant_builder_add(&b, "{sv}", "name", g_variant_new_take_string(gt_project_get_name(p)));
	}
	g_simple_action_set_state(action, g_variant_builder_end(&b));
}

/* Clic programmé, synchrone : traité avant la réponse D-Bus, ses effets sont visibles dès le retour de l'appel. Un GtkButton (bascules comprises)
 * reçoit « clicked » directement, s'il est sensible : gtk_widget_activate ne l'émettrait qu'après le délai d'activation de GTK (250 ms) et
 * « cliquerait » aussi un bouton insensible. Les autres widgets (case, entrée, élément de menu) passent par gtk_widget_activate. Un widget
 * introuvable, insensible ou non activable est signalé sur stderr par une ligne « GTKTERM-TEST: », qui fait échouer le scénario ; le harnais
 * vérifie la présence du widget dans le dump avant d'appeler. */
static void on_test_activate(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	const gchar *key = g_variant_get_string(param, NULL);
	GtkWidget *w = find_any(key);
	const gchar *problem = NULL;

	if(w == NULL)
		problem = "introuvable";
	else if(GTK_IS_BUTTON(w))
	{
		if(gtk_widget_is_sensitive(w))
			g_signal_emit_by_name(w, "clicked");
		else
			problem = "insensible, non activable";
	}
	else if(!gtk_widget_activate(w))
		problem = "non activable";
	if(problem != NULL)
		g_printerr("GTKTERM-TEST: test-activate %s : %s\n", key, problem);
}

/* test-set-text sur les widgets sans texte éditable simple (liste déroulante, GtkSpinButton, boutons de police et de couleur, GtkTextView) ;
 * TRUE si le widget a été traité (un GtkSpinButton est aussi un GtkEditable : ce traitement passe avant) */
static gboolean set_special_value(GtkWidget *w, const char *text)
{
	if(GTK_IS_DROP_DOWN(w) && GTK_IS_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(w))))
	{
		GtkStringList *list = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(w)));
		guint n = g_list_model_get_n_items(G_LIST_MODEL(list));

		for(guint i = 0; i < n; i++)
			if(g_strcmp0(gtk_string_list_get_string(list, i), text) == 0)
			{
				gtk_drop_down_set_selected(GTK_DROP_DOWN(w), i);
				return TRUE;
			}
		g_printerr("GTKTERM-TEST: test-set-text %s : « %s » absent de la liste\n", gtk_widget_get_name(w), text);
		return TRUE;
	}
	if(GTK_IS_SPIN_BUTTON(w))
	{
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), g_ascii_strtod(text, NULL));	/* bornée par l'ajustement, émet value-changed */
		return TRUE;
	}
	if(GTK_IS_FONT_DIALOG_BUTTON(w))
	{
		PangoFontDescription *desc = pango_font_description_from_string(text);

		gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(w), desc);
		pango_font_description_free(desc);
		return TRUE;
	}
	if(GTK_IS_COLOR_DIALOG_BUTTON(w))
	{
		GdkRGBA rgba;

		if(gdk_rgba_parse(&rgba, text))
			gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(w), &rgba);
		else
			g_printerr("GTKTERM-TEST: test-set-text %s : couleur illisible « %s »\n", gtk_widget_get_name(w), text);
		return TRUE;
	}
	if(GTK_IS_TEXT_VIEW(w))
	{
		gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)), text, -1);
		return TRUE;
	}
	return FALSE;
}

static void on_test_set_text(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	const gchar *key, *text;
	GtkWidget *w;

	g_variant_get(param, "(&s&s)", &key, &text);
	w = find_any(key);
	if(w != NULL && set_special_value(w, text))
		;	/* traité par set_special_value */
	else if(w != NULL && GTK_IS_EDITABLE(w))
		gtk_editable_set_text(GTK_EDITABLE(w), text);
	else
		g_printerr("GTKTERM-TEST: test-set-text %s : %s\n", key, w == NULL ? "introuvable" : "non éditable");
}

/* Fenêtre visible qui a le focus (« Configure Macros » n'est pas attachée à l'application), sinon la fenêtre active de l'application */
static GtkWindow *focused_window(void)
{
	GListModel *tops = gtk_window_get_toplevels();
	GtkApplication *app = GTK_APPLICATION(g_application_get_default());
	GtkWindow *found = NULL;

	for(guint i = 0; found == NULL && i < g_list_model_get_n_items(tops); i++)
	{
		GtkWindow *w = g_list_model_get_item(tops, i);

		if(gtk_widget_get_mapped(GTK_WIDGET(w)) && gtk_window_is_active(w))
			found = w;
		g_object_unref(w);
	}
	return found != NULL ? found : (app != NULL ? gtk_application_get_active_window(app) : NULL);
}

/* Capture PNG de la fenêtre qui a le focus (rendu Cairo hors écran) : pour regarder une disposition, aucun test ne la compare */
static void on_test_screenshot(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtkWindow *win = focused_window();
	const gchar *path = g_variant_get_string(param, NULL);
	GdkPaintable *paintable;
	GtkSnapshot *snapshot;
	GskRenderNode *node;
	GskRenderer *renderer;
	GError *error = NULL;

	if(win == NULL)
		return;
	paintable = gtk_widget_paintable_new(GTK_WIDGET(win));
	snapshot = gtk_snapshot_new();
	gdk_paintable_snapshot(paintable, snapshot, gtk_widget_get_width(GTK_WIDGET(win)), gtk_widget_get_height(GTK_WIDGET(win)));
	node = gtk_snapshot_free_to_node(snapshot);
	renderer = gsk_cairo_renderer_new();
	if(node != NULL && gsk_renderer_realize_for_display(renderer, gtk_widget_get_display(GTK_WIDGET(win)), &error))
	{
		GdkTexture *texture = gsk_renderer_render_texture(renderer, node, NULL);

		if(!gdk_texture_save_to_png(texture, path))
			g_printerr("GTKTERM-TEST: test-screenshot %s : échec\n", path);
		g_object_unref(texture);
		gsk_renderer_unrealize(renderer);
	}
	else if(error != NULL)
	{
		g_printerr("GTKTERM-TEST: test-screenshot : %s\n", error->message);
		g_error_free(error);
	}
	g_object_unref(renderer);
	if(node != NULL)
		gsk_render_node_unref(node);
	g_object_unref(paintable);
}

void gt_test_hooks_install(GtkApplication *app)
{
	GSimpleAction *a;

	a = g_simple_action_new_stateful("test-dump", NULL, g_variant_new_array(G_VARIANT_TYPE_VARDICT, NULL, 0));
	g_signal_connect(a, "activate", G_CALLBACK(on_test_dump), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new_stateful("test-terminal-text", NULL, g_variant_new_string(""));
	g_signal_connect(a, "activate", G_CALLBACK(on_test_terminal_text), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new_stateful("test-terminal-props", NULL, g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
	g_signal_connect(a, "activate", G_CALLBACK(on_test_terminal_props), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new_stateful("test-sessions", NULL, g_variant_new_array(G_VARIANT_TYPE_VARDICT, NULL, 0));
	g_signal_connect(a, "activate", G_CALLBACK(on_test_sessions), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new_stateful("test-project", NULL, g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
	g_signal_connect(a, "activate", G_CALLBACK(on_test_project), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new("test-activate", G_VARIANT_TYPE_STRING);
	g_signal_connect(a, "activate", G_CALLBACK(on_test_activate), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new("test-screenshot", G_VARIANT_TYPE_STRING);
	g_signal_connect(a, "activate", G_CALLBACK(on_test_screenshot), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
	a = g_simple_action_new("test-set-text", G_VARIANT_TYPE("(ss)"));
	g_signal_connect(a, "activate", G_CALLBACK(on_test_set_text), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);
}
