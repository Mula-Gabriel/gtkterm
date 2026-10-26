/* Dialogue Port (§6.4, action app.port, Ctrl+Maj+S) : type de liaison, port série et ses réglages, hôte et port TCP, envoi de fichier (délai de fin de
 * ligne, attente d'un caractère), RS-485, délais d'activité du port (repos, sans données : indicateurs d'onglet). Instance unique, modale,
 * transitoire de la fenêtre principale (C12). Saisie contrôlée sans widget par gt_port_form_apply (port_form.c). Deux modes (6b) : réglage de la session active à l'ouverture (OK copie dans son profil et rouvre son port ;
 * session fermée pendant le dialogue : fermé sans rien appliquer), ou création d'onglet (gt_port_dialog_show_new : OK rend le profil au rappel).
 * Annuler ne change rien. Remplace Config_Port_Fenetre / Lis_Config (GTK 3). */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>

#include "app_config.h"
#include "app_ui.h"
#include "port_form.h"
#include "serial_ports.h"
#include "transport.h"
#include "app.h"
#include "dialogs.h"
#include "port_dialog.h"

/* Valeurs des listes déroulantes, dans l'ordre d'affichage (positions explicites) */
static const gint transport_values[] = { TRANSPORT_SERIAL, TRANSPORT_TCP_CLIENT, TRANSPORT_TCP_SERVER };
static const char *const transport_labels[] = { N_("Serial"), N_("TCP Client"), N_("TCP Server") };
static const gint parity_values[] = { GT_PARITY_NONE, GT_PARITY_ODD, GT_PARITY_EVEN };
static const char *const parity_labels[] = { N_("None"), N_("Odd"), N_("Even") };
static const gint bits_values[] = { 5, 6, 7, 8 };
static const char *const bits_labels[] = { "5", "6", "7", "8" };
static const gint stopbits_values[] = { 1, 2 };
static const char *const stopbits_labels[] = { "1", "2" };
static const gint flow_values[] = { GT_FLOW_NONE, GT_FLOW_RTSCTS, GT_FLOW_XONXOFF, GT_FLOW_RS485 };
static const char *const flow_labels[] = { N_("None"), N_("RTS/CTS"), N_("Xon/Xoff"), N_("RS-485 half-duplex (RTS)") };

typedef struct
{
	GtkWindow *window;
	GtkDropDown *type;
	GtkWidget *serial_frame;
	GtkWidget *tcp_frame;
	GtkWidget *rs485_frame;
	GtkEntry *device;
	GtkStringList *device_model;	/* ports détectés */
	GHashTable *links;	/* chemins des liens udev (gras) */
	GtkEntry *baud;
	GtkStringList *baud_model;	/* débits standard */
	GtkDropDown *parity, *bits, *stopbits, *flow;
	GtkEntry *tcp_host, *tcp_port;
	GtkSpinButton *line_delay;
	GtkCheckButton *wait_char_check;
	GtkEntry *wait_char;
	GtkSpinButton *rs485_before, *rs485_after;
	GtkSpinButton *idle_timeout, *no_data_timeout;	/* secondes, 0 : jamais */
	GtkLabel *error;
	GWeakRef session;	/* réglage : session active à l'ouverture (spec §3.2-12) */
	GtProfile *base;	/* création d'onglet : copie du profil initial (possédée) ; NULL en réglage */
	GtPortDialogNewCb new_cb;
	gpointer new_data;
} PortDialog;

static GtkWindow *port_window = NULL;	/* dialogue ouvert (pointeur faible) */

static guint index_of(const gint *values, guint n, gint v)
{
	for(guint i = 0; i < n; i++)
		if(values[i] == v)
			return i;
	return 0;
}

static gint value_at(GtkDropDown *dd, const gint *values, guint n)
{
	guint i = gtk_drop_down_get_selected(dd);

	return values[i < n ? i : 0];
}

static GtkDropDown *dropdown_new(const char *name, const char *const *labels, guint n, gboolean translate, guint selected)
{
	const char *texts[8];
	GtkWidget *dd;

	g_assert(n < G_N_ELEMENTS(texts));
	for(guint i = 0; i < n; i++)
		texts[i] = translate ? _(labels[i]) : labels[i];
	texts[n] = NULL;
	dd = gtk_drop_down_new_from_strings(texts);
	gtk_widget_set_name(dd, name);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), selected);
	return GTK_DROP_DOWN(dd);
}

