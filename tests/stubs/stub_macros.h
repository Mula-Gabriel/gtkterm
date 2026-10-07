/* Bouchon de l'état des macros pour test_script_engine : jeu fourni par le test, envoi par la liaison bouchonnée. */
#ifndef STUB_MACROS_H_
#define STUB_MACROS_H_

#include "macros.h"

void     stub_set_macro_set(GtMacroSet *set);	/* prend le jeu en charge */
GThread *stub_macros_thread(void);		/* fil du dernier appel à macros_get_set */

#endif
