/* Effets d'interface demandés par la couche application (sous-projet 6a, spec §4.3). Chaque interface remplit la table une fois au
 * démarrage ; table vide = aucun effet (tests), sauf message qui écrit alors sur stderr. Fil principal seulement ; aucun crochet ne
 * doit lancer de boucle imbriquée. */
#ifndef APP_UI_H_
#define APP_UI_H_

#include <glib.h>

typedef enum { GT_APP_MSG_INFO, GT_APP_MSG_WARNING, GT_APP_MSG_ERROR } GtAppMessageKind;

typedef struct
{
	void (*message)(GtAppMessageKind kind, const gchar *text);	/* boîte non bloquante ; NULL : g_printerr */
	void (*status_flash)(const gchar *text, guint ms);	/* message temporaire de la barre d'état ; NULL : rien */
	void (*macros_changed)(void);	/* jeu remplacé ou modifié : panneau et raccourcis à reconstruire */
	void (*macro_polling_changed)(guint id, gboolean running);
	void (*profile_applied)(void);	/* profil chargé par app_config_load_profile : options, terminal, couleurs à réappliquer, port à rouvrir */
} GtAppUi;

void           gt_app_ui_set(const GtAppUi *ui);	/* copie la table ; NULL = table vide */
const GtAppUi *gt_app_ui_get(void);	/* jamais NULL */
void           gt_app_message(GtAppMessageKind kind, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);	/* formate puis crochet ou stderr */
void           gt_app_status_flash(guint ms, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);

#endif
