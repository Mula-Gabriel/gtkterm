/* Rendu des vues ASCII et hexadécimale du terminal, écho compris, vers les octets à donner à VTE (sous-projet 2).
   Pur : aucune dépendance GTK ; src/ui/terminal_view.c se contente d'alimenter VTE avec la sortie. */
#ifndef VIEW_RENDER_H_
#define VIEW_RENDER_H_

#include <glib.h>

#include "history.h"

typedef enum
{
	GT_VIEW_ASCII,
	GT_VIEW_HEX
} GtViewMode;

typedef struct
{
	GtViewMode mode;
	gboolean echo;			/* afficher les octets envoyés */
	gboolean crlf_auto;		/* vue ASCII */
	gboolean timestamp;		/* vue ASCII */
	guint bytes_per_line;		/* vue hexadécimale (2 au minimum) */
	gboolean show_index;		/* vue hexadécimale */
} GtViewOptions;

typedef struct _GtViewRender GtViewRender;

GtViewRender *gt_view_render_new(const GtViewOptions *opts);
void          gt_view_render_free(GtViewRender *r);
/* Nouvelles options et état remis à zéro (vue vidée puis redessinée par l'appelant). */
void          gt_view_render_reset(GtViewRender *r, const GtViewOptions *opts);
void          gt_view_render_rx(GtViewRender *r, const guint8 *data, gsize len, gint64 wall_time_us, GString *out);
void          gt_view_render_tx(GtViewRender *r, const guint8 *data, gsize len, GtSendOrigin origin, GString *out);
/* Rejeu d'un enregistrement de l'historique (RX ou TX). */
void          gt_view_render_record(GtViewRender *r, const GtRecord *rec, GString *out);

#endif
