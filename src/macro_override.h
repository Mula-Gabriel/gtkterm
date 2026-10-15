/* macro_override.h — valeurs propres d'un onglet lié à un modèle de macros (cœur, sans GTK).
 * Voir docs/superpowers/specs/2026-10-15-modeles-macros-design.md, §2.3, §2.4 et §2.8.
 * Une valeur propre désigne une macro par son libellé et son rang parmi les macros de même libellé ; elle porte des valeurs par
 * défaut d'arguments et éventuellement un réglage de polling. Égalité des valeurs par défaut : args NULL ≡ vecteur de "". */

#ifndef MACRO_OVERRIDE_H_
#define MACRO_OVERRIDE_H_

#include <glib.h>

#include "macro_set.h"

typedef struct
{
	gchar     *label;		/* jamais NULL */
	guint      occurrence;	/* rang parmi les macros de même libellé (0 : première) */
	GPtrArray *args;		/* gchar * ou NULL (rang non surchargé), possédés ; longueur = dernier rang surchargé + 1 */
	gboolean   has_polling;
	gboolean   polling;
	guint      polling_period_ms;	/* telle que lue : peut être hors bornes (abandonnée par apply) */
} GtMacroOverride;

typedef struct _GtMacroOverrides GtMacroOverrides;

GtMacroOverrides      *gt_macro_overrides_new(void);
void                   gt_macro_overrides_free(GtMacroOverrides *o);	/* NULL accepté */
guint                  gt_macro_overrides_count(const GtMacroOverrides *o);	/* NULL : 0 */
const GtMacroOverride *gt_macro_overrides_get(const GtMacroOverrides *o, guint i);	/* hors bornes : NULL */
GtMacroOverrides      *gt_macro_overrides_copy(const GtMacroOverrides *o);	/* NULL : liste vide */
/* Ajoute ou remplace (même libellé et occurrence : l'entrée existante est complétée en place, valeurs fusionnées rang par rang) ;
 * value NULL vaut "". */
void                   gt_macro_overrides_set_arg(GtMacroOverrides *o, const gchar *label, guint occurrence, guint rank, const gchar *value);
void                   gt_macro_overrides_set_polling(GtMacroOverrides *o, const gchar *label, guint occurrence, gboolean polling, guint period_ms);
/* Différence de effective par rapport à base, dans l'ordre des macros ; NULL si les structures diffèrent (gt_macro_set_same_structure). */
GtMacroOverrides      *gt_macro_overrides_diff(const GtMacroSet *base, const GtMacroSet *effective);
/* Copie de base (nouveaux identifiants) où chaque valeur propre est appliquée ; o NULL : simple copie. *dropped (peut être NULL) reçoit
 * le nombre d'éléments abandonnés (un par argument ou par polling) : macro absente, rang dépassé, valeur refusée par le type ou les
 * limites de la macro du modèle (listes non vérifiées), polling d'une macro non envoyable ou période hors bornes. */
GtMacroSet            *gt_macro_overrides_apply(const GtMacroOverrides *o, const GtMacroSet *base, guint *dropped);
/* Nouvelle base d'un modèle après une édition (§2.8-2) : copie de after ; pour une macro de after présente dans before (même
 * identifiant), une valeur par défaut inchangée par l'édition, d'un rang de même type, reprend celle de base (si les limites de after
 * l'acceptent) ; le polling reprend celui de base. base et before ont la même structure. */
GtMacroSet            *gt_macro_overrides_merge_template(const GtMacroSet *base, const GtMacroSet *before, const GtMacroSet *after);
/* Groupes « <prefix>override:K » (K = 1, 2, …) ; l'écriture retire d'abord les groupes existants de ce préfixe. Lecture tolérante
 * (valeur invalide ignorée avec avertissement citant origin, groupe et clé) et normalisée (une entrée par libellé et occurrence, la
 * dernière lue gagne, à la place de la première) ; jamais NULL. */
void                   gt_macro_overrides_to_key_file(const GtMacroOverrides *o, GKeyFile *kf, const gchar *prefix);
GtMacroOverrides      *gt_macro_overrides_from_key_file(GKeyFile *kf, const gchar *prefix, const gchar *origin);

#endif
