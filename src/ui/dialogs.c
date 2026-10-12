/* Boîtes de l'interface GTK 4 : voir dialogs.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "app.h"
#include "dialogs.h"
#include "macro_editor.h"
#include "macro_panel.h"
#include "macro_shortcuts.h"
#include "session_view.h"
#include "status_bar.h"

/* ---------- Boîte modale construite par GTKTerm (messages et confirmations) ----------
 * Fenêtre modale, transitoire du parent, détruite avec lui ; texte principal et détail en texte brut ; boutons ; Échap = fermer.
 * Durée de vie maîtrisée par API publiques seulement (pas de GtkAlertDialog : sa GtkMessageDialog, détruite avec le parent sans
 * référence gardée, était ensuite redétruite libérée par gtk_alert_dialog_choose_finish — CRITICAL mesuré à la fermeture de GTKTerm).
 * Un seul point de sortie, « destroy » (bouton, Échap, parent détruit, fermeture) : le rappel est appelé une fois, depuis une source
 * d'inactivité, avec l'indice du bouton cliqué ou -1. */

typedef void (*AlertDone)(gint response, gpointer user_data);

typedef struct
{
	AlertDone done;
	gpointer user_data;
	gint response;	/* -1 tant qu'aucun bouton n'a été cliqué */
} AlertData;

static gboolean alert_done_idle(gpointer user_data)
{
	AlertData *ad = user_data;

	ad->done(ad->response, ad->user_data);
	g_free(ad);
	return G_SOURCE_REMOVE;
}

static void on_alert_destroy(GtkWidget *win, AlertData *ad)
{
	g_idle_add(alert_done_idle, ad);
}

static void on_alert_button(GtkButton *button, GtkWindow *win)
{
	AlertData *ad = g_object_get_data(G_OBJECT(win), "gt-alert");

	ad->response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "gt-response"));
	gtk_window_destroy(win);
}

static GtkWidget *alert_label(const gchar *text, gboolean heading)
{
	GtkWidget *label = gtk_label_new(NULL);

	gtk_label_set_text(GTK_LABEL(label), text);	/* jamais de balisage */
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	if(heading)
		gtk_widget_add_css_class(label, "heading");
	return label;
}

/* buttons : libellés à mnémonique, terminés par NULL ; default_button : indice du bouton par défaut (focus et Entrée), « suggested-action »
 * sauf s'il y a un bouton destructeur ; destructive_button : indice du bouton « destructive-action » (-1 : aucun) */
static GtkWindow *alert_show(GtkWindow *parent, const gchar *text, const gchar *detail, const gchar *const *buttons, gint default_button,
                             gint destructive_button, AlertDone done, gpointer user_data)
{
	GtkWindow *win = GTK_WINDOW(gtk_window_new());
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *def = NULL;
	AlertData *ad = g_new0(AlertData, 1);

	ad->done = done;
	ad->user_data = user_data;
	ad->response = -1;
	g_object_set_data(G_OBJECT(win), "gt-alert", ad);
	gtk_widget_add_css_class(GTK_WIDGET(win), "dialog");
	gtk_widget_add_css_class(GTK_WIDGET(win), "message");
	gtk_window_set_modal(win, TRUE);
	gtk_window_set_resizable(win, FALSE);
	if(parent != NULL)
	{
		gtk_window_set_transient_for(win, parent);
		gtk_window_set_destroy_with_parent(win, TRUE);
	}
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 18);
	gtk_widget_set_margin_start(box, 18);
	gtk_widget_set_margin_end(box, 18);
	gtk_box_append(GTK_BOX(box), alert_label(text, TRUE));
	if(detail != NULL && *detail != '\0')
		gtk_box_append(GTK_BOX(box), alert_label(detail, FALSE));
	gtk_widget_set_halign(row, GTK_ALIGN_END);
	gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
	for(gint i = 0; buttons[i] != NULL; i++)
	{
		GtkWidget *b = gtk_button_new_with_mnemonic(buttons[i]);

		g_object_set_data(G_OBJECT(b), "gt-response", GINT_TO_POINTER(i));
		g_signal_connect(b, "clicked", G_CALLBACK(on_alert_button), win);
		gtk_box_append(GTK_BOX(row), b);
		if(i == default_button)
			def = b;
		if(i == destructive_button)
			gtk_widget_add_css_class(b, "destructive-action");
	}
	gtk_box_append(GTK_BOX(box), row);
	gtk_window_set_child(win, box);
	if(def != NULL)
	{
		if(destructive_button < 0)
			gtk_widget_add_css_class(def, "suggested-action");
		gtk_window_set_default_widget(win, def);
		gtk_window_set_focus(win, def);	/* pas le texte sélectionnable */
	}
	gt_ui_close_on_escape(win);
	g_signal_connect(win, "destroy", G_CALLBACK(on_alert_destroy), ad);
	gtk_window_present(win);
	return win;
}

