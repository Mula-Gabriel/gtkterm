/* Versions publiées (tags vX.Y.Z), version installée (git describe) et notes de version (fichier NEWS) : logique pure, GLib seulement, sans
 * sous-processus ni globale. Partagé tel quel par la ligne GTK 3 et la refonte (spec docs/superpowers/specs/2026-10-12-maj-par-tags-design.md). */

#ifndef UPDATE_VERSIONS_H_
#define UPDATE_VERSIONS_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
	guint major;
	guint minor;
	guint patch;
	guint distance;	/* commits après le tag (git describe « vX.Y.Z-N-g<hex> ») ; 0 pour une version publiée */
} GtVersion;

/* « vX.Y.Z », « X.Y.Z » ou « vX.Y.Z-N-g<hex> » (N ≥ 1, 4 à 64 chiffres hexadécimaux) ; nombres décimaux de 1 à 9 chiffres sans zéro de tête
 * superflu. FALSE (et *out intact) sinon. */
gboolean gt_version_parse(const gchar *str, GtVersion *out);
/* TRUE seulement pour un nom de tag de version publiée : exactement « vX.Y.Z » */
gboolean gt_version_is_release_tag(const gchar *str);
/* < 0, 0, > 0 : major, minor, patch, puis distance */
gint gt_version_compare(const GtVersion *a, const GtVersion *b);
/* Version installée : describe (GIT_DESCRIBE) s'il est lisible, sinon project_version (VERSION de meson), sinon 0.0.0 */
void gt_version_installed(const gchar *describe, const gchar *project_version, GtVersion *out);
/* Sortie de « git for-each-ref --format=%(refname:strip=2) refs/tags » → tags de versions publiées (gchar *, libérés par le tableau), sans
 * doublon, du plus récent au plus ancien. Jamais NULL. */
GPtrArray *gt_version_list_from_refs(const gchar *text);

typedef enum
{
	GT_NEWS_ITEM,	/* « - » : nouveauté */
	GT_NEWS_WARNING,	/* « ! » : avertissement */
	GT_NEWS_TEXT	/* toute autre ligne, gardée telle quelle */
} GtNewsKind;

typedef struct
{
	GtNewsKind kind;
	gchar *text;	/* sans le marqueur ; lignes de prolongement jointes par une espace */
} GtNewsEntry;

typedef struct
{
	gchar *tag;	/* « vX.Y.Z » */
	GtVersion version;
	gchar *title;	/* en-tête sans « ## » : « v2.0.0 — 2026-10-20 » */
	GPtrArray *entries;	/* GtNewsEntry * */
} GtNewsSection;

/* Sections de NEWS dans l'ordre du fichier (GtNewsSection *, libérées par le tableau) ; ce qui précède la première section est ignoré. Un titre de niveau 1
 * (« # … ») clôt la section en cours : ce qui suit, jusqu'au prochain « ## vX.Y.Z », est ignoré. Jamais NULL. */
GPtrArray *gt_news_parse(const gchar *text);

typedef enum
{
	GT_NEWS_SAME,	/* version visée = version installée */
	GT_NEWS_UPGRADE,	/* plus récente */
	GT_NEWS_ROLLBACK	/* plus ancienne : les sections sont celles des versions quittées */
} GtNewsDirection;

/* Sections à montrer pour passer de installed à target : montée, versions v avec installed < v ≤ target ; retour, target < v ≤ installed.
 * Seules celles dont le tag figure dans released (tags publiés), une seule par tag (la première du fichier), de la plus récente à la plus
 * ancienne. Pointeurs empruntés à sections (le tableau rendu ne libère rien). *direction (si non NULL) reçoit le sens. */
GPtrArray *gt_news_select(GPtrArray *sections, GPtrArray *released, const GtVersion *installed, const GtVersion *target, GtNewsDirection *direction);
/* TRUE si une des sections contient un avertissement */
gboolean gt_news_has_warning(GPtrArray *selected);

typedef enum
{
	GT_RELEASE_UP_TO_DATE,	/* aucune version publiée plus récente que l'installée */
	GT_RELEASE_AVAILABLE,	/* newest plus récente et non ignorée */
	GT_RELEASE_SKIPPED	/* newest plus récente mais ignorée (« Skip this version ») */
} GtReleaseVerdict;

/* Démarrage : newest = tag le plus récent (NULL ou pas une version : UP_TO_DATE) ; skipped = tag ignoré (NULL ou "" : aucun). */
GtReleaseVerdict gt_release_verdict(const GtVersion *installed, const gchar *newest, const gchar *skipped);

G_END_DECLS

#endif /* UPDATE_VERSIONS_H_ */
