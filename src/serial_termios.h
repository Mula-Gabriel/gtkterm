/* Construction pure des réglages termios d'un port série (aucune entrée-sortie). */
#ifndef SERIAL_TERMIOS_H_
#define SERIAL_TERMIOS_H_

#include <glib.h>
#include <termios.h>

/* Applique à tio les bits de données (5 à 8), la parité (0 aucune, 1 impaire, 2 paire), les bits de stop (1 ou 2)
 * et le contrôle de flux (0 aucun, 1 Xon/Xoff, 2 RTS/CTS, 3 RS485 semi-duplex sans contrôle matériel).
 * Les champs concernés sont d'abord effacés : rien n'est hérité de l'état précédent du port. Le débit n'est pas touché. */
void serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow);

#endif
