/* Profils et fichiers de macros (sous-projet 6a, §6.4) : fenêtres modales transitoires de la fenêtre principale, sélecteurs GtkFileDialog.
 * Remplace Select_config, Save_config_file, load/save_macros_file_callback de term_config.c (GTK 3), sans boucle imbriquée.
 * Chaque dialogue et chaque sélecteur agit sur la session active au moment de son ouverture (6b, plan, décision 4), gardée par référence
 * faible : session fermée entre-temps, rien n'est appliqué. */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app_config.h"
#include "link.h"
#include "macros.h"
#include "macros_save.h"
#include "profile.h"
#include "term_options.h"
#include "app.h"
#include "dialogs.h"
#include "profile_dialogs.h"

typedef enum
{
	PROFILE_LOAD,
	PROFILE_SAVE,
	PROFILE_DELETE
} ProfileMode;

typedef struct
{
	ProfileMode mode;
	GWeakRef session;	/* session active à l'ouverture */
	GtkWindow *window;
	GtkSingleSelection *selection;	/* chargement et suppression ; NULL à l'enregistrement ou si la liste est vide */
	GtkEditable *name;	/* enregistrement ; NULL sinon */
	GtkLabel *error;
} ProfileDialog;

/* Opération en attente d'une confirmation : la fenêtre peut avoir disparu quand la réponse arrive (fermeture de GTKTerm) */
typedef struct
{
	GWeakRef window;
	gchar *name;
} PendingConfirm;

static GtkWindow *profile_window = NULL;	/* fenêtre de profils ouverte, une seule à la fois (pointeur faible) */

static void show_error(ProfileDialog *pd, const gchar *text)
{
	gtk_label_set_text(pd->error, text);
	gtk_widget_set_visible(GTK_WIDGET(pd->error), TRUE);
}

static void clear_error(ProfileDialog *pd)
{
	gtk_widget_set_visible(GTK_WIDGET(pd->error), FALSE);
}

static ProfileDialog *dialog_of(GtkWindow *win)
{
	return g_object_get_data(G_OBJECT(win), "profile-dialog");
}

static void profile_dialog_free(gpointer data)
{
	ProfileDialog *pd = data;

	g_weak_ref_clear(&pd->session);
	g_free(pd);
}

/* Session du dialogue (référence à rendre), NULL si elle n'existe plus ou est arrêtée */
static GtSession *weak_session(GWeakRef *ref)
{
	GtSession *s = g_weak_ref_get(ref);

	if(s != NULL && gt_session_is_shut_down(s))
		g_clear_object(&s);
	return s;
}

static PendingConfirm *pending_new(ProfileDialog *pd, const gchar *name)
{
	PendingConfirm *pc = g_new0(PendingConfirm, 1);

	g_weak_ref_init(&pc->window, pd->window);
	pc->name = g_strdup(name);
	return pc;
}

static void pending_free(PendingConfirm *pc)
{
	g_weak_ref_clear(&pc->window);
	g_free(pc->name);
	g_free(pc);
}

static gchar *selected_name(ProfileDialog *pd)
{
	GtkStringObject *item;

	if(pd->selection == NULL)
		return NULL;
	item = gtk_single_selection_get_selected_item(pd->selection);
	return item != NULL ? g_strdup(gtk_string_object_get_string(item)) : NULL;
}

static void save_now(ProfileDialog *pd, const gchar *name)
{
	GtSession *s = weak_session(&pd->session);
	GError *error = NULL;
	gboolean saved;

	if(s == NULL)	/* session fermée : rien n'est enregistré */
	{
		gtk_window_destroy(pd->window);
		return;
	}
	/* Le profil de la session prend son état réel (options des menus, auto-reconnexion) dans app_config_save_profile */
	saved = app_config_save_profile(s, name, &error);
	g_object_unref(s);
	if(saved)
	{
		gtk_window_destroy(pd->window);
		return;
	}
	/* FALSE : GError toujours posé (nom refusé ou écriture échouée), affiché dans profile-error ; la fenêtre reste ouverte */
	show_error(pd, error->message);
	g_error_free(error);
}

