/* Indicateur d'activité d'un port : voir activity_icon.h. */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "activity_icon.h"

#define ICON_KEY "gt-activity-icon"
#define SPIN_FRAME_US 66000	/* ≈ 15 images par seconde */
#define SPIN_PERIOD_US 900000	/* un tour */
#define ZZZ_STEP_MS 800

typedef struct
{
	GtkWidget *stack;	/* le widget rendu (non possédé) */
	GtPortActivity *activity;	/* référence */
	GtkWidget *spinner;	/* référence : son rappel par image est retiré à la destruction, même si la pile l'a déjà détaché */
	GtkWidget *dot, *skull, *zzz;
	GtkWidget *z[3];
	GtPortActivityState state;
	guint tick_id;
	gint64 last_frame_us;
	gdouble angle;
	guint zzz_id;
	gint zzz_step;
} ActivityIcon;

static const gchar *accessible_label(GtPortActivityState st)
{
	switch(st)
	{
	case GT_PORT_ACTIVITY_CONNECTING: return _("Connecting");
	case GT_PORT_ACTIVITY_WAITING: return _("Waiting for data");
	case GT_PORT_ACTIVITY_ACTIVE: return _("Port open");
	case GT_PORT_ACTIVITY_RECEIVING: return _("Receiving data");
	case GT_PORT_ACTIVITY_IDLE: return _("Idle");
	case GT_PORT_ACTIVITY_LOST: return _("Connection lost");
	case GT_PORT_ACTIVITY_RECONNECTING: return _("Reconnecting");
	case GT_PORT_ACTIVITY_DEAD: return _("No data received");
	default: return _("Port closed");
	}
}

/* Cercle pâle et arc d'un quart de tour ; couleur de la zone (classe CSS connecting / reconnecting) : cairo ne voit pas le CSS */
static void draw_spinner(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
	ActivityIcon *icon = data;
	GdkRGBA c;
	gdouble r = MIN(w, h) / 2.0 - 1.5;

	gtk_widget_get_color(GTK_WIDGET(area), &c);
	cairo_set_line_width(cr, 2.0);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha * 0.25);
	cairo_arc(cr, w / 2.0, h / 2.0, r, 0, 2 * G_PI);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
	cairo_arc(cr, w / 2.0, h / 2.0, r, icon->angle, icon->angle + G_PI / 2);
	cairo_stroke(cr);
}

/* Rotation : redessin limité à SPIN_FRAME_US (un serveur en attente affiché tourne indéfiniment) */
static gboolean on_spin_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	ActivityIcon *icon = data;
	gint64 now = gdk_frame_clock_get_frame_time(clock);

	if(now - icon->last_frame_us >= SPIN_FRAME_US)
	{
		icon->last_frame_us = now;
		icon->angle = (gdouble)(now % SPIN_PERIOD_US) / SPIN_PERIOD_US * 2 * G_PI;
		gtk_widget_queue_draw(widget);
	}
	return G_SOURCE_CONTINUE;
}

static void zzz_show(ActivityIcon *icon)
{
	gtk_widget_set_opacity(icon->z[1], icon->zzz_step >= 1 ? 1.0 : 0.0);
	gtk_widget_set_opacity(icon->z[2], icon->zzz_step >= 2 ? 1.0 : 0.0);
}

static gboolean on_zzz_step(gpointer data)
{
	ActivityIcon *icon = data;

	icon->zzz_step = (icon->zzz_step + 1) % 3;	/* chenillard : Z, Z z, Z z z, Z… */
	zzz_show(icon);
	return G_SOURCE_CONTINUE;
}

/* Animations seulement pour l'état affiché et un widget cartographié */
static void update_animations(ActivityIcon *icon)
{
	gboolean mapped = gtk_widget_get_mapped(icon->stack);
	gboolean spin = mapped && (icon->state == GT_PORT_ACTIVITY_CONNECTING || icon->state == GT_PORT_ACTIVITY_RECONNECTING);
	gboolean zzz = mapped && icon->state == GT_PORT_ACTIVITY_IDLE;

	if(spin && icon->tick_id == 0)
	{
		icon->last_frame_us = 0;
		icon->tick_id = gtk_widget_add_tick_callback(icon->spinner, on_spin_tick, icon, NULL);
	}
	else if(!spin && icon->tick_id != 0)
	{
		gtk_widget_remove_tick_callback(icon->spinner, icon->tick_id);
		icon->tick_id = 0;
	}
	if(zzz && icon->zzz_id == 0)
	{
		icon->zzz_step = 0;
		zzz_show(icon);
		icon->zzz_id = g_timeout_add(ZZZ_STEP_MS, on_zzz_step, icon);
	}
	else if(!zzz)
		g_clear_handle_id(&icon->zzz_id, g_source_remove);
}

static void css_class_set(GtkWidget *widget, const char *cls, gboolean on)
{
	if(on)
		gtk_widget_add_css_class(widget, cls);
	else
		gtk_widget_remove_css_class(widget, cls);
}

