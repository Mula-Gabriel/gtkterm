#ifndef UPDATE_H_
#define UPDATE_H_

#include <gtk/gtk.h>

/*
 * Run the version check against the configured remote.
 *
 * manual == TRUE  : invoked from the "Check for updates now" button. Always
 *                   reports the result to the user (up-to-date / error / newer).
 * manual == FALSE : startup check. Stays silent unless a newer version is found
 *                   (and not previously skipped).
 *
 * parent is the window used to anchor any dialog (may be NULL).
 */
void update_check(GtkWindow *parent, gboolean manual);

#endif /* UPDATE_H_ */