/* ---------- File des messages ---------- */

typedef struct
{
	GtAppMessageKind kind;
	gchar *text;
	GWeakRef parent;
} PendingMessage;

static GQueue pending = G_QUEUE_INIT;
static GtkWindow *main_window;	/* pointeur faible */
static GtkWindow *showing_window;	/* boîte affichée (pointeur faible) : détruite à la fermeture */
static gboolean window_ready, showing, closed;

static const gchar *kind_title(GtAppMessageKind kind)
{
	switch(kind)
	{
		case GT_APP_MSG_INFO:
			return _("Information");
		case GT_APP_MSG_WARNING:
			return _("Warning");
		default:
			return _("Error");
	}
}

static void pending_free(PendingMessage *m)
{
	g_weak_ref_clear(&m->parent);
	g_free(m->text);
	g_free(m);
}

static void show_next(void);

static void on_message_done(gint response, gpointer user_data)
{
	showing = FALSE;
	show_next();
}

static void show_next(void)
{
	const char *buttons[] = { _("_OK"), NULL };
	PendingMessage *m;
	GtkWindow *parent;

	if(!window_ready || showing || closed || (m = g_queue_pop_head(&pending)) == NULL)
		return;
	parent = g_weak_ref_get(&m->parent);
	if(parent == NULL && main_window != NULL)
		parent = g_object_ref(main_window);
	showing = TRUE;
	showing_window = alert_show(parent, kind_title(m->kind), m->text, buttons, 0, -1, on_message_done, NULL);
	g_object_add_weak_pointer(G_OBJECT(showing_window), (gpointer *)&showing_window);
	g_clear_object(&parent);
	pending_free(m);
}

void gt_ui_message(GtkWindow *parent, GtAppMessageKind kind, const char *text)
{
	PendingMessage *m;

	if(closed)
	{
		g_printerr("%s: %s\n", kind_title(kind), text);
		return;
	}
	m = g_new0(PendingMessage, 1);
	m->kind = kind;
	m->text = g_strdup(text);
	g_weak_ref_init(&m->parent, parent);
	g_queue_push_tail(&pending, m);
	show_next();
}

void gt_ui_main_window_ready(GtkWindow *win)
{
	main_window = win;
	g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&main_window);
	window_ready = TRUE;
	show_next();
}

void gt_ui_messages_shutdown(void)
{
	PendingMessage *m;

	if(closed)
		return;
	closed = TRUE;
	if(showing_window != NULL)
		gtk_window_destroy(showing_window);	/* son rappel différé ne relance rien : closed */
	while((m = g_queue_pop_head(&pending)) != NULL)
	{
		g_printerr("%s: %s\n", kind_title(m->kind), m->text);
		pending_free(m);
	}
}

/* Annule l'opération asynchrone quand la fenêtre parente est détruite (fermeture de GTKTerm pendant un sélecteur ouvert, Review Focus 2).
 * La connexion disparaît avec la GCancellable (g_signal_connect_object) ; le rappel de fin la libère. */
static GCancellable *cancellable_for_parent(GtkWindow *parent)
{
	GCancellable *c = g_cancellable_new();

	if(parent != NULL)
		g_signal_connect_object(parent, "destroy", G_CALLBACK(g_cancellable_cancel), c, G_CONNECT_SWAPPED);
	return c;
}