/* notify::state de l'activité (et construction) : page de la pile, classes CSS, nom accessible, animations */
static void on_state(GObject *object, GParamSpec *pspec, gpointer data)
{
	ActivityIcon *icon = data;
	GtPortActivityState st = gt_port_activity_get_state(icon->activity);
	gchar *old_class = g_strdup_printf("activity-%s", gt_port_activity_state_nick(icon->state));
	gchar *new_class = g_strdup_printf("activity-%s", gt_port_activity_state_nick(st));
	const gchar *page = "none";

	gtk_widget_remove_css_class(icon->stack, old_class);
	gtk_widget_add_css_class(icon->stack, new_class);
	icon->state = st;
	switch(st)
	{
	case GT_PORT_ACTIVITY_WAITING:
	case GT_PORT_ACTIVITY_ACTIVE:
	case GT_PORT_ACTIVITY_RECEIVING:
	case GT_PORT_ACTIVITY_LOST:
		page = "dot";
		break;
	case GT_PORT_ACTIVITY_CONNECTING:
	case GT_PORT_ACTIVITY_RECONNECTING:
		page = "spinner";
		break;
	case GT_PORT_ACTIVITY_IDLE:
		page = "zzz";
		break;
	case GT_PORT_ACTIVITY_DEAD:
		page = "skull";
		break;
	default:
		break;
	}
	css_class_set(icon->dot, "receiving", st == GT_PORT_ACTIVITY_RECEIVING);
	css_class_set(icon->dot, "lost", st == GT_PORT_ACTIVITY_LOST);
	css_class_set(icon->spinner, "reconnecting", st == GT_PORT_ACTIVITY_RECONNECTING);
	css_class_set(icon->spinner, "connecting", st != GT_PORT_ACTIVITY_RECONNECTING);
	gtk_stack_set_visible_child_name(GTK_STACK(icon->stack), page);
	gtk_accessible_update_property(GTK_ACCESSIBLE(icon->stack), GTK_ACCESSIBLE_PROPERTY_LABEL, accessible_label(st), -1);
	update_animations(icon);
	g_free(old_class);
	g_free(new_class);
}

static void on_map_changed(GtkWidget *widget, gpointer data)
{
	update_animations(data);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
	ActivityIcon *icon = data;

	g_clear_handle_id(&icon->zzz_id, g_source_remove);
	if(icon->tick_id != 0)
		gtk_widget_remove_tick_callback(icon->spinner, icon->tick_id);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon->spinner), NULL, NULL, NULL);
	g_object_unref(icon->spinner);
	g_signal_handlers_disconnect_by_data(icon->activity, icon);
	g_object_unref(icon->activity);
	g_object_set_data(G_OBJECT(widget), ICON_KEY, NULL);
	g_free(icon);
}

GtkWidget *gt_activity_icon_new(GtPortActivity *activity, const char *name)
{
	ActivityIcon *icon = g_new0(ActivityIcon, 1);
	static const char *const z_text[3] = { "Z", "z", "z" };
	static const GtkAlign z_valign[3] = { GTK_ALIGN_END, GTK_ALIGN_CENTER, GTK_ALIGN_START };	/* en escalier */

	icon->activity = g_object_ref(activity);
	icon->state = GT_PORT_ACTIVITY_CLOSED;
	icon->stack = gtk_stack_new();
	gtk_widget_set_name(icon->stack, name);
	gtk_widget_add_css_class(icon->stack, "activity-icon");
	gtk_widget_set_size_request(icon->stack, 18, 16);
	gtk_widget_set_valign(icon->stack, GTK_ALIGN_CENTER);
	gtk_stack_set_transition_type(GTK_STACK(icon->stack), GTK_STACK_TRANSITION_TYPE_NONE);

	gtk_stack_add_named(GTK_STACK(icon->stack), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), "none");

	icon->dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(icon->dot, "activity-dot");
	gtk_widget_set_halign(icon->dot, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->dot, GTK_ALIGN_CENTER);
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->dot, "dot");

	icon->spinner = g_object_ref(gtk_drawing_area_new());
	gtk_widget_add_css_class(icon->spinner, "activity-spinner");
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon->spinner), 12);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon->spinner), 12);
	gtk_widget_set_halign(icon->spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->spinner, GTK_ALIGN_CENTER);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon->spinner), draw_spinner, icon, NULL);
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->spinner, "spinner");

	icon->skull = gtk_image_new_from_icon_name("skull-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(icon->skull), 14);
	gtk_widget_add_css_class(icon->skull, "activity-skull");
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->skull, "skull");

	icon->zzz = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(icon->zzz, "activity-zzz");
	gtk_widget_set_halign(icon->zzz, GTK_ALIGN_CENTER);
	for(guint i = 0; i < 3; i++)
	{
		gchar *cls = g_strdup_printf("z%u", i + 1);

		icon->z[i] = gtk_label_new(z_text[i]);
		gtk_widget_add_css_class(icon->z[i], cls);
		gtk_widget_set_valign(icon->z[i], z_valign[i]);
		gtk_box_append(GTK_BOX(icon->zzz), icon->z[i]);
		g_free(cls);
	}
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->zzz, "zzz");

	g_object_set_data(G_OBJECT(icon->stack), ICON_KEY, icon);
	g_signal_connect(activity, "notify::state", G_CALLBACK(on_state), icon);	/* déconnecté par on_destroy */
	g_signal_connect(icon->stack, "map", G_CALLBACK(on_map_changed), icon);
	g_signal_connect(icon->stack, "unmap", G_CALLBACK(on_map_changed), icon);
	g_signal_connect(icon->stack, "destroy", G_CALLBACK(on_destroy), icon);
	on_state(NULL, NULL, icon);
	return icon->stack;
}
