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
#include "macros_format.h"
#include "file_stamp.h"

struct _GtMacros
{
  GtkTermLink *link;
  GtSession *owner;		/* non possédée : passée aux crochets */
  GtMacroSet *set;
  GtMacroPoller *poller;
  gchar *path;
  gchar *last_save_error;	/* dernier échec d'enregistrement signalé (chemin, domaine, code) : un seul message par échec */
  gboolean shut_down;
  GHashTable *usage;		/* id → gchar ** (possédés) : valeurs d'usage des arguments, jamais enregistrées */
  GHashTable *resolved;		/* id → gchar ** (possédés) : dernier vecteur rendu par gt_macros_get_args */
  GHashTable *refused_at;	/* id → gint64 * (possédés) : heure monotone du dernier message de refus */
  gboolean embedded;		/* onglet de projet : aucun fichier retenu */
  GtFileStamp stamp;		/* identité du fichier courant à sa dernière lecture ou écriture */
  void (*edited) (gpointer data);
  gpointer edited_data;
};

static gboolean
poller_send (guint id, gpointer user_data)
{
  return gt_macros_send (user_data, id, NULL, -1);
}

static void
poller_state (guint id, gboolean running, gpointer user_data)
{
  GtMacros *m = user_data;
  const GtAppUi *ui = gt_app_ui_get ();

  if (ui->macro_polling_changed != NULL)
    ui->macro_polling_changed (m->owner, id, running);
}

GtMacros *
gt_macros_new (GtkTermLink *link, GtSession *owner)
{
  GtMacros *m = g_new0 (GtMacros, 1);

  m->link = g_object_ref (link);
  m->owner = owner;
  m->set = gt_macro_set_new ();
  m->usage = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) g_strfreev);
  m->resolved = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) g_strfreev);
  m->refused_at = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
  m->poller = gt_macro_poller_new (m->set, poller_send, poller_state, m);
  return m;
}

GtSession *
gt_macros_get_owner (GtMacros *m)
{
  return m->owner;
}

GtkTermLink *
gt_macros_get_link (GtMacros *m)
{
  return m->link;
}

GtMacroSet *
gt_macros_get_set (GtMacros *m)
{
  return m->set;
}

GtMacroPoller *
gt_macros_get_poller (GtMacros *m)
{
  return m->poller;
}

/* gt_macro_poller_free arrête tout sans rappel d'état (gt_macro_poller_reset en émettrait vers des boutons peut-être détruits) */
void
gt_macros_shutdown (GtMacros *m)
{
  if (m == NULL)
    return;
  gt_macro_poller_free (m->poller);
  m->poller = NULL;
  m->shut_down = TRUE;
  m->edited = NULL;
  m->edited_data = NULL;
}

void
gt_macros_free (GtMacros *m)
{
  if (m == NULL)
    return;
  gt_macros_shutdown (m);
  gt_macro_set_free (m->set);
  g_free (m->path);
  g_free (m->last_save_error);
  g_hash_table_unref (m->usage);
  g_hash_table_unref (m->resolved);
  g_hash_table_unref (m->refused_at);
  g_object_unref (m->link);
  g_free (m);
}

#define REFUSAL_FLASH_US (3 * G_USEC_PER_SEC)

static gboolean
send_macro (GtMacros *m, guint id, const gchar *const *args, gint n_args, gboolean announce)
{
  const GtMacro *macro = gt_macro_set_find (m->set, id);
  const gchar *const *use_args = args;
  GBytes *bytes;
  const guint8 *data;
  gsize len = 0;
  gssize written = 0;
  gchar *name;
  gchar *reason = NULL;
  gint bad = -1;

  if (m->shut_down || !gt_macro_is_sendable (macro))
    return FALSE;
  /* Nom affiché : le libellé, à défaut le raccourci (C8) ; pris avant l'envoi, dont les gestionnaires ne doivent pas toucher au jeu */
  name = g_strdup (macro->label[0] != '\0' ? macro->label : macro->shortcut);
  /* Arguments enregistrés : valeurs d'usage ou défauts, déjà résolus ici (jamais -1 vers gt_macro_expand) */
  if (n_args < 0)
    {
      use_args = gt_macros_get_args (m, id);
      n_args = use_args != NULL ? (gint) g_strv_length ((gchar **) use_args) : 0;
    }
  /* Arguments hors limites (spec arguments §2.8) : rien n'est écrit ; un message au plus toutes les 3 s par macro (polling rapide) */
  if (!gt_macro_check_args (m->set, macro, use_args, n_args, &bad, &reason))
    {
      gint64 now = g_get_monotonic_time ();
      gint64 *last = g_hash_table_lookup (m->refused_at, GUINT_TO_POINTER (id));

      if (announce && (last == NULL || now - *last >= REFUSAL_FLASH_US))
        {
          gint64 *stamp = g_new (gint64, 1);

          *stamp = now;
          g_hash_table_insert (m->refused_at, GUINT_TO_POINTER (id), stamp);
          gt_app_status_flash (m->owner, 3000, _ ("Macro \"%s\" not sent: argument %d: %s"), name, bad + 1,
                               reason != NULL ? reason : "");
        }
      g_free (reason);
      g_free (name);
      return FALSE;
    }
  bytes = gt_macro_expand (m->set, macro, use_args, n_args);
  data = g_bytes_get_data (bytes, &len);
  if (len > 0)
    written = gt_link_send (m->link, data, len, GT_SEND_MACRO);
  g_bytes_unref (bytes);
  /* Rien d'écrit (port fermé ou perdu) : pas de « envoyée », le « — lost » de la barre d'état reste visible */
  if (announce && written > 0)
    gt_app_status_flash (m->owner, 800, _ ("Macro \"%s\" sent!"), name);
  g_free (name);
  return len == 0 || (written >= 0 && (gsize) written == len);
}

