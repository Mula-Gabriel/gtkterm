#ifndef SCRIPT_ENGINE_H_
#define SCRIPT_ENGINE_H_

#include <glib.h>

#include "link.h"
#include "macros.h"

/* Moteur Lua (sous-projet 5) ; un moteur par onglet (6b). Toutes ces fonctions s'appellent depuis le thread principal. Le contrat d'arrêt, de journal
 * et de fermeture de 6a/SP5 vaut pour chaque moteur ; les moteurs sont indépendants (thread, ports 1-15, files, journal, drapeaux). */
typedef struct _GtScriptEngine GtScriptEngine;

void            script_engine_setup(void);	/* une fois, au démarrage : note le thread principal (commun à tous les moteurs) */
/* Port 0 = main_link (référence gardée) ; send_macro, get_macros, get_lists → macros (non possédé, doit vivre plus longtemps que le moteur) */
GtScriptEngine *gt_script_engine_new(GtkTermLink *main_link, GtMacros *macros);
void            gt_script_engine_run(GtScriptEngine *e, const char *script_text);
/* Demande l'arrêt : le script s'arrête au plus 1000 instructions Lua plus tard (crochet de comptage), ou au prochain appel
 * gtkterm.*, print ou fonction bloquante de la bibliothèque standard enveloppée (refusé dès l'entrée), ou au plus 20 ms après
 * dans une fonction bloquante du moteur. Un appel C long ne peut pas être interrompu : appel bloquant de la bibliothèque standard (io.read, os.execute), ou
 * calcul d'une seule fonction C (string.rep énorme, motif string.find/gsub pathologique) ; l'arrêt est vu à son retour, et à
 * l'entrée de l'appel suivant pour os.execute, io.read/lines/popen/open/write et les méthodes read/lines/write des fichiers
 * (enveloppées). La résolution du nom d'un client TCP (thread Lua) n'est pas interruptible non plus. Pas de vidange de la sortie
 * série à la fermeture des ports quand l'arrêt est demandé (sinon : 2 s au plus par port). Une boucle Lua pure dans un finaliseur
 * (__gc) n'est pas interruptible (Lua coupe les crochets pendant un finaliseur). debug.sethook
 * est retiré pour que le script ne puisse pas enlever le crochet d'arrêt, mais package.loadlib peut le contourner si une liblua
 * partagée est présente (chargement de lua_sethook depuis la bibliothèque). */
void            gt_script_engine_stop(GtScriptEngine *e);
gboolean        gt_script_engine_is_running(GtScriptEngine *e);
/* Contrat de script_engine_shutdown (6a), pour ce moteur : fonctions de journal et de fin retirées, script arrêté. Le contexte par défaut ne tourne
 * plus forcément (ses sources toucheraient une interface détruite) : ce thread exécute lui-même les demandes run_on_main_sync en attente (de tous les
 * moteurs) jusqu'à la fin du thread Lua de ce moteur, au plus 3 s, puis le joint ; au-delà, avertissement, thread abandonné. Idempotente ; ensuite
 * gt_script_engine_run ne fait rien. */
void            gt_script_engine_shutdown(GtScriptEngine *e);
void            gt_script_engine_free(GtScriptEngine *e);	/* shutdown puis libération ; NULL accepté */

/* Texte du journal, en UTF-8 valide, par lots (plusieurs lignes à la fois). */
typedef void (*ScriptLogFunc)(const char *text, gpointer user_data);
void            gt_script_engine_set_log_func(GtScriptEngine *e, ScriptLogFunc func, gpointer user_data);

typedef enum
{
	GT_SCRIPT_END_FINISHED,	/* fin normale, os.exit(), os.exit(true) ou os.exit(0) */
	GT_SCRIPT_END_ERROR,	/* erreur Lua ou os.exit non nul : le message est journalisé avant */
	GT_SCRIPT_END_STOPPED	/* « Stop », fermeture de l'onglet ou de GTKTerm */
} GtScriptEnd;

/* Appelée sur le thread principal après le vidage du journal, le thread Lua joint. */
typedef void (*ScriptDoneFunc)(GtScriptEnd end, gpointer user_data);
void            gt_script_engine_set_done_func(GtScriptEngine *e, ScriptDoneFunc func, gpointer user_data);

#endif
