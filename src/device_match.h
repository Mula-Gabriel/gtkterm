/* Correspondance d'un événement udev avec le port série ouvert (sous-projet 2, M6). */
#ifndef DEVICE_MATCH_H_
#define DEVICE_MATCH_H_

#include <glib.h>
#include <sys/types.h>

/* Vrai si le numéro de périphérique est connu (non nul) et identique, ou si le chemin voulu est le nœud de l'événement ou
   l'un de ses liens udev (DEVLINKS, présents aussi dans l'événement « remove »). */
gboolean device_match(dev_t want_devnum, const gchar *want_path, dev_t ev_devnum, const gchar *ev_file, const gchar *const *ev_links);

#endif
