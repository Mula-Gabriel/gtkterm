/* Détection des ports série, sans GTK (sous-projet 6a) : motifs des pilotes « serial » de /proc/tty/drivers, ports accessibles, liens udev.
 * Repris de term_config.c (get_device_paths, find_serial_ports, find_serial_port_links, compare_seminum) ; chemins injectés pour les tests. */
#ifndef SERIAL_PORTS_H_
#define SERIAL_PORTS_H_

#include <glib.h>

#define GT_SERIAL_DRIVERS_FILE "/proc/tty/drivers"
#define GT_SERIAL_LINKS_DIR "/dev"

typedef struct
{
	gchar *pattern;	/* motif glob, ex. « /dev/ttyS[0-9]* » */
	gulong major;
	gulong minor;
	gulong nminors;	/* 0 : pas de contrôle du numéro de périphérique */
} GtSerialPattern;

typedef struct
{
	gchar *path;
	gboolean is_link;	/* lien symbolique (règle udev, ex. /dev/ttyM5 -> ttyACM0) : affiché en gras */
} GtSerialPort;

/* Motifs des pilotes de type « serial » ; motifs par défaut (/dev/ttyS*, /dev/tts/[0-9]*, /dev/usb/tts/[0-9]*) si le fichier est illisible ou n'en
 * décrit aucun. Tableau de GtSerialPattern libéré, motifs compris, par g_array_unref. */
GArray    *gt_serial_patterns_read(const gchar *drivers_path);
/* « \t<motif>\n » par motif (message « aucun port trouvé ») ; g_free */
gchar     *gt_serial_patterns_to_string(const GArray *patterns);
/* Ports accessibles en lecture-écriture (périphériques caractère dont le numéro correspond au pilote), puis liens symboliques de links_dir (NULL :
 * aucun) vers l'un d'eux ; triés par gt_serial_ports_compare, sans doublon. N'ouvre aucun périphérique (ouvrir un tty modifie DTR et RTS).
 * Tableau de GtSerialPort * libéré par g_ptr_array_unref. */
GPtrArray *gt_serial_ports_find(const GArray *patterns, const gchar *links_dir);
/* Ordre « semi-numérique » : suites de chiffres comparées comme des nombres (ttyS2 < ttyS10) ; à valeur égale, la plus courte d'abord ; un préfixe
 * vient avant. */
gint       gt_serial_ports_compare(const gchar *a, const gchar *b);

#endif
