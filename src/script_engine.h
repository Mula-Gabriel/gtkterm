#ifndef SCRIPT_ENGINE_H_
#define SCRIPT_ENGINE_H_

#include <glib.h>

/* Moteur Lua (sous-projet 5). Toutes ces fonctions s'appellent depuis le thread principal. */
void     script_engine_setup(void);
void     script_engine_run(const char *script_text);
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
void     script_engine_stop(void);
gboolean script_engine_is_running(void);
/* Fermeture de GTKTerm (après gtk_main) : retire les fonctions de journal et de fin et arrête le script. Le contexte par défaut ne
 * tourne plus (ses sources toucheraient l'interface détruite) : ce thread exécute lui-même les demandes du thread Lua (fermeture de
 * ses ports) jusqu'à sa fin, au plus 3 s, puis le joint ; au-delà, avertissement, thread abandonné. */
void     script_engine_shutdown(void);

/* Texte du journal, en UTF-8 valide, par lots (plusieurs lignes à la fois). */
typedef void (*ScriptLogFunc)(const char *text, gpointer user_data);
void     script_engine_set_log_func(ScriptLogFunc func, gpointer user_data);

typedef enum
{
	GT_SCRIPT_END_FINISHED,	/* fin normale, os.exit(), os.exit(true) ou os.exit(0) */
	GT_SCRIPT_END_ERROR,	/* erreur Lua ou os.exit non nul : le message est journalisé avant */
	GT_SCRIPT_END_STOPPED	/* « Stop » ou fermeture de GTKTerm */
} GtScriptEnd;

/* Appelée sur le thread principal après le vidage du journal, le thread Lua joint. */
typedef void (*ScriptDoneFunc)(GtScriptEnd end, gpointer user_data);
void     script_engine_set_done_func(ScriptDoneFunc func, gpointer user_data);

#endif
