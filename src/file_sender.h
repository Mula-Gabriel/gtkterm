/* Envoi d'un fichier sur la liaison (sous-projet 6a) : lecture par blocs, délai après chaque fin de ligne ou attente d'un caractère
 * reçu, progression, fin, annulation. Sans GTK ni globale : la fonction d'écriture est injectée (gt_link_send dans l'application).
 * Boucle GLib du fil principal ; toutes les fonctions s'appellent depuis ce fil.
 *
 * Signaux :
 *   "progress" (guint64 sent, guint64 total) : après chaque écriture d'au moins un octet ;
 *   "done" (GError *error, NULL si succès) : une seule fois par envoi démarré ; le gestionnaire peut libérer l'objet.
 * Rythme : line_delay_ms > 0 → pause de ce délai après chaque LF écrit (prioritaire) ; sinon wait_char ∈ 0-255 → après chaque LF
 * écrit, attente d'un octet reçu égal (gt_file_sender_feed_rx) ; sinon envoi continu. Le dernier LF est suivi de son délai ou de
 * son attente avant "done" (comme l'ancien files.c). L'écriture qui rend 0 (port occupé, EAGAIN) est réessayée après 10 ms.
 * Libérer l'objet pendant un envoi arrête tout sans émettre "done". */
#ifndef FILE_SENDER_H_
#define FILE_SENDER_H_

#include <gio/gio.h>

G_BEGIN_DECLS

/* Octets écrits (1 à len), 0 = réessayer plus tard (port momentanément plein), -1 = erreur (errno lu pour le message).
 * Une liaison absente ou fermée doit rendre -1, pas 0 (sinon l'envoi réessaierait sans fin). La fonction ne doit ni annuler l'envoi
 * ni libérer l'objet. */
typedef gssize (*GtFileSenderWrite)(const guint8 *data, gsize len, gpointer user_data);

#define GT_TYPE_FILE_SENDER (gt_file_sender_get_type())
G_DECLARE_FINAL_TYPE(GtFileSender, gt_file_sender, GT, FILE_SENDER, GObject)

/* wait_char -1 : sans attente ; line_delay_ms ≤ 0 : sans délai */
GtFileSender *gt_file_sender_new(GtFileSenderWrite write, gpointer user_data, gint line_delay_ms, gint wait_char);
/* Ouvre le fichier et lance l'envoi (première écriture depuis la boucle). Fichier illisible, pas un fichier ordinaire ou envoi
 * déjà en cours : FALSE et G_IO_ERROR, aucun "done". Fichier vide : TRUE, "done" sans erreur émis depuis un idle. */
gboolean      gt_file_sender_start(GtFileSender *s, const gchar *path, GError **error);
/* Octets reçus par la liaison : débloque l'attente du caractère s'il y figure */
void          gt_file_sender_feed_rx(GtFileSender *s, const guint8 *data, gsize len);
/* Liaison perdue ou fermée : "done" avec G_IO_ERROR_BROKEN_PIPE si un envoi est en cours, rien sinon */
void          gt_file_sender_link_lost(GtFileSender *s);
/* "done" avec G_IO_ERROR_CANCELLED si un envoi est en cours, rien sinon ; minuteries retirées, fichier fermé */
void          gt_file_sender_cancel(GtFileSender *s);
guint64       gt_file_sender_get_sent(GtFileSender *s);
guint64       gt_file_sender_get_total(GtFileSender *s);

G_END_DECLS

#endif
