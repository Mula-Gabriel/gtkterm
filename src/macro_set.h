/* macro_set.h — jeu de macros sans GTK : identifiants stables, arguments enregistrés, réglages de polling, expansion unique.
 * Utilisé uniquement sur le fil principal. */

#ifndef MACRO_SET_H_
#define MACRO_SET_H_

#include <glib.h>

#include "macros_list.h"

#define GT_MACRO_PERIOD_MIN_MS     1u
#define GT_MACRO_PERIOD_MAX_MS     86400000u	/* 24 h */
#define GT_MACRO_PERIOD_DEFAULT_MS 1000u

typedef struct
{
	guint    id;			/* stable pendant la vie du jeu, jamais 0, jamais réutilisé dans le processus */
	gchar   *label;			/* jamais NULL ("" possible) */
	gchar   *shortcut;		/* jamais NULL ; "" = aucun raccourci */
	gchar   *action;		/* jamais NULL */
	gchar   *tab;			/* jamais NULL ; "" = onglet par défaut (nom décidé par l'interface) */
	gchar  **args;			/* NULL, ou exactement macro_count_format_args() éléments */
	gchar  **arg_min;		/* limites des arguments (macro_arg.h) : NULL, ou exactement autant d'éléments que args ("" = aucune) ; */
	gchar  **arg_max;		/* jamais un vecteur entièrement vide (il devient NULL) ; longueur en octets pour %s, valeur pour les nombres */
	gchar  **arg_charset;	/* jeu de caractères d'un %s ("", "printable", "ascii", "latin1", "class:…") */
	gboolean polling;		/* mode polling activé (persistant) ; la marche n'est jamais persistée */
	guint    polling_period_ms;	/* toujours dans [GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS] */
} GtMacro;

typedef struct _GtMacroSet GtMacroSet;

/* Une ligne de la fenêtre d'édition : id 0 = nouvelle macro. */
typedef struct
{
	guint        id;
	const gchar *label;
	const gchar *shortcut;
	const gchar *action;
	const gchar *tab;
	const gchar *const *args;	/* valeurs par défaut ; NULL : aucune */
	const gchar *const *arg_min;	/* limites ; NULL : aucune (les initialiseurs positionnels existants restent valides) */
	const gchar *const *arg_max;
	const gchar *const *arg_charset;
} GtMacroEdit;

GtMacroSet         *gt_macro_set_new          (void);
void                gt_macro_set_free         (GtMacroSet *set);
guint               gt_macro_set_count        (const GtMacroSet *set);
const GtMacro      *gt_macro_set_get          (const GtMacroSet *set, guint index);
const GtMacro      *gt_macro_set_find         (const GtMacroSet *set, guint id);
gint                gt_macro_set_index_of     (const GtMacroSet *set, guint id);
const GtMacro      *gt_macro_set_find_label   (const GtMacroSet *set, const gchar *label);
guint               gt_macro_set_append       (GtMacroSet *set, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab);
const GtMacroLists *gt_macro_set_get_lists    (const GtMacroSet *set);
void                gt_macro_set_take_lists   (GtMacroSet *set, GtMacroLists *lists);
gboolean            gt_macro_set_set_arg      (GtMacroSet *set, guint id, gint arg_index, const gchar *value);
gboolean            gt_macro_set_set_args     (GtMacroSet *set, guint id, const gchar *const *args, gint n_args);
/* Limites d'une macro : chaque vecteur NULL ou de n_args éléments (n_args = nombre d'arguments de la macro), sinon -1 et rien ne change
 * (macro inconnue : -1). Les entrées invalides pour le type de l'argument de même rang sont remises à "" ; rend leur nombre. */
gint                gt_macro_set_set_limits   (GtMacroSet *set, guint id, const gchar *const *min, const gchar *const *max,
                                               const gchar *const *charset, gint n_args);
gboolean            gt_macro_set_set_polling  (GtMacroSet *set, guint id, gboolean polling, guint period_ms);
GtMacroSet         *gt_macro_set_copy         (const GtMacroSet *set);	/* copie profonde ; nouveaux identifiants */
void                gt_macro_set_apply_edits  (GtMacroSet *set, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists);

gboolean            gt_macro_action_is_separator(const gchar *action, gchar **label_out);
gboolean            gt_macro_is_sendable      (const GtMacro *macro);
GBytes             *gt_macro_expand           (const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args);

/* Segments des octets envoyés (§2.9 de la spec des arguments) : ESCAPE = octet issu de plusieurs caractères qui ne tiennent pas dans un
 * seul argument (« \r », « \x%02X ») ; ARG = octet issu de la plage d'un argument ; TEXT sinon. Voisins de même sorte et même argument fusionnés. */
typedef enum
{
	GT_MACRO_SEG_TEXT,
	GT_MACRO_SEG_ESCAPE,
	GT_MACRO_SEG_ARG
} GtMacroSegKind;

typedef struct
{
	GtMacroSegKind kind;
	gint           arg;		/* rang de l'argument (0..) pour ARG, -1 sinon */
	guint          start, len;	/* en octets, dans les octets rendus */
} GtMacroSegment;

/* Mêmes octets et mêmes règles d'arguments que gt_macro_expand (n_args < 0 : valeurs par défaut de la macro), jeux de caractères
 * de macro->arg_charset ; segments_out (nullable) reçoit un GArray de GtMacroSegment couvrant les octets bout à bout (g_array_unref). */
GBytes             *gt_macro_expand_segments  (const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args,
                                               GArray **segments_out);
/* Vérifie (gt_macro_arg_check, limites de la macro) les min(n_args, nombre d'arguments) premiers arguments, listes exceptées ;
 * n_args < 0 : valeurs par défaut de la macro ; args NULL : TRUE. Refus : *index_out = rang (0..) du premier argument refusé,
 * *reason_out = raison traduite (g_free) ; sinon -1 et NULL. index_out et reason_out peuvent être NULL. */
gboolean            gt_macro_check_args       (const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args,
                                               gint *index_out, gchar **reason_out);
gboolean            gt_macro_period_parse     (const gchar *text, guint *period_ms);

#endif