/* ---------- Confirmation ---------- */

typedef struct
{
	GtUiConfirmCb cb;
	gpointer user_data;
} ConfirmData;

/* Déjà différé (alert_done_idle) : l'appelant peut détruire la fenêtre parente ici */
static void on_confirm_done(gint response, gpointer user_data)
{
	ConfirmData *cd = user_data;

	cd->cb(response == 1, cd->user_data);
	g_free(cd);
}

void gt_ui_confirm(GtkWindow *parent, const char *question, const char *detail, const char *accept_label, GtUiConfirmCb cb, gpointer user_data)
{
	const char *buttons[] = { _("_Cancel"), accept_label, NULL };
	ConfirmData *cd = g_new0(ConfirmData, 1);

	cd->cb = cb;
	cd->user_data = user_data;
	alert_show(parent != NULL ? parent : gt_app_get_main_window(), question, detail, buttons, 1, -1, on_confirm_done, cd);
}

/* ---------- Choix ---------- */

typedef struct
{
	GtUiChooseCb cb;
	gpointer user_data;
} ChooseData;

/* Déjà différé (alert_done_idle) */
static void on_choose_done(gint response, gpointer user_data)
{
	ChooseData *cd = user_data;

	cd->cb(response, cd->user_data);
	g_free(cd);
}

void gt_ui_choose(GtkWindow *parent, const char *text, const char *detail, const char *const *labels, gint default_choice, gint destructive_choice,
                  GtUiChooseCb cb, gpointer user_data)
{
	ChooseData *cd = g_new0(ChooseData, 1);

	cd->cb = cb;
	cd->user_data = user_data;
	alert_show(parent != NULL ? parent : gt_app_get_main_window(), text, detail, labels, default_choice, destructive_choice, on_choose_done, cd);
}

/* ---------- Sélecteurs de fichiers ---------- */

typedef struct
{
	GtUiFileCb cb;
	gpointer user_data;
	gboolean save;
	GCancellable *cancel;
	GFile *file;	/* choix, NULL si annulé */
} FileData;

static gboolean file_answer_idle(gpointer user_data)
{
	FileData *fd = user_data;

	/* Parent détruit entre la réponse et ce rappel (fichier choisi pendant app.quit) : annulé, les services sont peut-être arrêtés */
	if(g_cancellable_is_cancelled(fd->cancel))
		g_clear_object(&fd->file);
	fd->cb(fd->file, fd->user_data);
	g_clear_object(&fd->file);
	g_object_unref(fd->cancel);
	g_free(fd);
	return G_SOURCE_REMOVE;
}

/* Rappel différé, comme pour la confirmation : jamais pendant l'émission « response » du sélecteur, même règle pour tous les appelants */
static void on_file_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FileData *fd = user_data;
	GError *error = NULL;

	if(fd->save)
		fd->file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, &error);
	else
		fd->file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
	g_clear_error(&error);	/* annulé ou fermé : file NULL */
	g_idle_add(file_answer_idle, fd);
}

static GtkFileDialog *file_dialog_new(const char *title, GtUiFileCb cb, gpointer user_data, gboolean save, FileData **out)
{
	GtkFileDialog *dialog = gtk_file_dialog_new();

	*out = g_new0(FileData, 1);
	(*out)->cb = cb;
	(*out)->user_data = user_data;
	(*out)->save = save;
	gtk_file_dialog_set_title(dialog, title);
	gtk_file_dialog_set_modal(dialog, TRUE);
	return dialog;
}

