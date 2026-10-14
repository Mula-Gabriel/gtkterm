/* macro_preview.h — aperçu de la ligne envoyée par une macro (cœur, sans GTK) : runs de texte, d'arguments et de caractères spéciaux
 * nommés, octets en hexadécimal. Voir le §2.10 de docs/superpowers/specs/2026-10-14-arguments-macros-design.md. */

#ifndef MACRO_PREVIEW_H_
#define MACRO_PREVIEW_H_

#include <glib.h>

#include "macro_set.h"

typedef enum
{
	GT_PREVIEW_TEXT,
	GT_PREVIEW_ARG,
	GT_PREVIEW_SPECIAL
} GtPreviewKind;

typedef struct
{
	GtPreviewKind kind;
	gint          arg;	/* rang de l'argument pour ARG, -1 sinon */
	gchar        *text;	/* UTF-8 valide */
} GtPreviewRun;

/* Runs (GtPreviewRun *, libérés avec le tableau) des octets data[0..len[ découpés par segments (GtMacroSegment, nullable : tout est
 * texte). Octets 0x20–0x7E et séquences UTF-8 valides tels quels ; contrôles nommés NUL BEL BS TAB LF VT FF CR ESC DEL, autres octets
 * \xHH. Segment ESCAPE : runs SPECIAL ; dans TEXT, un nom ou \xHH est SPECIAL ; dans ARG, tout reste ARG. Dans un run, un nom ou un \xHH
 * est séparé de ses voisins par une espace (« CR LF »). Runs voisins de même sorte et même argument fusionnés. */
GPtrArray *gt_macro_preview_runs(const guint8 *data, gsize len, const GArray *segments);
/* « 50 57 4D » (hexa majuscule) ; au-delà de max octets, les max premiers puis « … ». À libérer avec g_free. */
gchar     *gt_macro_preview_hex(const guint8 *data, gsize len, gsize max);

#endif
