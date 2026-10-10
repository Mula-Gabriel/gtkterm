/* Garde-fou LD_PRELOAD de la suite graphique GTK 4 : refuse l'ouverture des vrais ports série et de la configuration réelle de
   l'utilisateur (domicile réel transmis par GTKTERM_GUARD_REAL_HOME). Copie du guard.c de l'ancienne suite GTK 3, sans le correctif clavier de GDK
   broadway GTK 3 : inutile en GTK 4 (essai du 2026-10-09 : « b » ne déclenche plus Ctrl+B) et impossible (gdk_keymap_* n'est pas
   exportée par libgtk-4). */
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
#include <sys/ioctl.h>
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

/* TIOCMGET simulé : si GTKTERM_GUARD_FAKE_MODEM est posée (bits TIOCM_*, décimal ou 0x…), la lecture des lignes modem d'un terminal (pty)
   renvoie ces bits au lieu de ENOTTY ; sans la variable, ou hors terminal, comportement normal. */
int ioctl(int fd, unsigned long req, ...)
{
	static int (*r)(int, unsigned long, ...);
	const char *fake = getenv("GTKTERM_GUARD_FAKE_MODEM");
	va_list a;
	void *arg;

	if(!r)
		r = dlsym(RTLD_NEXT, "ioctl");
	va_start(a, req);
	arg = va_arg(a, void *);
	va_end(a);
	if(req == TIOCMGET && fake != NULL && fake[0] != '\0' && arg != NULL && isatty(fd))
	{
		*(int *)arg = (int)strtol(fake, NULL, 0);
		return 0;
	}
	return r(fd, req, arg);
}
