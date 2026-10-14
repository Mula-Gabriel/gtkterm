/* Indicateur d'activité d'un port : voir activity_icon.h. Les indicateurs sont superposés dans un GtkOverlay de taille fixe et un seul est
 * opaque : changer d'indicateur ne change ni la visibilité ni la taille d'un widget. Une GtkStack (changement d'enfant visible) dans l'étiquette
 * d'un onglet d'un GtkNotebook défilable fait allouer ses flèches de défilement à une largeur nulle (avertissements « Trying to measure
 * GtkImage … for width of 0 », vu avec GTK 4.24). L'overlay a le rôle accessible « image » et porte le nom de l'état ; les indicateurs
 * sont cachés à l'accessibilité (sinon un lecteur d'écran lirait « Z z z »). */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "activity_icon.h"

#define ICON_KEY "gt-activity-icon"
#define SPIN_FRAME_MS 66	/* ≈ 15 images par seconde */
#define SPIN_PERIOD_US 900000	/* un tour */
#define ZZZ_STEP_MS 800

typedef struct
{
	GtkWidget *overlay;	/* le widget rendu (non possédé) */
	GtPortActivity *activity;	/* référence */
	GtkWidget *spinner;	/* référence : sa fonction de dessin est retirée à la destruction, même si l'overlay l'a déjà détaché */
	GtkWidget *dot, *skull, *zzz;	/* superposés : un seul opaque (aucun pour l'état fermé) */
	GtkWidget *z[3];
	GtPortActivityState state;
	guint spin_id;
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

static void spin_angle_update(ActivityIcon *icon)
{
	icon->angle = (gdouble)(g_get_monotonic_time() % SPIN_PERIOD_US) / SPIN_PERIOD_US * 2 * G_PI;
}

/* Rotation : une minuterie de SPIN_FRAME_MS plutôt qu'un rappel par image (≈ 60 réveils par seconde pour 15 redessins) ; un serveur en
 * attente affiché tourne indéfiniment. Angle tiré de l'horloge monotone : un réveil en retard ne ralentit pas la rotation. */
static gboolean on_spin_step(gpointer data)
{
	ActivityIcon *icon = data;

	spin_angle_update(icon);
	gtk_widget_queue_draw(icon->spinner);
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
	gboolean mapped = gtk_widget_get_mapped(icon->overlay);
	gboolean spin = mapped && (icon->state == GT_PORT_ACTIVITY_CONNECTING || icon->state == GT_PORT_ACTIVITY_RECONNECTING);
	gboolean zzz = mapped && icon->state == GT_PORT_ACTIVITY_IDLE;

	if(spin && icon->spin_id == 0)
	{
		spin_angle_update(icon);
		gtk_widget_queue_draw(icon->spinner);
		icon->spin_id = g_timeout_add(SPIN_FRAME_MS, on_spin_step, icon);
	}
	else if(!spin)
		g_clear_handle_id(&icon->spin_id, g_source_remove);
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

/* Seul shown (NULL : aucun) est opaque */
static void indicator_show(ActivityIcon *icon, GtkWidget *shown)
{
	GtkWidget *all[4] = { icon->dot, icon->spinner, icon->skull, icon->zzz };

	for(guint i = 0; i < G_N_ELEMENTS(all); i++)
		gtk_widget_set_opacity(all[i], all[i] == shown ? 1.0 : 0.0);
}

/* notify::state de l'activité (et construction) : indicateur opaque, classes CSS, nom accessible, animations */
static void on_state(GObject *object, GParamSpec *pspec, gpointer data)
{
	ActivityIcon *icon = data;
	GtPortActivityState st = gt_port_activity_get_state(icon->activity);
	gchar *old_class = g_strdup_printf("activity-%s", gt_port_activity_state_nick(icon->state));
	gchar *new_class = g_strdup_printf("activity-%s", gt_port_activity_state_nick(st));
	GtkWidget *shown = NULL;

	gtk_widget_remove_css_class(icon->overlay, old_class);
	gtk_widget_add_css_class(icon->overlay, new_class);
	icon->state = st;
	switch(st)
	{
	case GT_PORT_ACTIVITY_WAITING:
	case GT_PORT_ACTIVITY_ACTIVE:
	case GT_PORT_ACTIVITY_RECEIVING:
	case GT_PORT_ACTIVITY_LOST:
		shown = icon->dot;
		break;
	case GT_PORT_ACTIVITY_CONNECTING:
	case GT_PORT_ACTIVITY_RECONNECTING:
		shown = icon->spinner;
		break;
	case GT_PORT_ACTIVITY_IDLE:
		shown = icon->zzz;
		break;
	case GT_PORT_ACTIVITY_DEAD:
		shown = icon->skull;
		break;
	default:
		break;
	}
	css_class_set(icon->dot, "receiving", st == GT_PORT_ACTIVITY_RECEIVING);
	css_class_set(icon->dot, "lost", st == GT_PORT_ACTIVITY_LOST);
	css_class_set(icon->spinner, "reconnecting", st == GT_PORT_ACTIVITY_RECONNECTING);
	css_class_set(icon->spinner, "connecting", st != GT_PORT_ACTIVITY_RECONNECTING);
	indicator_show(icon, shown);
	gtk_accessible_update_property(GTK_ACCESSIBLE(icon->overlay), GTK_ACCESSIBLE_PROPERTY_LABEL, accessible_label(st), -1);
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
	g_clear_handle_id(&icon->spin_id, g_source_remove);
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
	GtkWidget *base;
	static const char *const z_text[3] = { "Z", "z", "z" };
	static const GtkAlign z_valign[3] = { GTK_ALIGN_END, GTK_ALIGN_CENTER, GTK_ALIGN_START };	/* en escalier */

	icon->activity = g_object_ref(activity);
	icon->state = GT_PORT_ACTIVITY_CLOSED;
	icon->overlay = g_object_new(GTK_TYPE_OVERLAY, "accessible-role", GTK_ACCESSIBLE_ROLE_IMG, NULL);
	gtk_widget_set_name(icon->overlay, name);
	gtk_widget_add_css_class(icon->overlay, "activity-icon");
	gtk_widget_set_valign(icon->overlay, GTK_ALIGN_CENTER);
	gtk_widget_set_can_target(icon->overlay, FALSE);	/* les clics et l'infobulle vont à l'étiquette */
	base = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);	/* fixe la taille ; les indicateurs y sont centrés */
	gtk_widget_set_size_request(base, 18, 16);
	gtk_overlay_set_child(GTK_OVERLAY(icon->overlay), base);

	icon->dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(icon->dot, "activity-dot");
	gtk_widget_set_halign(icon->dot, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->dot, GTK_ALIGN_CENTER);
	gtk_overlay_add_overlay(GTK_OVERLAY(icon->overlay), icon->dot);

	icon->spinner = g_object_ref(gtk_drawing_area_new());
	gtk_widget_add_css_class(icon->spinner, "activity-spinner");
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon->spinner), 12);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon->spinner), 12);
	gtk_widget_set_halign(icon->spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->spinner, GTK_ALIGN_CENTER);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon->spinner), draw_spinner, icon, NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(icon->overlay), icon->spinner);

	icon->skull = gtk_image_new_from_icon_name("skull-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(icon->skull), 14);
	gtk_widget_add_css_class(icon->skull, "activity-skull");
	gtk_widget_set_halign(icon->skull, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->skull, GTK_ALIGN_CENTER);
	gtk_overlay_add_overlay(GTK_OVERLAY(icon->overlay), icon->skull);

	icon->zzz = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(icon->zzz, "activity-zzz");
	gtk_widget_set_halign(icon->zzz, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->zzz, GTK_ALIGN_FILL);
	for(guint i = 0; i < 3; i++)
	{
		gchar *cls = g_strdup_printf("z%u", i + 1);

		icon->z[i] = gtk_label_new(z_text[i]);
		gtk_widget_add_css_class(icon->z[i], cls);
		gtk_widget_set_valign(icon->z[i], z_valign[i]);
		gtk_box_append(GTK_BOX(icon->zzz), icon->z[i]);
		g_free(cls);
	}
	gtk_overlay_add_overlay(GTK_OVERLAY(icon->overlay), icon->zzz);
	{
		GtkWidget *hidden[5] = { base, icon->dot, icon->spinner, icon->skull, icon->zzz };	/* seul le nom de l'état est exposé */

		for(guint i = 0; i < G_N_ELEMENTS(hidden); i++)
			gtk_accessible_update_state(GTK_ACCESSIBLE(hidden[i]), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
	}

	g_object_set_data(G_OBJECT(icon->overlay), ICON_KEY, icon);
	g_signal_connect(activity, "notify::state", G_CALLBACK(on_state), icon);	/* déconnecté par on_destroy */
	g_signal_connect(icon->overlay, "map", G_CALLBACK(on_map_changed), icon);
	g_signal_connect(icon->overlay, "unmap", G_CALLBACK(on_map_changed), icon);
	g_signal_connect(icon->overlay, "destroy", G_CALLBACK(on_destroy), icon);
	on_state(NULL, NULL, icon);
	return icon->overlay;
}