void gt_ui_open_file(GtkWindow *parent, const char *title, GFile *initial, GListModel *filters, GtUiFileCb cb, gpointer user_data)
{
	FileData *fd;
	GtkFileDialog *dialog = file_dialog_new(title, cb, user_data, FALSE, &fd);
	GtkWindow *owner = parent != NULL ? parent : gt_app_get_main_window();

	if(initial != NULL)
	{
		if(g_file_query_file_type(initial, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
			gtk_file_dialog_set_initial_folder(dialog, initial);
		else
			gtk_file_dialog_set_initial_file(dialog, initial);
	}
	if(filters != NULL)
		gtk_file_dialog_set_filters(dialog, filters);
	fd->cancel = cancellable_for_parent(owner);
	gtk_file_dialog_open(dialog, owner, fd->cancel, on_file_done, fd);
	g_object_unref(dialog);
}

void gt_ui_save_file(GtkWindow *parent, const char *title, GFile *initial_folder, const char *initial_name, GtUiFileCb cb, gpointer user_data)
{
	FileData *fd;
	GtkFileDialog *dialog = file_dialog_new(title, cb, user_data, TRUE, &fd);
	GtkWindow *owner = parent != NULL ? parent : gt_app_get_main_window();

	if(initial_folder != NULL)
		gtk_file_dialog_set_initial_folder(dialog, initial_folder);
	if(initial_name != NULL)
		gtk_file_dialog_set_initial_name(dialog, initial_name);
	fd->cancel = cancellable_for_parent(owner);
	gtk_file_dialog_save(dialog, owner, fd->cancel, on_file_done, fd);
	g_object_unref(dialog);
}

/* ---------- Crochets de la couche application ---------- */

static void hook_message(GtAppMessageKind kind, const gchar *text)
{
	gt_ui_message(NULL, kind, text);
}

/* Profil chargé dans session par app_config_load_profile (menu Load configuration) : profil de cette session réappliqué (options, terminal,
 * couleurs) et son port rouvert. Au démarrage, la page n'existe pas encore et « activate » applique le profil elle-même ; une session
 * (ou sa page) arrêtée n'est plus touchée. */
static void hook_profile_applied(GtSession *session)
{
	GtkWidget *view = gt_session_view_lookup(session);

	if(view == NULL || gt_session_is_shut_down(session))
		return;
	gt_session_apply_profile(session);	/* un seul « changed » des options : un seul redessin du terminal */
	gt_session_view_apply_profile(view);
	gt_session_open_port(session);
}

/* Message temporaire dans la barre d'état de la page de session ; rien sans page ou si elle est arrêtée */
static void hook_status_flash(GtSession *session, const gchar *text, guint ms)
{
	GtkWidget *view = gt_session_view_lookup(session);

	if(view != NULL)
		gt_status_bar_flash(gt_session_view_get_slot(view, "status-bar"), text, ms);
}

/* Jeu de macros de session remplacé ou modifié : raccourcis et panneau de sa page reconstruits (sans page ou page arrêtée, par exemple avant
 * la fenêtre principale : rien) ; son éditeur ouvert sur l'ancien jeu est fermé sans enregistrer */
static void ui_macros_changed(GtSession *session)
{
	GtkWidget *view = gt_session_view_lookup(session);

	gt_macro_editor_macros_changed(session);
	if(view == NULL)
		return;
	gt_macro_shortcuts_rebuild(view);
	gt_macro_panel_rebuild(gt_session_view_get_slot(view, "macro-panel"));
}

/* Polling d'une macro de session démarré ou arrêté : panneau de sa page (rien sans page ou page arrêtée) */
static void hook_polling_changed(GtSession *session, guint id, gboolean running)
{
	GtkWidget *view = gt_session_view_lookup(session);

	if(view != NULL)
		gt_macro_panel_polling_changed(gt_session_view_get_slot(view, "macro-panel"), id, running);
}

void gt_ui_install_app_hooks(void)
{
	static const GtAppUi ui =
	{
		.message = hook_message,
		.profile_applied = hook_profile_applied,
		.status_flash = hook_status_flash,
		.macros_changed = ui_macros_changed,
		.macro_polling_changed = hook_polling_changed,
	};

	gt_app_ui_set(&ui);
}

/* Échap ferme la fenêtre (action window.close de GtkWindow, donc close-request) : fenêtres et dialogues de l'interface GTK 4 */
void gt_ui_close_on_escape(GtkWindow *win)
{
	GtkEventController *keys = gtk_shortcut_controller_new();

	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(keys),
	                                     gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, 0), gtk_named_action_new("window.close")));
	gtk_widget_add_controller(GTK_WIDGET(win), keys);
}