static GtkEntry *entry_new(const char *name, const char *text, gint max_length, GtkInputPurpose purpose)
{
	GtkWidget *entry = gtk_entry_new();

	gtk_widget_set_name(entry, name);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_editable_set_text(GTK_EDITABLE(entry), text);
	gtk_entry_set_max_length(GTK_ENTRY(entry), max_length);
	gtk_entry_set_input_purpose(GTK_ENTRY(entry), purpose);
	return GTK_ENTRY(entry);
}

static GtkSpinButton *delay_spin_new(const char *name, gint value)
{
	GtkWidget *spin = gtk_spin_button_new_with_range(0, GT_PORT_FORM_DELAY_MAX, 10);

	gtk_widget_set_name(spin, name);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
	return GTK_SPIN_BUTTON(spin);
}

static GtkSpinButton *seconds_spin_new(const char *name, gint value)
{
	GtkWidget *spin = gtk_spin_button_new_with_range(0, GT_PROFILE_ACTIVITY_MAX, 1);

	gtk_widget_set_name(spin, name);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
	return GTK_SPIN_BUTTON(spin);
}

static GtkWidget *grid_new(void)
{
	GtkWidget *grid = gtk_grid_new();

	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_widget_set_margin_top(grid, 6);
	gtk_widget_set_margin_bottom(grid, 6);
	gtk_widget_set_margin_start(grid, 6);
	gtk_widget_set_margin_end(grid, 6);
	return grid;
}

static void grid_label(GtkWidget *grid, const char *text, GtkWidget *target, gint column, gint row)
{
	GtkWidget *label = gtk_label_new_with_mnemonic(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), target);
	gtk_grid_attach(GTK_GRID(grid), label, column, row, 1, 1);
}

static GtkWidget *frame_new(const char *title, GtkWidget *child)
{
	GtkWidget *frame = gtk_frame_new(title);

	gtk_frame_set_child(GTK_FRAME(frame), child);
	return frame;
}

/* ---- Listes de choix (ports détectés, débits) ------------------------------ */

static void choice_setup(GtkSignalListItemFactory *factory, GObject *object, gpointer data)
{
	GtkWidget *label = gtk_label_new(NULL);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_list_item_set_child(GTK_LIST_ITEM(object), label);
}

static void choice_bind(GtkSignalListItemFactory *factory, GObject *object, gpointer bold_set)
{
	GtkListItem *item = GTK_LIST_ITEM(object);
	GtkLabel *label = GTK_LABEL(gtk_list_item_get_child(item));
	const char *text = gtk_string_object_get_string(gtk_list_item_get_item(item));
	PangoAttrList *attrs = NULL;

	gtk_label_set_text(label, text);
	/* Les liens udev (/dev/ttyM5_1…) nomment l'appareil : en gras, pour les distinguer des ttyACMx */
	if(bold_set != NULL && g_hash_table_contains(bold_set, text))
	{
		attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	}
	gtk_label_set_attributes(label, attrs);
	if(attrs != NULL)
		pango_attr_list_unref(attrs);
}

static void bold_set_release(gpointer data, GClosure *closure)
{
	g_hash_table_unref(data);
}

static void choice_activated(GtkListView *list, guint position, gpointer data)
{
	GtkStringObject *item = g_list_model_get_item(G_LIST_MODEL(gtk_list_view_get_model(list)), position);

	gtk_editable_set_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(list), "target")), gtk_string_object_get_string(item));
	gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(list), "popover")));
	g_object_unref(item);
}

