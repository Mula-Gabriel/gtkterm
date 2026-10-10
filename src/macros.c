/***********************************************************************/
/* macros.c                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Macro state of the application (set, file, poller, sending),   */
/*      without GTK : interface effects through GtAppUi hooks          */
/*                                                                     */
/*   ChangeLog                                                         */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.99.0 : file creation by Julien                             */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "app_ui.h"
#include "link.h"
#include "macros.h"
#include "macro_file.h"

static GtMacroSet *current_set = NULL;
static GtMacroPoller *poller = NULL;
static gchar *current_path = NULL;
static gchar *last_save_error = NULL;		/* dernier échec d'enregistrement signalé (chemin, domaine, code) : un seul message par échec */

static gboolean
poller_send (guint id, gpointer user_data)
{
  return macros_send (id, NULL, -1);
}

static void
poller_state (guint id, gboolean running, gpointer user_data)
{
  const GtAppUi *ui = gt_app_ui_get ();

  if (ui->macro_polling_changed != NULL)
    ui->macro_polling_changed (id, running);
}

void
macros_init (void)
{
  if (current_set != NULL)
    return;
  current_set = gt_macro_set_new ();
  poller = gt_macro_poller_new (current_set, poller_send, poller_state, NULL);
}

GtMacroSet *
macros_get_set (void)
{
  return current_set;
}

GtMacroPoller *
macros_get_poller (void)
{
  return poller;
}

/* gt_macro_poller_free arrête tout sans rappel d'état (gt_macro_poller_reset en émettrait vers des boutons peut-être détruits) */
void
macros_shutdown (void)
{
  gt_macro_poller_free (poller);
  poller = NULL;
}

static gboolean
send_macro (guint id, const gchar *const *args, gint n_args, gboolean announce)
{
  const GtMacro *m = gt_macro_set_find (current_set, id);
  GBytes *bytes;
  const guint8 *data;
  gsize len = 0;
  gssize written = 0;
  gchar *name;

  if (!gt_macro_is_sendable (m))
    return FALSE;
  /* Nom affiché : le libellé, à défaut le raccourci (C8) ; pris avant l'envoi, dont les gestionnaires ne doivent pas toucher au jeu */
  name = g_strdup (m->label[0] != '\0' ? m->label : m->shortcut);
  bytes = gt_macro_expand (current_set, m, args, n_args);
  data = g_bytes_get_data (bytes, &len);
  if (len > 0)
    written = gt_link_send (gt_link_get_default (), data, len, GT_SEND_MACRO);
  g_bytes_unref (bytes);
  /* Rien d'écrit (port fermé ou perdu) : pas de « envoyée », le « — lost » de la barre d'état reste visible */
  if (announce && written > 0)
    gt_app_status_flash (800, _ ("Macro \"%s\" sent!"), name);
  g_free (name);
  return len == 0 || (written >= 0 && (gsize) written == len);
}

gboolean
macros_send (guint id, const gchar *const *args, gint n_args)
{
  return send_macro (id, args, n_args, TRUE);
}

gboolean
macros_send_quiet (guint id, const gchar *const *args, gint n_args)
{
  return send_macro (id, args, n_args, FALSE);
}

void
macros_changed (void)
{
  const GtAppUi *ui = gt_app_ui_get ();

  if (ui->macros_changed != NULL)
    ui->macros_changed ();
}

void
macros_replace_set (GtMacroSet *set)
{
  GtMacroSet *old;

  macros_init ();
  /* L'ordonnanceur s'arrête pendant que l'ancien jeu est encore le jeu courant : les rappels d'état voient des boutons cohérents */
  gt_macro_poller_set_set (poller, set);
  old = current_set;
  current_set = set;
  gt_macro_set_free (old);
  /* Panneau et raccourcis reconstruits par l'interface ; une fenêtre d'édition ouverte sur l'ancien jeu s'y ferme sans enregistrer
     (ses lignes décrivent l'ancien jeu : les appliquer au nouveau perdrait arguments et polling) */
  macros_changed ();
}

