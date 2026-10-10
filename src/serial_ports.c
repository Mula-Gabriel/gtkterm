/* Détection des ports série (voir serial_ports.h). */
#include <config.h>
#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#include "serial_ports.h"

static void pattern_clear(gpointer data)
{
	GtSerialPattern *p = data;

	g_free(p->pattern);
}

static void add_pattern(GArray *a, const gchar *pattern, gulong major_nr, gulong minor_nr, gulong nminors)
{
	GtSerialPattern p = { g_strdup(pattern), major_nr, minor_nr, nminors };

	g_array_append_val(a, p);
}

static char *skip_word(char *p)
{
	while(*p != '\0' && !isspace((unsigned char)*p))
		p++;
	return p;
}

static char *skip_space(char *p)
{
	while(isspace((unsigned char)*p))
		p++;
	return p;
}

GArray *gt_serial_patterns_read(const gchar *drivers_path)
{
	GArray *a = g_array_new(FALSE, TRUE, sizeof(GtSerialPattern));
	FILE *f = fopen(drivers_path, "r");
	char line[BUFSIZ];

	g_array_set_clear_func(a, pattern_clear);
	/* Ligne : nom, chemin, majeur, mineur ou plage « début-fin », type ; seuls les pilotes « serial… » comptent */
	while(f != NULL && fgets(line, sizeof line, f) != NULL)
	{
		char *p = skip_word(line), *path;
		const char *suffix;
		gulong major_nr, minor_nr, nminors = 1;
		struct stat st;
		gchar *full;

		if(p == line || *p == '\0')
			continue;
		*p++ = '\0';
		p = skip_space(p);
		if(*p != '/')
			continue;
		path = p;
		p = skip_word(p);
		if(p == path || *p == '\0')
			continue;
		*p++ = '\0';
		major_nr = strtoul(p, &p, 10);
		if(!isspace((unsigned char)*p))
			continue;
		minor_nr = strtoul(p, &p, 10);
		if(*p == '-')
			nminors = strtoul(p + 1, &p, 10) - minor_nr + 1;
		if(!isspace((unsigned char)*p))
			continue;
		p = skip_space(p);
		if(strncmp(p, "serial", 6) != 0)
			continue;

		suffix = "[0-9]*";
		if(stat(path, &st) == 0)
		{
			if(S_ISDIR(st.st_mode))
				suffix = "/[0-9]*";
			else if(nminors == 1)
				suffix = "";
		}
		full = g_strconcat(path, suffix, NULL);
		add_pattern(a, full, major_nr, minor_nr, nminors);
		g_free(full);
	}
	if(f != NULL)
		fclose(f);
	if(a->len == 0)
	{
		add_pattern(a, "/dev/ttyS*", 0, 0, 0);
		add_pattern(a, "/dev/tts/[0-9]*", 0, 0, 0);
		add_pattern(a, "/dev/usb/tts/[0-9]*", 0, 0, 0);
	}
	return a;
}

gchar *gt_serial_patterns_to_string(const GArray *patterns)
{
	GString *s = g_string_new(NULL);

	for(guint i = 0; i < patterns->len; i++)
		g_string_append_printf(s, "\t%s\n", g_array_index(patterns, GtSerialPattern, i).pattern);
	return g_string_free(s, FALSE);
}