static GtkWidget *choice_button_new(const char *name, const char *tooltip, GtkStringList *model, GHashTable *bold_set, GtkEntry *target)
{
	GtkWidget *button = gtk_menu_button_new();
	GtkWidget *popover = gtk_popover_new();
	GtkWidget *scroll = gtk_scrolled_window_new();
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	GtkWidget *list;

	gtk_widget_set_name(button, name);
	gtk_widget_set_tooltip_text(button, tooltip);
	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "pan-down-symbolic");
	g_signal_connect(factory, "setup", G_CALLBACK(choice_setup), NULL);
	if(bold_set != NULL)
		g_signal_connect_data(factory, "bind", G_CALLBACK(choice_bind), g_hash_table_ref(bold_set), bold_set_release, 0);
	else
		g_signal_connect(factory, "bind", G_CALLBACK(choice_bind), NULL);
	list = gtk_list_view_new(GTK_SELECTION_MODEL(gtk_no_selection_new(G_LIST_MODEL(g_object_ref(model)))), factory);
	gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(list), TRUE);
	g_object_set_data(G_OBJECT(list), "target", target);
	g_object_set_data(G_OBJECT(list), "popover", popover);
	g_signal_connect(list, "activate", G_CALLBACK(choice_activated), NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
	gtk_popover_set_child(GTK_POPOVER(popover), scroll);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
	return button;
}

/* ---- Comportement --------------------------------------------------------- */

static void update_visibility(PortDialog *pd)
{
	gboolean serial = value_at(pd->type, transport_values, G_N_ELEMENTS(transport_values)) == TRANSPORT_SERIAL;

	gtk_widget_set_visible(pd->serial_frame, serial);
	gtk_widget_set_visible(pd->rs485_frame, serial);
	gtk_widget_set_visible(pd->tcp_frame, !serial);
	gtk_widget_set_visible(GTK_WIDGET(pd->error), FALSE);
}

static void on_type_changed(GObject *dropdown, GParamSpec *pspec, PortDialog *pd)
{
	update_visibility(pd);
}

static void on_wait_toggled(GtkCheckButton *check, PortDialog *pd)
{
	gboolean wait = gtk_check_button_get_active(check);

	gtk_widget_set_sensitive(GTK_WIDGET(pd->wait_char), wait);
	gtk_widget_set_sensitive(GTK_WIDGET(pd->line_delay), !wait);
}

static gint spin_value(GtkSpinButton *spin)
{
	gtk_spin_button_update(spin);	/* texte saisi sans validation : pris en compte */
	return gtk_spin_button_get_value_as_int(spin);
}

static void read_form(PortDialog *pd, GtPortForm *f)
{
	memset(f, 0, sizeof(*f));
	f->transport = (TransportType)value_at(pd->type, transport_values, G_N_ELEMENTS(transport_values));
	f->device = g_strdup(gtk_editable_get_text(GTK_EDITABLE(pd->device)));
	f->baudrate = g_strdup(gtk_editable_get_text(GTK_EDITABLE(pd->baud)));
	f->parity = (GtParity)value_at(pd->parity, parity_values, G_N_ELEMENTS(parity_values));
	f->bits = value_at(pd->bits, bits_values, G_N_ELEMENTS(bits_values));
	f->stopbits = value_at(pd->stopbits, stopbits_values, G_N_ELEMENTS(stopbits_values));
	f->flow = (GtFlow)value_at(pd->flow, flow_values, G_N_ELEMENTS(flow_values));
	f->host = g_strdup(gtk_editable_get_text(GTK_EDITABLE(pd->tcp_host)));
	f->tcp_port = g_strdup(gtk_editable_get_text(GTK_EDITABLE(pd->tcp_port)));
	f->wait_delay_ms = spin_value(pd->line_delay);
	f->wait_char_enabled = gtk_check_button_get_active(pd->wait_char_check);
	f->wait_char = g_strdup(gtk_editable_get_text(GTK_EDITABLE(pd->wait_char)));
	f->rs485_before_ms = spin_value(pd->rs485_before);
	f->rs485_after_ms = spin_value(pd->rs485_after);
	f->idle_timeout_s = spin_value(pd->idle_timeout);
	f->no_data_timeout_s = spin_value(pd->no_data_timeout);
}

/* Session du dialogue en réglage (référence à rendre), NULL si elle n'existe plus ou est arrêtée */
static GtSession *dialog_session(PortDialog *pd)
{
	GtSession *s = g_weak_ref_get(&pd->session);

	if(s != NULL && gt_session_is_shut_down(s))
		g_clear_object(&s);
	return s;
}

