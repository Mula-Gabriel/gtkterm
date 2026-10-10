/* Bouchon de l'état des macros pour test_script_engine : une instance par moteur, jeu fourni par le test, envoi par la liaison bouchonnée. */
#ifndef STUB_MACROS_H_
#define STUB_MACROS_H_

#include "macros.h"

GtMacros *stub_macros_default(void);	/* créée au premier appel, jamais libérée */
GtMacros *stub_macros_new(void);	/* instance nouvelle, gardée jusqu'à la fin du processus */
void      stub_macros_set_set(GtMacros *m, GtMacroSet *set);	/* prend le jeu en charge */
GThread  *stub_macros_thread(void);	/* fil du dernier appel à gt_macros_get_set (toutes instances) */

#endif
