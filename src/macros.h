/***********************************************************************/
/* macros.h                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Macro state of the application (set, file, poller, sending),   */
/*      without GTK                                                    */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_H_
#define MACROS_H_

#include <glib.h>

#include "app_ui.h"
#include "link.h"
#include "macro_override.h"
#include "macro_set.h"
#include "macro_poller.h"

/* État des macros d'un onglet (sous-projet 4, instances en 6b) : jeu, chemin du fichier, ordonnanceur de polling sur la liaison de l'onglet. Sans GTK ;
 * fil principal seulement. Effets d'interface par les crochets GtAppUi (macros_changed, macro_polling_changed, status_flash, message), avec owner. */
typedef struct _GtMacros GtMacros;

typedef enum { GT_MACROS_SAVED, GT_MACROS_FAILED, GT_MACROS_CONFLICT } GtMacrosSaveResult;
/* Classement des lignes de « Configure Macros » d'un onglet lié (spec modèles §2.7) */
typedef enum { GT_MACROS_EDIT_NONE, GT_MACROS_EDIT_DEFAULTS, GT_MACROS_EDIT_STRUCTURE } GtMacrosEditKind;
/* Modèle relu à l'ouverture d'un projet (spec modèles §2.5) */
typedef enum { GT_MACROS_TEMPLATE_UNCHANGED, GT_MACROS_TEMPLATE_UPDATED, GT_MACROS_TEMPLATE_MISSING } GtMacrosTemplateStatus;

/* Jeu vide, aucun fichier ; envois et polling sur link (référence gardée) ; owner : session passée aux crochets (non possédée, NULL permis) */
GtMacros      *gt_macros_new             (GtkTermLink *link, GtSession *owner);
/* Fermeture : polling arrêté sans rappel d'état, ordonnanceur libéré ; ensuite gt_macros_send rend FALSE sans rien envoyer. Idempotente ; NULL accepté. */
void           gt_macros_shutdown        (GtMacros *m);
void           gt_macros_free            (GtMacros *m);	/* shutdown puis libération ; NULL accepté */
GtSession     *gt_macros_get_owner       (GtMacros *m);
GtkTermLink   *gt_macros_get_link        (GtMacros *m);
GtMacroSet    *gt_macros_get_set         (GtMacros *m);
GtMacroPoller *gt_macros_get_poller      (GtMacros *m);	/* NULL après shutdown */
/* Prend le jeu en charge, arrête tout polling, puis crochet macros_changed(owner). Une fenêtre qui édite l'ancien jeu le reconnaît en comparant
 * gt_macros_get_set() au jeu qu'elle édite, et se ferme sans enregistrer. */
void           gt_macros_replace_set     (GtMacros *m, GtMacroSet *set);
/* Modifications de la fenêtre d'édition (OK) appliquées au jeu courant (gt_macro_set_apply_edits) ; une macro dont les valeurs par défaut changent
 * perd ses valeurs d'usage. Ni reconstruction ni enregistrement : à l'appelant (gt_macros_changed, enregistrement). */
void           gt_macros_apply_edits     (GtMacros *m, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists);
/* Envoi de macro de l'interface (clic, raccourci, polling) sur la liaison de m ; n_args < 0 : arguments résolus (gt_macros_get_args). Macro inconnue
 * ou non envoyable, m arrêté, ou argument refusé par gt_macros_check_args : FALSE, rien n'est envoyé. TRUE si tous les octets ont été écrits. Au moins un
 * octet écrit : « Macro "…" sent! » (status_flash(owner)). Argument refusé : « Macro "…" not sent: argument N: raison » (3 s), au plus une fois toutes
 * les 3 s par macro (délai remis à zéro par un changement des valeurs d'usage ou du jeu) ; gt_macros_send_quiet n'affiche aucun des deux. */
gboolean       gt_macros_send            (GtMacros *m, guint id, const gchar *const *args, gint n_args);
gboolean       gt_macros_send_quiet      (GtMacros *m, guint id, const gchar *const *args, gint n_args);	/* moteur Lua : sans message */
/* NULL : chemin courant (ou défaut). Fichier absent : jeu vide, TRUE. Fichier illisible : FALSE ; interactif (menu), message, jeu et chemin inchangés ;
 * non interactif (profil), jeu vide, chemin courant = path, avertissement + message différé (g_idle_add). Dès que le jeu est remplacé (réussite, ou
 * échec non interactif), un lien à un modèle est oublié, intégré ou non ; un échec interactif ne change rien. */