static gboolean is_serial_port(const char *path, const GtSerialPattern *pat)
{
	struct stat st;

	if(access(path, R_OK | W_OK) != 0)
		return FALSE;
	if(stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
		return FALSE;
	/* Aucune sonde par descripteur : ouvrir un tty modifie DTR et RTS (limite de l'API du noyau) */
#if defined(major) && defined(minor)
	if(pat->nminors == 0)
		return TRUE;
	if(pat->major != major(st.st_rdev))
		return FALSE;
	if((gulong)minor(st.st_rdev) - pat->minor >= pat->nminors)
		return FALSE;
#endif
	return TRUE;
}

static void port_free(gpointer data)
{
	GtSerialPort *p = data;

	g_free(p->path);
	g_free(p);
}

static void add_port(GPtrArray *ports, const gchar *path)
{
	GtSerialPort *p = g_new0(GtSerialPort, 1);
	struct stat st;

	p->path = g_strdup(path);
	p->is_link = lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
	g_ptr_array_add(ports, p);
}

/* Liens de links_dir vers un port déjà trouvé (ex. /dev/ttyM5 -> ttyACM0, posé par une règle udev) : aucun motif de pilote ne les voit. Seuls les
 * alias de ports réels sont gardés, ce qui écarte /dev/stdin, /dev/fd et consorts. */
static void add_links(GPtrArray *ports, const gchar *links_dir)
{
	guint found_before = ports->len;
	GDir *dir = g_dir_open(links_dir, 0, NULL);
	const gchar *entry;

	if(dir == NULL)
		return;
	while((entry = g_dir_read_name(dir)) != NULL)
	{
		gchar *link = g_build_filename(links_dir, entry, NULL);
		struct stat st;
		char *target = NULL;
		gboolean match = FALSE;

		if(lstat(link, &st) == 0 && S_ISLNK(st.st_mode))
			target = realpath(link, NULL);
		for(guint i = 0; target != NULL && i < found_before && !match; i++)
			match = strcmp(target, ((GtSerialPort *)g_ptr_array_index(ports, i))->path) == 0;
		if(match)
			add_port(ports, link);
		free(target);
		g_free(link);
	}
	g_dir_close(dir);
}

static gint compare_ports(gconstpointer a, gconstpointer b)
{
	const GtSerialPort *pa = *(GtSerialPort *const *)a;
	const GtSerialPort *pb = *(GtSerialPort *const *)b;

	return gt_serial_ports_compare(pa->path, pb->path);
}

GPtrArray *gt_serial_ports_find(const GArray *patterns, const gchar *links_dir)
{
	GPtrArray *ports = g_ptr_array_new_with_free_func(port_free);

	for(guint i = 0; i < patterns->len; i++)
	{
		const GtSerialPattern *pat = &g_array_index(patterns, GtSerialPattern, i);
		glob_t gl;

		memset(&gl, 0, sizeof(gl));
		if(glob(pat->pattern, GLOB_NOSORT, NULL, &gl) != 0)
			continue;
		for(char **file = gl.gl_pathv; *file != NULL; file++)
			if(is_serial_port(*file, pat))
				add_port(ports, *file);
		globfree(&gl);
	}
	if(links_dir != NULL)
		add_links(ports, links_dir);
	g_ptr_array_sort(ports, compare_ports);
	/* Doublons (motifs qui se recouvrent) : voisins après le tri */
	for(guint i = 1; i < ports->len;)
	{
		if(strcmp(((GtSerialPort *)g_ptr_array_index(ports, i - 1))->path, ((GtSerialPort *)g_ptr_array_index(ports, i))->path) == 0)
			g_ptr_array_remove_index(ports, i);
		else
			i++;
	}
	return ports;
}

gint gt_serial_ports_compare(const gchar *a, const gchar *b)
{
	const char *sa = a, *sb = b;

	while(*sa != '\0')
	{
		unsigned char ca = (unsigned char)*sa, cb = (unsigned char)*sb;

		if(cb == '\0')
			return 1;	/* b se termine : a > b */
		if(isdigit(ca))
		{
			char *ea, *eb;
			unsigned long na, nb;

			if(!isdigit(cb))
				return -1;	/* chiffre contre autre caractère : a < b */
			na = strtoul(sa, &ea, 10);
			nb = strtoul(sb, &eb, 10);
			if(na != nb)
				return na < nb ? -1 : 1;
			if((ea - sa) != (eb - sb))
				return (ea - sa) < (eb - sb) ? -1 : 1;	/* zéros de tête : départage */
			sa = ea;
			sb = eb;
		}
		else
		{
			if(isdigit(cb))
				return 1;
			if(ca != cb)
				return (int)ca - (int)cb;
			sa++;
			sb++;
		}
	}
	return *sb != '\0' ? -1 : 0;	/* a se termine : préfixe de b (term_config.c rendait 1 ici) */
}