static void on_ok(GtkButton *button, PortDialog *pd)
{
	GtPortForm form;
	GtProfile next = { 0 };
	GError *error = NULL;
	GPtrArray *warnings;
	GtSession *s = NULL;
	GtProfile *base;
	GtPortDialogNewCb new_cb = pd->new_cb;
	gpointer new_data = pd->new_data;

	if(pd->base != NULL)
		base = pd->base;
	else
	{
		s = dialog_session(pd);
		if(s == NULL)	/* session fermée pendant le dialogue : rien n'est appliqué */
		{
			gtk_window_destroy(pd->window);
			return;
		}
		base = gt_session_get_profile(s);
	}
	read_form(pd, &form);
	if(!gt_port_form_apply(&form, base, &next, &error))
	{
		gtk_label_set_text(pd->error, error->message);
		gtk_widget_set_visible(GTK_WIDGET(pd->error), TRUE);
		g_error_free(error);
		gt_port_form_clear(&form);
		g_clear_object(&s);
		return;
	}
	gt_port_form_clear(&form);
	warnings = g_ptr_array_new_with_free_func(g_free);
	gt_profile_validate(&next, warnings);	/* corrections silencieuses ; seul avertissement possible ici : débit non standard */
	if(s != NULL)
		gt_profile_copy(gt_session_get_profile(s), &next);
	gtk_window_destroy(pd->window);	/* pd est libéré : plus aucun accès après cette ligne */
	if(s != NULL)
		gt_session_open_port(s);
	else
		new_cb(&next, new_data);	/* création d'onglet : dialogue déjà fermé */
	gt_profile_clear(&next);
	g_clear_object(&s);
	if(warnings->len > 0)
	{
		gchar *text;

		g_ptr_array_add(warnings, NULL);
		text = g_strjoinv("\n", (gchar **)warnings->pdata);
		gt_app_message(GT_APP_MSG_WARNING, "%s", text);
		g_free(text);
	}
	g_ptr_array_unref(warnings);
}

static void port_dialog_free(gpointer data)
{
	PortDialog *pd = data;

	g_hash_table_unref(pd->links);
	g_object_unref(pd->device_model);
	g_object_unref(pd->baud_model);
	g_weak_ref_clear(&pd->session);
	if(pd->base != NULL)
	{
		gt_profile_clear(pd->base);
		g_free(pd->base);
	}
	g_free(pd);
}

/* ---- Construction ----------------------------------------------------------- */

static GtkWidget *build_tcp(PortDialog *pd, const GtPortForm *form)
{
	GtkWidget *grid = grid_new();

	pd->tcp_host = entry_new("port-tcp-host", form->host, 0, GTK_INPUT_PURPOSE_URL);
	pd->tcp_port = entry_new("port-tcp-port", form->tcp_port, 5, GTK_INPUT_PURPOSE_DIGITS);
	grid_label(grid, _("_Host:"), GTK_WIDGET(pd->tcp_host), 0, 0);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->tcp_host), 1, 0, 1, 1);
	grid_label(grid, _("_Port:"), GTK_WIDGET(pd->tcp_port), 0, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->tcp_port), 1, 1, 1, 1);
	return frame_new(_("TCP"), grid);
}