gboolean       gt_macros_load_file       (GtMacros *m, const gchar *path, gboolean interactive);
/* path NULL : chemin courant (ou défaut). CONFLICT (sauf force) : cible = fichier courant changé sur le disque depuis la dernière lecture/écriture,
 * ou, sans fichier courant ni path, fichier par défaut présent et non vide (jamais lu par m) ; rien n'est écrit (non interactif : g_warning).
 * FAILED : message (interactif) ou g_warning. Intégré : le chemin n'est pas retenu. */
GtMacrosSaveResult gt_macros_save_file   (GtMacros *m, const gchar *path, gboolean interactive, gboolean force);
/* Relit le fichier courant, à défaut le fichier par défaut (interactif) ; FALSE en mode intégré sans fichier courant */
gboolean       gt_macros_reload          (GtMacros *m);
/* Oublie le dernier échec d'enregistrement signalé : le prochain échec, même identique, est de nouveau signalé. Appelé avant un enregistrement
 * explicite (menu « Save macros file ») ; les enregistrements implicites (frappe, fenêtre d'édition) ne l'appellent pas. */
void           gt_macros_forget_save_error(GtMacros *m);
const gchar   *gt_macros_get_path        (GtMacros *m);	/* NULL : aucun fichier chargé ni enregistré */
gchar         *macros_file_default_path  (void);
void           gt_macros_changed         (GtMacros *m);	/* crochet macros_changed(owner) : panneau et raccourcis à reconstruire */

/* Valeurs d'usage (spec §3.2-6) : jamais enregistrées ; effacées par gt_macros_replace_set (donc par tout chargement). */
void                gt_macros_set_usage_arg  (GtMacros *m, guint id, gint arg_index, const gchar *value);	/* initialisées depuis les défauts */
void                gt_macros_reset_usage    (GtMacros *m, guint id);	/* id 0 : toutes */
gboolean            gt_macros_has_usage      (GtMacros *m, guint id);
/* Arguments d'envoi résolus argument par argument (spec arguments §2.7) : valeur d'usage non vide, sinon défaut, sinon "". Un vecteur d'usage de
 * mauvaise longueur est ignoré en entier. NULL : macro inconnue, ou ni défaut (GtMacro.args NULL) ni valeur d'usage non vide (l'action part alors
 * avec ses spécificateurs). Vecteur possédé par m, valable jusqu'au prochain appel pour cette macro ou au prochain changement du jeu ou des valeurs
 * d'usage. */
const gchar *const *gt_macros_get_args       (GtMacros *m, guint id);
/* Valeurs d'usage brutes ("" : champ vide) ; NULL si aucune ou de mauvaise longueur. Empruntées (même durée que gt_macros_get_args). */
const gchar *const *gt_macros_get_usage      (GtMacros *m, guint id);
/* Vérifie les arguments (gt_macro_check_args, limites de la macro) ; n_args < 0 : arguments résolus. -1 : valides, ou macro inconnue ; sinon rang (0..)
 * du premier argument refusé et *reason (peut être NULL) = raison traduite (g_free), NULL quand -1. */
gint                gt_macros_check_args     (GtMacros *m, guint id, const gchar *const *args, gint n_args, gchar **reason);
/* Mode intégré (onglet de projet, spec §3.2-4) : chemin oublié ; load/save visent alors un fichier sans le retenir (import/export). */
void                gt_macros_set_embedded   (GtMacros *m, gboolean embedded);
gboolean            gt_macros_is_embedded    (GtMacros *m);
/* Rappel « jeu modifié » (session : signal edited) ; appelé par gt_macros_changed et gt_macros_edited */
void                gt_macros_set_edited_func(GtMacros *m, void (*func) (gpointer data), gpointer data);
void                gt_macros_edited         (GtMacros *m);	/* réglage enregistrable changé sans reconstruction (popover de polling) */

/* Onglet lié à un modèle de macros (spec docs/superpowers/specs/2026-10-15-modeles-macros-design.md, §2.1, §2.5 à §2.8). Le lien (chemin absolu du
 * modèle, base = jeu du modèle lu en dernier, identité du fichier) est distinct du fichier courant (gt_macros_get_path) ; set_embedded(TRUE) n'y touche
 * pas, set_embedded(FALSE) et un gt_macros_load_file qui remplace le jeu détachent ; gt_macros_reload et gt_macros_save_file l'ignorent. Le jeu
 * courant est le jeu effectif : base + valeurs propres, toujours de même structure que la base (garde-fou : gt_macros_replace_set d'un jeu d'une
 * autre structure oublie le lien, avec un g_warning). */