gboolean
gt_macros_send (GtMacros *m, guint id, const gchar *const *args, gint n_args)
{
  return send_macro (m, id, args, n_args, TRUE);
}

gboolean
gt_macros_send_quiet (GtMacros *m, guint id, const gchar *const *args, gint n_args)
{
  return send_macro (m, id, args, n_args, FALSE);
}

void
gt_macros_changed (GtMacros *m)
{
  const GtAppUi *ui = gt_app_ui_get ();

  if (ui->macros_changed != NULL)
    ui->macros_changed (m->owner);
  gt_macros_edited (m);
}

void
gt_macros_edited (GtMacros *m)
{
  if (m->edited != NULL)
    m->edited (m->edited_data);
}

void
gt_macros_set_edited_func (GtMacros *m, void (*func) (gpointer data), gpointer data)
{
  m->edited = func;
  m->edited_data = data;
}

void
gt_macros_set_embedded (GtMacros *m, gboolean embedded)
{
  m->embedded = embedded;
  if (embedded)
    {
      g_clear_pointer (&m->path, g_free);
      memset (&m->stamp, 0, sizeof m->stamp);
    }
}

gboolean
gt_macros_is_embedded (GtMacros *m)
{
  return m->embedded;
}

/* Valeurs d'usage de la macro si elles ont le bon nombre (n = nombre d'arguments de son action), sinon NULL */
static gchar **
usage_of (GtMacros *m, const GtMacro *macro, gint n)
{
  gchar **usage = g_hash_table_lookup (m->usage, GUINT_TO_POINTER (macro->id));

  if (usage == NULL || (gint) g_strv_length (usage) != n)
    return NULL;
  return usage;
}

/* Les entrées dérivées des valeurs d'usage ou du jeu (vecteur résolu, délai du message de refus) ; id 0 : toutes */
static void
forget_derived (GtMacros *m, guint id)
{
  if (id == 0)
    {
      g_hash_table_remove_all (m->resolved);
      g_hash_table_remove_all (m->refused_at);
    }
  else
    {
      g_hash_table_remove (m->resolved, GUINT_TO_POINTER (id));
      g_hash_table_remove (m->refused_at, GUINT_TO_POINTER (id));
    }
}

const gchar *const *
gt_macros_get_args (GtMacros *m, guint id)
{
  const GtMacro *macro = gt_macro_set_find (m->set, id);
  gchar **usage, **defaults, **out;
  gboolean any_usage = FALSE;
  gint n;

  g_hash_table_remove (m->resolved, GUINT_TO_POINTER (id));
  if (macro == NULL)
    return NULL;
  n = macro_count_format_args (gt_macro_set_get_lists (m->set), macro->action);
  usage = usage_of (m, macro, n);
  defaults = macro->args != NULL && (gint) g_strv_length (macro->args) == n ? macro->args : NULL;
  for (gint i = 0; usage != NULL && i < n; i++)
    if (usage[i][0] != '\0')
      any_usage = TRUE;
  /* Ni défaut ni valeur d'usage : l'action part avec ses spécificateurs (« V%d » envoie « V%d »), comme avant les valeurs d'usage */
  if (defaults == NULL && !any_usage)
    return NULL;
  out = g_new0 (gchar *, n + 1);
  for (gint i = 0; i < n; i++)
    {
      if (usage != NULL && usage[i][0] != '\0')
        out[i] = g_strdup (usage[i]);
      else
        out[i] = g_strdup (defaults != NULL ? defaults[i] : "");
    }
  g_hash_table_insert (m->resolved, GUINT_TO_POINTER (id), out);
  return (const gchar *const *) out;
}

