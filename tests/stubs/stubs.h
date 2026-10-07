/* Bouchons des tests d'intégration : remplacent les fonctions de l'interface (GTK/VTE) et enregistrent chaque appel. */
#ifndef STUBS_H_
#define STUBS_H_

#include <glib.h>
#include "history.h"

void         stub_reset(void);
void         stub_record(const gchar *nom, const gchar *texte);
gint         stub_count(const gchar *nom);
const gchar *stub_last(const gchar *nom);

/* Liaison (stub_link.c) */
GByteArray  *stub_sent(void);
GThread     *stub_send_thread(void);
GtSendOrigin stub_last_origin(void);
void         stub_inject_rx(const gchar *data, guint len);

#endif
