/***********************************************************************/
/* device_match.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      Correspondance d'un événement udev avec le port ouvert         */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "device_match.h"

gboolean device_match(dev_t want_devnum, const gchar *want_path, dev_t ev_devnum, const gchar *ev_file, const gchar *const *ev_links)
{
	if(want_devnum != 0 && ev_devnum == want_devnum)
		return TRUE;
	if(want_path == NULL || want_path[0] == '\0')
		return FALSE;
	if(ev_file != NULL && strcmp(ev_file, want_path) == 0)
		return TRUE;
	for(; ev_links != NULL && *ev_links != NULL; ev_links++)
	{
		if(strcmp(*ev_links, want_path) == 0)
			return TRUE;
	}
	return FALSE;
}