const gchar *const *
gt_macros_get_usage (GtMacros *m, guint id)
{
  const GtMacro *macro = gt_macro_set_find (m->set, id);

  if (macro == NULL)
    return NULL;
  return (const gchar *const *) usage_of (m, macro, macro_count_format_args (gt_macro_set_get_lists (m->set), macro->action));
}

gint
gt_macros_check_args (GtMacros *m, guint id, const gchar *const *args, gint n_args, gchar **reason)
{
  const GtMacro *macro = gt_macro_set_find (m->set, id);
  gint index = -1;

  if (reason != NULL)
    *reason = NULL;
  if (macro == NULL)
    return -1;
  if (n_args < 0)
    {
      args = gt_macros_get_args (m, id);
      n_args = args != NULL ? (gint) g_strv_length ((gchar **) args) : 0;
    }
  if (gt_macro_check_args (m->set, macro, args, n_args, &index, reason))
    return -1;
  return index;
}

void
gt_macros_set_usage_arg (GtMacros *m, guint id, gint arg_index, const gchar *value)
{
  const GtMacro *macro = gt_macro_set_find (m->set, id);
  gint n;
  gchar **usage;

  if (macro == NULL)
    return;
  n = macro_count_format_args (gt_macro_set_get_lists (m->set), macro->action);
  if (arg_index < 0 || arg_index >= n)
    return;
  usage = g_hash_table_lookup (m->usage, GUINT_TO_POINTER (id));
  if (usage == NULL || (gint) g_strv_length (usage) != n)
    {
      /* Vecteur créé vide, au bon nombre : un rang vide prend la valeur par défaut à la résolution (spec arguments §2.7) */
      gchar **fresh = g_new0 (gchar *, n + 1);

      for (gint i = 0; i < n; i++)
        fresh[i] = g_strdup ("");
      g_hash_table_insert (m->usage, GUINT_TO_POINTER (id), fresh);
      usage = fresh;
    }
  g_free (usage[arg_index]);
  usage[arg_index] = g_strdup (value != NULL ? value : "");
  forget_derived (m, id);
}

void
gt_macros_reset_usage (GtMacros *m, guint id)
{
  if (id == 0)
    g_hash_table_remove_all (m->usage);
  else
    g_hash_table_remove (m->usage, GUINT_TO_POINTER (id));
  forget_derived (m, id);
}

gboolean
gt_macros_has_usage (GtMacros *m, guint id)
{
  if (id == 0)
    return g_hash_table_size (m->usage) > 0;
  return g_hash_table_contains (m->usage, GUINT_TO_POINTER (id));
}

void
gt_macros_replace_set (GtMacros *m, GtMacroSet *set)
{
  GtMacroSet *old;

  /* L'ordonnanceur s'arrête pendant que l'ancien jeu est encore le jeu courant : les rappels d'état voient des boutons cohérents */
  gt_macro_poller_set_set (m->poller, set);
  old = m->set;
  m->set = set;
  gt_macro_set_free (old);
  g_hash_table_remove_all (m->usage);
  forget_derived (m, 0);
  /* Panneau et raccourcis reconstruits par l'interface ; une fenêtre d'édition ouverte sur l'ancien jeu s'y ferme sans enregistrer
     (ses lignes décrivent l'ancien jeu : les appliquer au nouveau perdrait arguments et polling) */
  gt_macros_changed (m);
}

static gboolean
args_equal (const gchar *const *a, const gchar *const *b)
{
  static const gchar *const none[] = { NULL };

  return g_strv_equal (a != NULL ? a : none, b != NULL ? b : none);
}

void
gt_macros_apply_edits (GtMacros *m, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists)
{
  GHashTable *before = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) g_strfreev);
  GHashTableIter iter;
  gpointer key;

  /* Valeurs par défaut avant l'application, comparées après (check_args a pu les ajuster aux listes définitives) */
  for (guint i = 0; i < gt_macro_set_count (m->set); i++)
    {
      const GtMacro *macro = gt_macro_set_get (m->set, i);

      g_hash_table_insert (before, GUINT_TO_POINTER (macro->id), g_strdupv (macro->args));
    }
  gt_macro_set_apply_edits (m->set, rows, n_rows, lists);
  g_hash_table_iter_init (&iter, m->usage);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      const GtMacro *macro = gt_macro_set_find (m->set, GPOINTER_TO_UINT (key));

      /* Macro supprimée, ou valeurs par défaut changées : les valeurs d'usage masqueraient la nouvelle valeur par défaut */
      if (macro == NULL || !args_equal ((const gchar *const *) macro->args, g_hash_table_lookup (before, key)))
        g_hash_table_iter_remove (&iter);
    }
  g_hash_table_unref (before);
  forget_derived (m, 0);
}