static GtkWidget *build_serial(PortDialog *pd, const GtPortForm *form, const GArray *patterns, guint n_ports)
{
	GtkWidget *grid = grid_new();
	GtkWidget *device_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *baud_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *device_list, *baud_list;

	gtk_widget_add_css_class(device_box, "linked");
	gtk_widget_add_css_class(baud_box, "linked");
	pd->device = entry_new("port-device", form->device, 0, GTK_INPUT_PURPOSE_FREE_FORM);
	device_list = choice_button_new("port-device-list", _("Detected serial ports"), pd->device_model, pd->links, pd->device);
	gtk_widget_set_sensitive(device_list, n_ports > 0);
	gtk_box_append(GTK_BOX(device_box), GTK_WIDGET(pd->device));
	gtk_box_append(GTK_BOX(device_box), device_list);
	pd->baud = entry_new("port-baud", form->baudrate, 10, GTK_INPUT_PURPOSE_DIGITS);
	baud_list = choice_button_new("port-baud-list", _("Standard baud rates"), pd->baud_model, NULL, pd->baud);
	gtk_box_append(GTK_BOX(baud_box), GTK_WIDGET(pd->baud));
	gtk_box_append(GTK_BOX(baud_box), baud_list);
	pd->parity = dropdown_new("port-parity", parity_labels, G_N_ELEMENTS(parity_labels), TRUE,
	                          index_of(parity_values, G_N_ELEMENTS(parity_values), form->parity));
	pd->bits = dropdown_new("port-bits", bits_labels, G_N_ELEMENTS(bits_labels), FALSE, index_of(bits_values, G_N_ELEMENTS(bits_values), form->bits));
	pd->stopbits = dropdown_new("port-stopbits", stopbits_labels, G_N_ELEMENTS(stopbits_labels), FALSE,
	                            index_of(stopbits_values, G_N_ELEMENTS(stopbits_values), form->stopbits));
	pd->flow = dropdown_new("port-flow", flow_labels, G_N_ELEMENTS(flow_labels), TRUE, index_of(flow_values, G_N_ELEMENTS(flow_values), form->flow));

	grid_label(grid, _("_Port:"), GTK_WIDGET(pd->device), 0, 0);
	gtk_grid_attach(GTK_GRID(grid), device_box, 0, 1, 1, 1);
	grid_label(grid, _("_Baud Rate:"), GTK_WIDGET(pd->baud), 1, 0);
	gtk_grid_attach(GTK_GRID(grid), baud_box, 1, 1, 1, 1);
	grid_label(grid, _("P_arity:"), GTK_WIDGET(pd->parity), 2, 0);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->parity), 2, 1, 1, 1);
	grid_label(grid, _("B_its:"), GTK_WIDGET(pd->bits), 0, 2);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->bits), 0, 3, 1, 1);
	grid_label(grid, _("_Stopbits:"), GTK_WIDGET(pd->stopbits), 1, 2);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->stopbits), 1, 3, 1, 1);
	grid_label(grid, _("_Flow control:"), GTK_WIDGET(pd->flow), 2, 2);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->flow), 2, 3, 1, 1);
	if(n_ports == 0)
	{
		gchar *list = gt_serial_patterns_to_string(patterns);
		gchar *text = g_strdup_printf(_("No serial devices found. Searched the following device path patterns:\n%sEnter a device path in the Port box."),
		                              list);
		GtkWidget *none = gtk_label_new(text);

		gtk_widget_set_name(none, "port-no-device");
		gtk_label_set_wrap(GTK_LABEL(none), TRUE);
		gtk_label_set_xalign(GTK_LABEL(none), 0.0);
		gtk_widget_add_css_class(none, "dim-label");
		gtk_grid_attach(GTK_GRID(grid), none, 0, 4, 3, 1);
		g_free(text);
		g_free(list);
	}
	return frame_new(_("Serial port"), grid);
}