static void on_overwrite_answer(gboolean accepted, gpointer user_data)
{
	PendingConfirm *pc = user_data;
	GtkWindow *win = g_weak_ref_get(&pc->window);

	/* win == profile_window : une fenêtre détruite mais encore référencée ailleurs n'est plus la fenêtre de profils (pointeur remis à
	 * NULL dès sa destruction) ; ses widgets ne sont plus utilisables */
	if(win != NULL && win == profile_window && accepted)
		save_now(dialog_of(win), pc->name);
	g_clear_object(&win);
	pending_free(pc);
}

static void on_delete_answer(gboolean accepted, gpointer user_data)
{
	PendingConfirm *pc = user_data;
	GtkWindow *win = g_weak_ref_get(&pc->window);
	GError *error = NULL;

	if(win != NULL && win == profile_window && accepted)
	{
		if(app_config_delete_profile(pc->name, &error))
			gtk_window_destroy(win);
		else
		{
			show_error(dialog_of(win), error != NULL ? error->message : _("Cannot delete this configuration."));
			g_clear_error(&error);
		}
	}
	g_clear_object(&win);
	pending_free(pc);
}

static void on_ok(GtkWidget *button, ProfileDialog *pd)
{
	GtSession *s = weak_session(&pd->session);
	GError *error = NULL;
	gchar *name = NULL;
	gchar *question;

	if(s == NULL)	/* session fermée pendant le dialogue : rien n'est appliqué */
	{
		gtk_window_destroy(pd->window);
		return;
	}
	clear_error(pd);
	switch(pd->mode)
	{
	case PROFILE_LOAD:
		name = selected_name(pd);
		if(name == NULL)
			show_error(pd, _("Select a configuration."));
		else if(app_config_load_profile(s, name, &error))
			gtk_window_destroy(pd->window);	/* crochet profile_applied : options, terminal, couleurs, port rouvert */
		else
		{
			show_error(pd, error != NULL ? error->message : _("Cannot load this configuration."));
			g_clear_error(&error);
		}
		break;
	case PROFILE_DELETE:
		name = selected_name(pd);
		if(name == NULL)
		{
			show_error(pd, _("Select a configuration."));
			break;
		}
		question = g_strdup_printf(_("Delete the configuration \"%s\"?"), name);
		gt_ui_confirm(pd->window, question, _("This cannot be undone."), _("Delete"), on_delete_answer, pending_new(pd, name));
		g_free(question);
		break;
	case PROFILE_SAVE:
		if(!gt_profile_name_validate(gtk_editable_get_text(pd->name), &name, &error))
		{
			show_error(pd, error->message);
			g_clear_error(&error);
		}
		else if(app_config_has_profile(name))
		{
			question = g_strdup_printf(_("A configuration named \"%s\" already exists."), name);
			gt_ui_confirm(pd->window, question, _("Do you want to overwrite it?"), _("Overwrite"), on_overwrite_answer, pending_new(pd, name));
			g_free(question);
		}
		else
			save_now(pd, name);
		break;
	}
	g_free(name);
	g_object_unref(s);
}

static void on_row_activated(GtkListView *list, guint position, ProfileDialog *pd)
{
	gtk_single_selection_set_selected(pd->selection, position);
	on_ok(NULL, pd);
}

static void row_setup(GtkSignalListItemFactory *factory, GObject *object, gpointer data)
{
	GtkWidget *label = gtk_label_new(NULL);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_name(label, "profile-row");
	gtk_list_item_set_child(GTK_LIST_ITEM(object), label);
}

static void row_bind(GtkSignalListItemFactory *factory, GObject *object, gpointer data)
{
	GtkListItem *item = GTK_LIST_ITEM(object);

	/* Texte brut : un nom de profil n'est jamais interprété comme du balisage */
	gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)), gtk_string_object_get_string(gtk_list_item_get_item(item)));
}

