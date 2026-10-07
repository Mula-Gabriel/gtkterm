/***********************************************************************/
/* logging.h                                                           */
/* ---------                                                           */
/*                           GTKTerm Software                          */
/*                                 (c)                                 */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Log all data that GTKTerm sees to a file                       */
/*      - Header File -                                                */
/***********************************************************************/

#ifndef LOGGING_H_
#define LOGGING_H_

#include "link.h"

void logging_start(GtkAction *action, gpointer data);
void logging_pause_resume(void);
void logging_stop(void);
void logging_clear(void);

/* Journal : texte reçu tel que la vue ASCII le montre (CR/LF automatique, horodatage), sans l'écho (spec, décision 3) */
void logging_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);

#endif /* LOGGING_H_ */
