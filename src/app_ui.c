/***********************************************************************/
/* app_ui.c                                                            */
/*                                                                     */
/*   Purpose                                                           */
/*      Crochets d'interface de la couche application (GtAppUi),       */
/*      sans GTK (sous-projet 6a)                                      */
/*                                                                     */
/***********************************************************************/

#include <stdarg.h>
#include <string.h>

#include "app_ui.h"

static GtAppUi table;	/* copie de la table installée ; tout à NULL : aucune interface */

void gt_app_ui_set(const GtAppUi *ui)
{
	if(ui != NULL)
		table = *ui;
	else
		memset(&table, 0, sizeof(table));
}

const GtAppUi *gt_app_ui_get(void)
{
	return &table;
}

void gt_app_message(GtAppMessageKind kind, const gchar *fmt, ...)
{
	va_list args;
	gchar *text;

	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);
	if(table.message != NULL)
		table.message(kind, text);
	else
		g_printerr("%s%s", text, g_str_has_suffix(text, "\n") ? "" : "\n");
	g_free(text);
}

void gt_app_status_flash(guint ms, const gchar *fmt, ...)
{
	va_list args;
	gchar *text;

	if(table.status_flash == NULL)
		return;
	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);
	table.status_flash(text, ms);
	g_free(text);
}