/* Liste des profils (relue : profils ajoutés ou supprimés par une autre instance) ; libellé seul si aucun profil n'existe */
static GtkWidget *build_list(ProfileDialog *pd, GtSession *s)
{
	gchar **names = app_config_list_profiles();
	const gchar *active = gt_session_get_profile_name(s);
	GtkStringList *model;
	GtkListItemFactory *factory;
	GtkWidget *list, *scroll;

	if(names[0] == NULL)
	{
		g_strfreev(names);
		return gtk_label_new(_("No saved configuration."));
	}
	model = gtk_string_list_new((const char *const *)names);
	pd->selection = gtk_single_selection_new(G_LIST_MODEL(model));	/* prend le modèle ; autoselect : première ligne par défaut */
	for(guint i = 0; names[i] != NULL; i++)
		if(g_strcmp0(names[i], active) == 0)
			gtk_single_selection_set_selected(pd->selection, i);
	g_strfreev(names);

	factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(row_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(row_bind), NULL);
	list = gtk_list_view_new(GTK_SELECTION_MODEL(pd->selection), factory);	/* prend la sélection et la fabrique */
	gtk_widget_set_name(list, "profile-list");
	g_signal_connect(list, "activate", G_CALLBACK(on_row_activated), pd);

	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
	return scroll;
}

static void profile_dialog_open(ProfileMode mode)
{
	static const gchar *const titles[] = { N_("Load configuration"), N_("Save configuration"), N_("Delete configuration") };
	GtSession *s = gt_app_get_active_session();
	ProfileDialog *pd;
	GtkWidget *box, *buttons, *cancel, *ok, *error;

	if(profile_window != NULL)
	{
		gtk_window_present(profile_window);
		return;
	}
	if(s == NULL || gt_session_is_shut_down(s))
		return;
	pd = g_new0(ProfileDialog, 1);
	pd->mode = mode;
	g_weak_ref_init(&pd->session, s);
	pd->window = GTK_WINDOW(gtk_window_new());
	gtk_widget_set_name(GTK_WIDGET(pd->window), "profile-window");
	gtk_window_set_title(pd->window, _(titles[mode]));
	gtk_window_set_modal(pd->window, TRUE);
	gtk_window_set_transient_for(pd->window, gt_app_get_main_window());
	gtk_window_set_destroy_with_parent(pd->window, TRUE);
	gtk_window_set_default_size(pd->window, 320, mode == PROFILE_SAVE ? -1 : 320);
	g_object_set_data_full(G_OBJECT(pd->window), "profile-dialog", pd, profile_dialog_free);
	g_signal_connect_object(s, "closed", G_CALLBACK(gtk_window_destroy), pd->window, G_CONNECT_SWAPPED);	/* fermé sans rien appliquer */
	profile_window = pd->window;
	g_object_add_weak_pointer(G_OBJECT(pd->window), (gpointer *)&profile_window);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	if(mode == PROFILE_SAVE)
	{
		GtkWidget *label = gtk_label_new_with_mnemonic(_("Configuration _name:"));
		GtkWidget *entry = gtk_entry_new();

		gtk_widget_set_name(entry, "profile-name");
		gtk_editable_set_text(GTK_EDITABLE(entry), gt_session_get_profile_name(s));
		gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
		gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		g_signal_connect_swapped(entry, "changed", G_CALLBACK(clear_error), pd);
		pd->name = GTK_EDITABLE(entry);
		gtk_box_append(GTK_BOX(box), label);
		gtk_box_append(GTK_BOX(box), entry);
	}
	else
		gtk_box_append(GTK_BOX(box), build_list(pd, s));

	error = gtk_label_new(NULL);
	gtk_widget_set_name(error, "profile-error");
	gtk_label_set_wrap(GTK_LABEL(error), TRUE);
	gtk_label_set_xalign(GTK_LABEL(error), 0.0);
	gtk_widget_add_css_class(error, "error");
	gtk_widget_set_visible(error, FALSE);
	pd->error = GTK_LABEL(error);
	gtk_box_append(GTK_BOX(box), error);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
	gtk_widget_set_name(cancel, "profile-cancel");
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), pd->window);
	ok = gtk_button_new_with_mnemonic(mode == PROFILE_DELETE ? _("_Delete") : _("_OK"));
	gtk_widget_set_name(ok, "profile-ok");
	gtk_widget_add_css_class(ok, mode == PROFILE_DELETE ? "destructive-action" : "suggested-action");
	gtk_widget_set_sensitive(ok, mode == PROFILE_SAVE || pd->selection != NULL);
	g_signal_connect(ok, "clicked", G_CALLBACK(on_ok), pd);
	gtk_box_append(GTK_BOX(buttons), cancel);
	gtk_box_append(GTK_BOX(buttons), ok);
	gtk_box_append(GTK_BOX(box), buttons);

	gtk_window_set_default_widget(pd->window, ok);
	gt_ui_close_on_escape(pd->window);	/* dialogs.c (étape 4) */
	gtk_window_set_child(pd->window, box);
	gtk_window_present(pd->window);
	if(pd->name != NULL)
	{
		gtk_widget_grab_focus(GTK_WIDGET(pd->name));
		gtk_editable_select_region(pd->name, 0, -1);
	}
}

