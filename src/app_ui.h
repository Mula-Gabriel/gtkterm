/* Effets d'interface demandés par la couche application (sous-projet 6a, spec §4.3 ; session ajoutée par 6b, spec §3.2-4). Chaque interface remplit la
 * table une fois au démarrage ; table vide = aucun effet (tests), sauf message qui écrit alors sur stderr. Fil principal seulement ; aucun crochet ne
 * doit lancer de boucle imbriquée. session : l'onglet concerné (NULL : aucun, tests ou transition). */
#ifndef APP_UI_H_
#define APP_UI_H_

#include <glib.h>

typedef struct _GtSession GtSession;	/* session.h (couche application) ; opaque ici */

typedef enum { GT_APP_MSG_INFO, GT_APP_MSG_WARNING, GT_APP_MSG_ERROR } GtAppMessageKind;

typedef struct
{
	void (*message)(GtAppMessageKind kind, const gchar *text);	/* boîte non bloquante sur la fenêtre ; NULL : g_printerr */
	void (*status_flash)(GtSession *session, const gchar *text, guint ms);	/* barre d'état de la page de session ; NULL : rien */
	void (*macros_changed)(GtSession *session);	/* jeu de la session remplacé ou modifié : son panneau et ses raccourcis à reconstruire */
	void (*macro_polling_changed)(GtSession *session, guint id, gboolean running);
	void (*profile_applied)(GtSession *session);	/* profil chargé dans la session : sa page réapplique terminal et couleurs, son port est rouvert */
} GtAppUi;

void           gt_app_ui_set(const GtAppUi *ui);	/* copie la table ; NULL = table vide */
const GtAppUi *gt_app_ui_get(void);	/* jamais NULL */
void           gt_app_message(GtAppMessageKind kind, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);	/* formate puis crochet ou stderr */
void           gt_app_status_flash(GtSession *session, guint ms, const gchar *fmt, ...) G_GNUC_PRINTF(3, 4);

#endif
