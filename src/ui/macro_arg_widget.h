/* macro_arg_widget.h — champ d'un argument de macro (interface GTK 4), commun au panneau et à la zone « Default values » de l'éditeur :
 * type « l » → GtkDropDown des affichages de la liste ; autre type → GtkEntry vérifiée par gt_macro_arg_check (classe « error »). */
#ifndef GT_UI_MACRO_ARG_WIDGET_H
#define GT_UI_MACRO_ARG_WIDGET_H

#include <gtk/gtk.h>

#include "macros_format.h"

/* initial NULL : champ vide (liste : première entrée). placeholder : texte indicatif de l'entrée (NULL ou "" : indication du type,
 * « text », « 0 » ou « 0.0 »). Les valeurs de la liste sont copiées (le jeu peut être remplacé). Aucun rappel n'est branché. */
GtkWidget *gt_macro_arg_widget_new(const GtMacroLists *lists, const macro_arg_info_t *info, const gchar *initial, const gchar *placeholder);
/* Variante : optional et liste → première entrée « (none) » (valeur ""), choisie si initial vaut "" ou NULL ; optional et entrée → texte indicatif « (omitted) » par défaut */
GtkWidget *gt_macro_arg_widget_new_full(const GtMacroLists *lists, const macro_arg_info_t *info, const gchar *initial, const gchar *placeholder, gboolean optional);
/* Argument facultatif (et son préfixe) : une entrée vide est valide, la bulle ajoute la description de l'argument facultatif. Revalide le champ. */
void       gt_macro_arg_widget_set_optional(GtkWidget *w, gboolean optional, const gchar *prefix);
/* Drapeau (spec 2026-10-15-ouverture-silencieuse §3) : case à cocher libellée par le préfixe sans espaces de tête, cochée si initial est coché
 * (gt_macro_flag_value_set) ; toujours valide, bulle « Checked: sends … » */
GtkWidget *gt_macro_arg_widget_new_flag(const gchar *prefix, const gchar *initial);
/* Drapeau : "1" coché ; décoché "0" en mode « vide = défaut » (panneau, l'emporte sur un défaut coché), "" sinon (défaut de l'éditeur) */
gchar     *gt_macro_arg_widget_get_value(GtkWidget *w);	/* texte de l'entrée, valeur de l'élément choisi ("" sans choix) ; g_free */
/* Limites (macro_arg.h, NULL ou "" : aucune) et valeur par défaut de l'argument, copiées. Une fois posées, la validation donne au champ une
 * bulle d'aide décrivant l'argument (gt_macro_arg_describe), complétée par la raison quand la valeur est refusée. Revalide le champ. */
void       gt_macro_arg_widget_set_limits(GtkWidget *w, const gchar *min, const gchar *max, const gchar *charset, const gchar *def);
/* Mode « vide = défaut » (panneau) : un texte vide est vérifié comme la valeur par défaut posée par set_limits. Revalide le champ. */
void       gt_macro_arg_widget_set_empty_is_default(GtkWidget *w, gboolean empty_is_default);
/* Validité de la valeur vérifiée (texte, ou défaut si vide en mode « vide = défaut ») : gt_macro_arg_check avec les limites posées
 * (aucune : validité du type seulement). Une liste est toujours valide. reason (peut être NULL) : raison traduite si invalide, g_free. */
gboolean   gt_macro_arg_widget_check(GtkWidget *w, gchar **reason);
/* Classe « error » selon gt_macro_arg_widget_check (entrée seulement), bulle d'aide si des limites ont été posées */
void       gt_macro_arg_widget_validate(GtkWidget *w);

#endif
