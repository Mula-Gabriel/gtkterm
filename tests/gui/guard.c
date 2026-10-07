/* Garde-fou LD_PRELOAD de la suite graphique : refuse l'ouverture des vrais ports série et de la configuration réelle de
   l'utilisateur (domicile réel transmis par GTKTERM_GUARD_REAL_HOME). Corrige aussi un défaut du clavier de GDK broadway
   (voir gdk_keymap_translate_keyboard_state en fin de fichier). */
/* Meson définit _FILE_OFFSET_BITS=64 : open, openat et fopen deviendraient des alias de leurs variantes 64 bits, que l'on
   enveloppe aussi ici. On compile donc sans, pour définir chaque symbole une seule fois. */
#undef _FILE_OFFSET_BITS
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int starts_with(const char *p, const char *prefix)
{
	return strncmp(p, prefix, strlen(prefix)) == 0;
}

static int forbidden(const char *p)
{
	static const char *devices[] = { "/dev/ttyS", "/dev/ttyACM", "/dev/ttyUSB", "/dev/serial", NULL };
	static const char *home_entries[] = { "/.config", "/.gtktermrc", "/.cache", "/.local", NULL };
	const char *home = getenv("GTKTERM_GUARD_REAL_HOME");
	char buf[4096];
	int blocked = 0;

	if(p == NULL)
		return 0;
	for(int i = 0; devices[i] != NULL && !blocked; i++)
		blocked = starts_with(p, devices[i]);
	for(int i = 0; home != NULL && home[0] != '\0' && home_entries[i] != NULL && !blocked; i++)
	{
		snprintf(buf, sizeof(buf), "%s%s", home, home_entries[i]);
		blocked = starts_with(p, buf);
	}
	if(blocked)
	{
		int n = snprintf(buf, sizeof(buf), "EMGUARD: blocked %s\n", p);

		if(write(2, buf, (size_t)n) < 0)
			return 1;
	}
	return blocked;
}

#define WRAP(name) \
int name(const char *p, int f, ...) { static int (*r)(const char *, int, ...); if(!r) r = dlsym(RTLD_NEXT, #name); \
	mode_t m = 0; if(f & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, f); m = va_arg(a, mode_t); va_end(a); } \
	if(forbidden(p)) { errno = EACCES; return -1; } return r(p, f, m); }
WRAP(open)
WRAP(open64)

#define WRAPAT(name) \
int name(int d, const char *p, int f, ...) { static int (*r)(int, const char *, int, ...); if(!r) r = dlsym(RTLD_NEXT, #name); \
	mode_t m = 0; if(f & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, f); m = va_arg(a, mode_t); va_end(a); } \
	if(forbidden(p)) { errno = EACCES; return -1; } return r(d, p, f, m); }
WRAPAT(openat)
WRAPAT(openat64)

FILE *fopen(const char *p, const char *m)
{
	static FILE *(*r)(const char *, const char *);

	if(!r)
		r = dlsym(RTLD_NEXT, "fopen");
	if(forbidden(p))
	{
		errno = EACCES;
		return NULL;
	}
	return r(p, m);
}

FILE *fopen64(const char *p, const char *m)
{
	static FILE *(*r)(const char *, const char *);

	if(!r)
		r = dlsym(RTLD_NEXT, "fopen64");
	if(forbidden(p))
	{
		errno = EACCES;
		return NULL;
	}
	return r(p, m);
}

/* Clavier de GDK broadway (GTK 3) : gdk_keymap_translate_keyboard_state() n'y écrit jamais *consumed_modifiers. GtkKeyHash
   (gtk_window_activate_key) lit alors une valeur indéterminée de la pile ; quand elle contient le bit Contrôle, la touche « b »
   seule déclenche Ctrl+B (Send break) et Ctrl+F5 se confond avec F5 (Open Port). Les backends X11 et Wayland écrivent toujours
   cette valeur : on part de 0 (aucun modificateur consommé), ce que broadway laisse tel quel. Sans GTK, jamais appelée. */
typedef int (*translate_keyboard_state_fn)(void *, unsigned int, unsigned int, int, unsigned int *, int *, int *, unsigned int *);

int gdk_keymap_translate_keyboard_state(void *keymap, unsigned int hardware_keycode, unsigned int state, int group, unsigned int *keyval,
                                        int *effective_group, int *level, unsigned int *consumed_modifiers)
{
	static translate_keyboard_state_fn r;

	if(!r)
		r = (translate_keyboard_state_fn)dlsym(RTLD_NEXT, "gdk_keymap_translate_keyboard_state");
	if(consumed_modifiers != NULL)
		*consumed_modifiers = 0;
	return r(keymap, hardware_keycode, state, group, keyval, effective_group, level, consumed_modifiers);
}