static GtkWidget *build_advanced(PortDialog *pd, const GtPortForm *form)
{
	GtkWidget *expander = gtk_expander_new_with_mnemonic(_("_Advanced Configuration Options"));
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *file_grid = grid_new(), *rs485_grid = grid_new(), *activity_grid = grid_new();
	GtkWidget *check;

	gtk_widget_set_name(expander, "port-advanced");
	pd->line_delay = delay_spin_new("port-line-delay", form->wait_delay_ms);
	grid_label(file_grid, _("End of line _delay (milliseconds):"), GTK_WIDGET(pd->line_delay), 0, 0);
	gtk_grid_attach(GTK_GRID(file_grid), GTK_WIDGET(pd->line_delay), 1, 0, 1, 1);
	check = gtk_check_button_new_with_mnemonic(_("_Wait for this special character before passing to next line:"));
	gtk_widget_set_name(check, "port-wait-char-check");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(check), form->wait_char_enabled);
	pd->wait_char_check = GTK_CHECK_BUTTON(check);
	pd->wait_char = entry_new("port-wait-char", form->wait_char, 1, GTK_INPUT_PURPOSE_FREE_FORM);
	gtk_grid_attach(GTK_GRID(file_grid), check, 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(file_grid), GTK_WIDGET(pd->wait_char), 1, 1, 1, 1);
	gtk_box_append(GTK_BOX(box), frame_new(_("ASCII file transfer"), file_grid));

	/* Mnémoniques l et n : libres dans le dialogue (i est pris par « B_its ») */
	pd->idle_timeout = seconds_spin_new("port-idle-timeout", form->idle_timeout_s);
	grid_label(activity_grid, _("Id_le after (seconds, 0: never):"), GTK_WIDGET(pd->idle_timeout), 0, 0);
	gtk_grid_attach(GTK_GRID(activity_grid), GTK_WIDGET(pd->idle_timeout), 1, 0, 1, 1);
	pd->no_data_timeout = seconds_spin_new("port-no-data-timeout", form->no_data_timeout_s);
	grid_label(activity_grid, _("_No data after (seconds, 0: never):"), GTK_WIDGET(pd->no_data_timeout), 0, 1);
	gtk_grid_attach(GTK_GRID(activity_grid), GTK_WIDGET(pd->no_data_timeout), 1, 1, 1, 1);
	gtk_box_append(GTK_BOX(box), frame_new(_("Port activity"), activity_grid));

	pd->rs485_before = delay_spin_new("port-rs485-before", form->rs485_before_ms);
	pd->rs485_after = delay_spin_new("port-rs485-after", form->rs485_after_ms);
	grid_label(rs485_grid, _("Time with RTS 'on' _before transmit (milliseconds):"), GTK_WIDGET(pd->rs485_before), 0, 0);
	gtk_grid_attach(GTK_GRID(rs485_grid), GTK_WIDGET(pd->rs485_before), 1, 0, 1, 1);
	grid_label(rs485_grid, _("Time with RTS 'on' a_fter transmit (milliseconds):"), GTK_WIDGET(pd->rs485_after), 0, 1);
	gtk_grid_attach(GTK_GRID(rs485_grid), GTK_WIDGET(pd->rs485_after), 1, 1, 1, 1);
	pd->rs485_frame = frame_new(_("RS-485 half-duplex parameters (RTS signal used to send)"), rs485_grid);
	gtk_box_append(GTK_BOX(box), pd->rs485_frame);
	gtk_expander_set_child(GTK_EXPANDER(expander), box);
	return expander;
}

