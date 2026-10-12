/* macro_arg_widget.h — champ d'un argument de macro (interface GTK 4), commun au panneau et à la zone « Default values » de l'éditeur :
 * type « l » → GtkDropDown des affichages de la liste ; autre type → GtkEntry validée par macro_type_value_valid (classe « error »). */
#ifndef GT_UI_MACRO_ARG_WIDGET_H
#define GT_UI_MACRO_ARG_WIDGET_H

#include <gtk/gtk.h>

#include "macros_format.h"

/* initial NULL : champ vide (liste : première entrée). Les valeurs de la liste sont copiées (le jeu peut être remplacé). Aucun rappel n'est branché. */
GtkWidget *gt_macro_arg_widget_new(const GtMacroLists *lists, const macro_arg_info_t *info, const gchar *initial);
gchar     *gt_macro_arg_widget_get_value(GtkWidget *w);	/* texte de l'entrée, valeur de l'élément choisi ("" sans choix) ; g_free */
void       gt_macro_arg_widget_validate(GtkWidget *entry);	/* classe « error » selon la validité du texte (entrée seulement) */

#endif