const gchar *
gt_macros_get_path (GtMacros *m)
{
  return m->path;
}

gchar *
macros_file_default_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "gtkterm_macros.ini", NULL);
}

static gchar *
resolve_path (GtMacros *m, const gchar *path)
{
  if (path != NULL)
    return g_strdup (path);
  if (m->path != NULL)
    return g_strdup (m->path);
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
gt_macros_load_file (GtMacros *m, const gchar *path, gboolean interactive)
{
  gchar *load_path = resolve_path (m, path);
  GtMacroSet *set = NULL;
  GError *error = NULL;
  gboolean ok = TRUE;
  GtFileStamp stamp;

  /* Identité relevée avant la lecture (comme project_file.c et config_store.c) : jamais plus récente que le contenu chargé, une réécriture
     pendant la lecture sera vue comme un conflit au prochain enregistrement */
  gt_file_stamp_take (load_path, &stamp);
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
  /* Mode intégré : le fichier est importé, son chemin n'est pas retenu. Identité prise avant la lecture (absent : « absent »). */
  if (!m->embedded)
    {
      g_free (m->path);
      m->path = g_strdup (load_path);
      m->stamp = stamp;
    }
  g_free (load_path);
  g_clear_pointer (&m->last_save_error, g_free);
  gt_macros_replace_set (m, set);
  return ok;
}

void
gt_macros_forget_save_error (GtMacros *m)
{
  g_clear_pointer (&m->last_save_error, g_free);
}

GtMacrosSaveResult
gt_macros_save_file (GtMacros *m, const gchar *path, gboolean interactive, gboolean force)
{
  gchar *save_path;
  GError *error = NULL;

  if (path == NULL && m->embedded && m->path == NULL)
    {
      report_error (interactive, _ ("This tab has no macros file: its macros are saved with the project."));
      return GT_MACROS_FAILED;
    }
  save_path = resolve_path (m, path);
  /* Garde d'identité : le fichier courant a été réécrit par un tiers depuis notre dernière lecture ou écriture */
  if (!force && g_strcmp0 (save_path, m->path) == 0 && gt_file_stamp_changed (save_path, &m->stamp))
    {
      if (!interactive)
        g_warning (_ ("%s changed on disk: not overwritten"), save_path);
      g_free (save_path);
      return GT_MACROS_CONFLICT;
    }
  /* Aucun fichier courant (aucun chargé ni enregistré) : la cible est le fichier par défaut, jamais lu par cet onglet ; présent et non vide,
     il n'est pas réécrit en silence (conflit) */
  if (!force && path == NULL && m->path == NULL)
    {
      GtFileStamp target;

      gt_file_stamp_take (save_path, &target);
      if (target.exists && target.size > 0)
        {
          if (!interactive)
            g_warning (_ ("%s already exists: not overwritten"), save_path);
          g_free (save_path);
          return GT_MACROS_CONFLICT;
        }
    }
  if (!gt_macro_file_save (m->set, save_path, &error))
    {
      /* Chaque frappe dans un argument enregistre : un même échec n'ouvre qu'une boîte. L'échec est reconnu à son chemin et à son
         erreur, pas au texte : celui de g_file_set_contents cite un fichier temporaire au nom aléatoire, différent à chaque essai. */
      gchar *key = g_strdup_printf ("%s\n%s:%d", save_path, g_quark_to_string (error->domain), error->code);

      if (g_strcmp0 (key, m->last_save_error) != 0)
        {
          gchar *text = g_strdup_printf (_ ("Cannot save macros file: %s"), error->message);

          report_error (interactive, text);
          g_free (text);
        }
      g_free (m->last_save_error);
      m->last_save_error = key;
      g_error_free (error);
      g_free (save_path);
      return GT_MACROS_FAILED;
    }
  g_clear_pointer (&m->last_save_error, g_free);
  if (!m->embedded)
    {
      g_free (m->path);
      m->path = save_path;
      gt_file_stamp_take (m->path, &m->stamp);
    }
  else
    g_free (save_path);
  return GT_MACROS_SAVED;
}

gboolean
gt_macros_reload (GtMacros *m)
{
  gchar *path;
  gboolean ok;

  if (m->path == NULL && m->embedded)
    return FALSE;
  path = g_strdup (m->path);	/* NULL : le fichier par défaut (cible d'un enregistrement sans fichier courant) */
  ok = gt_macros_load_file (m, path, TRUE);
  g_free (path);
  return ok;
}