/* Relevée par chaque changement du jeu effectif ou du lien (replace_set, apply_edits, gt_macros_edited, lier, détacher, rafraîchir) : une réponse
 * asynchrone la compare à celle relevée en posant sa question et n'applique rien si elle a changé. */
guint64             gt_macros_get_generation (GtMacros *m);
const gchar        *gt_macros_get_template   (GtMacros *m);	/* chemin absolu ; NULL : non lié */
/* Base ; NULL : non lié. Empruntée : invalide après gt_macros_link_template, gt_macros_link_from_project, un gt_macros_save_template_edits
 * réussi, gt_macros_refresh_template / gt_macros_reload_template réussis, gt_macros_detach_template, gt_macros_copy_from (destination), et tout ce qui
 * oublie le lien (set_embedded(FALSE), gt_macros_load_file, gt_macros_free). */
const GtMacroSet   *gt_macros_get_template_set(GtMacros *m);
GtMacroOverrides   *gt_macros_get_overrides  (GtMacros *m);	/* diff(base, effectif), nouvelle (gt_macro_overrides_free) ; NULL : non lié */
/* Lie m au fichier path (rendu absolu) : base = jeu lu, jeu effectif = copie de la base (usage et polling remis à zéro). Échec de lecture, fichier
 * absent compris : message « Cannot load macros file: … », rien ne change, FALSE. */
gboolean            gt_macros_link_template  (GtMacros *m, const gchar *path);
/* Ouverture d'un projet (§2.5) : modèle relu (UPDATED si son texte diffère de cached_base, sinon UNCHANGED) ; illisible ou absent : base = copie
 * de cached_base, MISSING, *error_text = message de l'erreur (g_free), lien gardé. Jeu effectif = gt_macro_overrides_apply(overrides, base),
 * installé par gt_macros_replace_set. dropped, error_text, overrides : NULL permis. */
GtMacrosTemplateStatus gt_macros_link_from_project(GtMacros *m, const gchar *path, const GtMacroSet *cached_base,
                                                   const GtMacroOverrides *overrides, guint *dropped, gchar **error_text);
/* Oublie modèle, base et identité ; jeu effectif, polling et usage inchangés ; flash « Detached from macros template … », edited. Non lié : rien. */
void                gt_macros_detach_template(GtMacros *m);
/* Nouvel onglet : jeu de src copié (gt_macros_replace_set) et son lien (chemin, base copiée, identité) ; src non lié : dst détaché */
void                gt_macros_copy_from      (GtMacros *dst, GtMacros *src);
/* Simule les lignes sur un clone du jeu effectif (rien n'est appliqué) ; lists (NULL : listes actuelles) n'est jamais pris. */
GtMacrosEditKind    gt_macros_classify_edits (GtMacros *m, const GtMacroEdit *rows, guint n_rows, const GtMacroLists *lists);
/* Chemin unique d'application de l'éditeur : gt_macros_apply_edits (copie de lists) + gt_macro_poller_sync + gt_macros_changed */
void                gt_macros_commit_edits   (GtMacros *m, const GtMacroEdit *rows, guint n_rows, const GtMacroLists *lists);
/* Enregistre les lignes dans le modèle (§2.8) : nouvelle base = gt_macro_overrides_merge_template (les valeurs propres ne fuient pas) ; CONFLICT
 * (sauf force) si le modèle a changé sur le disque depuis sa dernière lecture ou écriture, rien n'est écrit ni appliqué ; FAILED : écriture
 * impossible (message) ou non lié (sans message), rien appliqué ; SAVED : base remplacée, gt_macros_commit_edits, flash « Macros template … saved ». */
GtMacrosSaveResult  gt_macros_save_template_edits(GtMacros *m, const GtMacroEdit *rows, guint n_rows, const GtMacroLists *lists,
                                                  gboolean force);
/* Relit le modèle et y réapplique les valeurs propres actuelles ; même structure que le jeu effectif : mise à jour en place (le polling en marche
 * continue, usage effacé pour les macros dont la valeur par défaut change), sinon gt_macros_replace_set. FALSE : non lié ou lecture impossible
 * (interactif : message ; sinon g_warning), rien ne change. Réussi : la génération change toujours, mais en mise à jour en place le pointeur
 * du jeu (gt_macros_get_set) reste le même : une fenêtre d'édition qui doit se fermer après un rechargement compare la génération, pas le pointeur. */
gboolean            gt_macros_refresh_template(GtMacros *m, gboolean interactive);
gboolean            gt_macros_reload_template(GtMacros *m);	/* = gt_macros_refresh_template (m, TRUE) */

#endif