/* s : session réglée (mode réglage) ; sinon initial copié, new_cb appelé par OK (création d'onglet) */
static void port_dialog_open(GtkWindow *parent, GtSession *s, const GtProfile *initial, GtPortDialogNewCb new_cb, gpointer new_data)
{
	PortDialog *pd;
	GtPortForm form;
	GArray *patterns;
	GPtrArray *ports;
	GtkWidget *box, *error, *buttons, *cancel, *ok;

	if(port_window != NULL)
	{
		gtk_window_present(port_window);	/* C12 : une seconde demande ramène le dialogue ouvert, tel quel */
		return;
	}
	pd = g_new0(PortDialog, 1);
	g_weak_ref_init(&pd->session, s);
	if(s == NULL)
	{
		pd->base = g_new0(GtProfile, 1);
		gt_profile_copy(pd->base, initial);
		pd->new_cb = new_cb;
		pd->new_data = new_data;
	}
	pd->links = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	pd->device_model = gtk_string_list_new(NULL);
	pd->baud_model = gtk_string_list_new(NULL);
	for(gint i = 0; i < baudrate_count; i++)
	{
		gchar *s = g_strdup_printf("%u", baudrate_list[i].baud);

		gtk_string_list_append(pd->baud_model, s);
		g_free(s);
	}
	patterns = gt_serial_patterns_read(GT_SERIAL_DRIVERS_FILE);
	ports = gt_serial_ports_find(patterns, GT_SERIAL_LINKS_DIR, GT_SERIAL_SYSFS_CHAR_DIR);
	for(guint i = 0; i < ports->len; i++)
	{
		const GtSerialPort *p = g_ptr_array_index(ports, i);

		gtk_string_list_append(pd->device_model, p->path);
		if(p->is_link)
			g_hash_table_add(pd->links, g_strdup(p->path));
	}
	gt_port_form_from_profile(&form, s != NULL ? gt_session_get_profile(s) : pd->base);
	if(form.device[0] == '\0' && ports->len > 0)
		gt_profile_set_string(&form.device, ((GtSerialPort *)g_ptr_array_index(ports, 0))->path);

	pd->window = GTK_WINDOW(gtk_window_new());
	gtk_widget_set_name(GTK_WIDGET(pd->window), "port-dialog");
	gtk_window_set_title(pd->window, s != NULL ? _("Port configuration") : _("New tab"));
	gtk_window_set_modal(pd->window, TRUE);
	gtk_window_set_transient_for(pd->window, parent);
	gtk_window_set_destroy_with_parent(pd->window, TRUE);
	gtk_window_set_resizable(pd->window, FALSE);
	g_object_set_data_full(G_OBJECT(pd->window), "port-dialog", pd, port_dialog_free);
	if(s != NULL)	/* session fermée pendant le dialogue : fermé sans rien appliquer */
		g_signal_connect_object(s, "closed", G_CALLBACK(gtk_window_destroy), pd->window, G_CONNECT_SWAPPED);
	port_window = pd->window;
	g_object_add_weak_pointer(G_OBJECT(pd->window), (gpointer *)&port_window);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	{
		GtkWidget *grid = grid_new();

		pd->type = dropdown_new("port-type", transport_labels, G_N_ELEMENTS(transport_labels), TRUE,
		                        index_of(transport_values, G_N_ELEMENTS(transport_values), form.transport));
		grid_label(grid, _("_Type:"), GTK_WIDGET(pd->type), 0, 0);
		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(pd->type), 1, 0, 1, 1);
		gtk_box_append(GTK_BOX(box), frame_new(_("Transport"), grid));
	}
	pd->tcp_frame = build_tcp(pd, &form);
	gtk_box_append(GTK_BOX(box), pd->tcp_frame);
	pd->serial_frame = build_serial(pd, &form, patterns, ports->len);
	gtk_box_append(GTK_BOX(box), pd->serial_frame);
	gtk_box_append(GTK_BOX(box), build_advanced(pd, &form));

	error = gtk_label_new(NULL);
	gtk_widget_set_name(error, "port-error");
	gtk_label_set_wrap(GTK_LABEL(error), TRUE);
	gtk_label_set_xalign(GTK_LABEL(error), 0.0);
	gtk_widget_add_css_class(error, "error");
	pd->error = GTK_LABEL(error);
	gtk_box_append(GTK_BOX(box), error);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
	gtk_widget_set_name(cancel, "port-cancel");
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), pd->window);
	ok = gtk_button_new_with_mnemonic(_("_OK"));
	gtk_widget_set_name(ok, "port-ok");
	gtk_widget_add_css_class(ok, "suggested-action");
	g_signal_connect(ok, "clicked", G_CALLBACK(on_ok), pd);
	gtk_box_append(GTK_BOX(buttons), cancel);
	gtk_box_append(GTK_BOX(buttons), ok);
	gtk_box_append(GTK_BOX(box), buttons);

	g_signal_connect(pd->type, "notify::selected", G_CALLBACK(on_type_changed), pd);
	g_signal_connect(pd->wait_char_check, "toggled", G_CALLBACK(on_wait_toggled), pd);
	gt_ui_close_on_escape(pd->window);	/* Échap ferme (dialogs.c) */
	gtk_window_set_default_widget(pd->window, ok);
	gtk_window_set_child(pd->window, box);
	update_visibility(pd);
	on_wait_toggled(pd->wait_char_check, pd);

	gt_port_form_clear(&form);
	g_ptr_array_unref(ports);
	g_array_unref(patterns);
	gtk_window_present(pd->window);
}

void gt_port_dialog_show(GtkWindow *parent)
{
	GtSession *s = gt_app_get_active_session();

	if(port_window == NULL && (s == NULL || gt_session_is_shut_down(s)))
		return;
	port_dialog_open(parent, s, NULL, NULL, NULL);
}

void gt_port_dialog_show_new(GtkWindow *parent, const GtProfile *initial, GtPortDialogNewCb cb, gpointer user_data)
{
	port_dialog_open(parent, NULL, initial, cb, user_data);
}

static void on_port_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_port_dialog_show(gt_app_get_main_window());
}

void gt_port_dialog_install(GtkApplication *app)
{
	static const GActionEntry entries[] = { { .name = "port", .activate = on_port_action } };
	static const char *const accels[] = { "<Control><Shift>s", NULL };

	g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);
	gtk_application_set_accels_for_action(app, "app.port", accels);
}
