/***********************************************************************/
/* files.h                                                           */
/* ---------                                                           */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Raw / text file transfer management                            */
/*      - Header file -                                                */
/*                                                                     */
/***********************************************************************/

#ifndef FICHIER_H_
#define FICHIER_H_

#include "link.h"

void send_raw_file(GtkAction *action, gpointer data);
void save_raw_file(GtkAction *action, gpointer data);
void save_ascii_file(GtkAction *action, gpointer data);

/* Attente du caractère de fin de ligne pendant un envoi de fichier : comparé aux octets reçus. */
void files_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
/* Perte ou fermeture de la liaison pendant un envoi de fichier (y compris pendant l'attente du caractère ou du délai) : arrêt avec un message. */
void files_on_link_state(GObject *object, GParamSpec *pspec, gpointer user_data);
extern gchar *fic_defaut;


#endif