const gchar *
macros_file_get_path (void)
{
  return current_path;
}

gchar *
macros_file_default_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "gtkterm_macros.ini", NULL);
}

static gchar *
resolve_path (const gchar *path)
{
  if (path != NULL)
    return g_strdup (path);
  if (current_path != NULL)
    return g_strdup (current_path);
  return macros_file_default_path ();
}

static void
report_error (gboolean interactive, const gchar *text)
{
  if (interactive)
    gt_app_message (GT_APP_MSG_ERROR, "%s", text);
  else
    g_warning ("%s", text);
}

static gboolean
load_error_idle (gpointer data)
{
  gt_app_message (GT_APP_MSG_ERROR, "%s", (const gchar *) data);
  return G_SOURCE_REMOVE;
}

gboolean
macros_load_file (const gchar *path, gboolean interactive)
{
  gchar *load_path = resolve_path (path);
  GtMacroSet *set = NULL;
  GError *error = NULL;
  gboolean ok = TRUE;

  if (!gt_macro_file_load (load_path, &set, &error))
    {
      gboolean missing = g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

      if (!missing && interactive)
        {
          gchar *text = g_strdup_printf (_ ("Cannot load macros file: %s"), error->message);

          /* Choix dans le menu : jeu et chemin courants inchangés, rien n'est perdu */
          gt_app_message (GT_APP_MSG_ERROR, "%s", text);
          g_free (text);
          g_error_free (error);
          g_free (load_path);
          return FALSE;
        }
      if (!missing)
        {
          /* Profil (non interactif) : jeu vide, mais le chemin courant devient ce fichier. Les enregistrements suivants le visent
             (gt_macro_file_save en garde d'abord une copie <fichier>.avant-migration), jamais le fichier par défaut, et le profil
             n'est pas redirigé. Avertissement, plus une boîte différée : pas de boucle imbriquée pendant le chargement du profil,
             et l'utilisateur sait que ses macros n'ont pas été lues. */
          gchar *text = g_strdup_printf (_ ("Cannot load macros file: %s"), error->message);

          g_warning ("%s", text);
          g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, load_error_idle, text, g_free);
          ok = FALSE;
        }
      /* Fichier absent (premier lancement, profil qui pointe un fichier pas encore créé) : jeu vide, créé au premier enregistrement */
      g_clear_error (&error);
      set = gt_macro_set_new ();
    }
  g_free (current_path);
  current_path = load_path;
  g_clear_pointer (&last_save_error, g_free);
  macros_replace_set (set);
  return ok;
}

void
macros_forget_save_error (void)
{
  g_clear_pointer (&last_save_error, g_free);
}

gboolean
macros_save_file (const gchar *path, gboolean interactive)
{
  gchar *save_path = resolve_path (path);
  GError *error = NULL;

  if (!gt_macro_file_save (current_set, save_path, &error))
    {
      /* Chaque frappe dans un argument enregistre : un même échec n'ouvre qu'une boîte. L'échec est reconnu à son chemin et à son
         erreur, pas au texte : celui de g_file_set_contents cite un fichier temporaire au nom aléatoire, différent à chaque essai. */
      gchar *key = g_strdup_printf ("%s\n%s:%d", save_path, g_quark_to_string (error->domain), error->code);

      if (g_strcmp0 (key, last_save_error) != 0)
        {
          gchar *text = g_strdup_printf (_ ("Cannot save macros file: %s"), error->message);

          report_error (interactive, text);
          g_free (text);
        }
      g_free (last_save_error);
      last_save_error = key;
      g_error_free (error);
      g_free (save_path);
      return FALSE;
    }
  g_clear_pointer (&last_save_error, g_free);
  g_free (current_path);
  current_path = save_path;
  return TRUE;
}