/* ---- Fichiers de macros ------------------------------------------------- */

static GFile *macros_folder(GtSession *s)
{
	const gchar *cur = gt_macros_get_path(gt_session_get_macros(s));
	gchar *dir;
	GFile *folder;

	if(cur == NULL || !g_file_test(cur, G_FILE_TEST_EXISTS))
		return g_file_new_for_path(g_get_user_config_dir());
	dir = g_path_get_dirname(cur);
	folder = g_file_new_for_path(dir);
	g_free(dir);
	return folder;
}

/* Sélecteurs de fichiers de macros : user_data = GWeakRef sur la session active à l'ouverture (libéré par le rappel, appelé une fois) */
static GWeakRef *session_weak_new(GtSession *s)
{
	GWeakRef *ref = g_new0(GWeakRef, 1);

	g_weak_ref_init(ref, s);
	return ref;
}

static GtSession *session_weak_take(GWeakRef *ref)
{
	GtSession *s = weak_session(ref);

	g_weak_ref_clear(ref);
	g_free(ref);
	return s;
}

static void on_macros_load_chosen(GFile *file, gpointer user_data)
{
	GtSession *s = session_weak_take(user_data);
	gchar *path;

	if(file == NULL || s == NULL)	/* annulé, fenêtre principale fermée pendant le choix, ou session fermée */
	{
		g_clear_object(&s);
		return;
	}
	path = g_file_get_path(file);
	/* Interactif : un fichier illisible ouvre une boîte ; le jeu et le chemin restent ceux d'avant */
	if(path != NULL && gt_macros_load_file(gt_session_get_macros(s), path, TRUE))
		config_remember_macros_file(s);
	g_free(path);
	g_object_unref(s);
}

static void on_macros_save_chosen(GFile *file, gpointer user_data)
{
	GtSession *s = session_weak_take(user_data);
	gchar *path;

	if(file == NULL || s == NULL)
	{
		g_clear_object(&s);
		return;
	}
	path = g_file_get_path(file);
	if(path != NULL)
	{
		/* Enregistrement explicite : un échec déjà signalé par un enregistrement implicite est de nouveau affiché */
		gt_macros_forget_save_error(gt_session_get_macros(s));
		gt_ui_macros_save(gt_app_get_main_window(), s, path);
	}
	g_free(path);
	g_object_unref(s);
}

/* ---- Actions ------------------------------------------------------------- */

static void on_load_profile(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	profile_dialog_open(PROFILE_LOAD);
}

static void on_save_profile(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	profile_dialog_open(PROFILE_SAVE);
}

static void on_delete_profile(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	profile_dialog_open(PROFILE_DELETE);
}

static void on_load_macros(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSession *s = gt_app_get_active_session();
	GFile *folder;

	if(s == NULL || gt_session_is_shut_down(s))
		return;
	folder = macros_folder(s);
	gt_ui_open_file(gt_app_get_main_window(), _("Load macros file"), folder, NULL, on_macros_load_chosen, session_weak_new(s));
	g_object_unref(folder);
}

static void on_save_macros(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSession *s = gt_app_get_active_session();
	GFile *folder;

	if(s == NULL || gt_session_is_shut_down(s))
		return;
	folder = macros_folder(s);
	gt_ui_save_file(gt_app_get_main_window(), _("Save macros file"), folder, "gtkterm_macros.ini", on_macros_save_chosen, session_weak_new(s));
	g_object_unref(folder);
}

void gt_profile_dialogs_install(GtkApplication *app)
{
	static const GActionEntry entries[] = {
		{ .name = "load-profile", .activate = on_load_profile },
		{ .name = "save-profile", .activate = on_save_profile },
		{ .name = "delete-profile", .activate = on_delete_profile },
		{ .name = "load-macros", .activate = on_load_macros },
		{ .name = "save-macros", .activate = on_save_macros },
	};

	g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);
}
