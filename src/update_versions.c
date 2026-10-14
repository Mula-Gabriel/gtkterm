/* Versions publiées, version installée et notes de version : voir update_versions.h. */

#include <glib.h>
#include <string.h>

#include "update_versions.h"

/* Nombre décimal en tête de *p : 1 à 9 chiffres, sans zéro de tête superflu ; *p avance après lui */
static gboolean
parse_number(const gchar **p, guint *out)
{
	const gchar *s = *p;
	gsize n = 0;
	guint v = 0;

	while(g_ascii_isdigit(s[n]))
		n++;
	if(n == 0 || n > 9 || (n > 1 && s[0] == '0'))
		return FALSE;
	for(gsize i = 0; i < n; i++)
		v = v * 10 + (guint)(s[i] - '0');
	*out = v;
	*p = s + n;
	return TRUE;
}

/* « X.Y.Z » en tête de *p ; *p avance après Z */
static gboolean
parse_triplet(const gchar **p, GtVersion *v)
{
	if(!parse_number(p, &v->major) || **p != '.')
		return FALSE;
	(*p)++;
	if(!parse_number(p, &v->minor) || **p != '.')
		return FALSE;
	(*p)++;
	return parse_number(p, &v->patch);
}

gboolean
gt_version_parse(const gchar *str, GtVersion *out)
{
	GtVersion v = {0, 0, 0, 0};
	const gchar *p = str;

	if(str == NULL || out == NULL)
		return FALSE;
	if(*p == 'v')
		p++;
	if(!parse_triplet(&p, &v))
		return FALSE;
	if(*p == '-')
	{
		/* git describe : « -N-g<hex> », N ≥ 1 */
		gsize n = 0;

		p++;
		if(!parse_number(&p, &v.distance) || v.distance == 0 || p[0] != '-' || p[1] != 'g')
			return FALSE;
		p += 2;
		while(g_ascii_isxdigit(p[n]))
			n++;
		if(n < 4 || n > 64)
			return FALSE;
		p += n;
	}
	if(*p != '\0')
		return FALSE;
	*out = v;
	return TRUE;
}

gboolean
gt_version_is_release_tag(const gchar *str)
{
	GtVersion v = {0, 0, 0, 0};
	const gchar *p = str;

	if(str == NULL || *p != 'v')
		return FALSE;
	p++;
	return parse_triplet(&p, &v) && *p == '\0';
}

static gint
cmp_uint(guint a, guint b)
{
	return a < b ? -1 : a > b ? 1 : 0;
}

gint
gt_version_compare(const GtVersion *a, const GtVersion *b)
{
	gint c = cmp_uint(a->major, b->major);

	if(c == 0)
		c = cmp_uint(a->minor, b->minor);
	if(c == 0)
		c = cmp_uint(a->patch, b->patch);
	if(c == 0)
		c = cmp_uint(a->distance, b->distance);
	return c;
}

void
gt_version_installed(const gchar *describe, const gchar *project_version, GtVersion *out)
{
	GtVersion zero = {0, 0, 0, 0};

	*out = zero;
	if(!gt_version_parse(describe, out))
		gt_version_parse(project_version, out);
}

/* Tri du plus récent au plus ancien ; a et b pointent sur des éléments du tableau (gchar *) */
static gint
compare_tags_desc(gconstpointer a, gconstpointer b)
{
	GtVersion va = {0, 0, 0, 0}, vb = {0, 0, 0, 0};

	gt_version_parse(*(const gchar *const *)a, &va);
	gt_version_parse(*(const gchar *const *)b, &vb);
	return gt_version_compare(&vb, &va);
}

GPtrArray *
gt_version_list_from_refs(const gchar *text)
{
	GPtrArray *tags = g_ptr_array_new_with_free_func(g_free);
	gchar **lines;

	if(text == NULL)
		return tags;
	lines = g_strsplit(text, "\n", -1);
	for(gchar **l = lines; *l != NULL; l++)
	{
		g_strstrip(*l);
		if(gt_version_is_release_tag(*l) && !g_ptr_array_find_with_equal_func(tags, *l, g_str_equal, NULL))
			g_ptr_array_add(tags, g_strdup(*l));
	}
	g_strfreev(lines);
	g_ptr_array_sort(tags, compare_tags_desc);
	return tags;
}

/* ---- NEWS ----------------------------------------------------------- */

static void
entry_free(gpointer p)
{
	GtNewsEntry *e = p;

	g_free(e->text);
	g_free(e);
}

static void
section_free(gpointer p)
{
	GtNewsSection *s = p;

	g_free(s->tag);
	g_free(s->title);
	g_ptr_array_unref(s->entries);
	g_free(s);
}

/* « ## vX.Y.Z… » : le tag va jusqu'au premier blanc ; NULL si la ligne n'est pas un en-tête de version */
static GtNewsSection *
section_from_heading(const gchar *line)
{
	const gchar *p;
	gchar *tag;
	gsize n = 0;
	GtNewsSection *s;

	if(!g_str_has_prefix(line, "## "))
		return NULL;
	p = line + 3;
	while(p[n] != '\0' && !g_ascii_isspace(p[n]))
		n++;
	tag = g_strndup(p, n);
	if(!gt_version_is_release_tag(tag))
	{
		g_free(tag);
		return NULL;
	}
	s = g_new0(GtNewsSection, 1);
	s->tag = tag;
	gt_version_parse(tag, &s->version);
	s->title = g_strstrip(g_strdup(p));
	s->entries = g_ptr_array_new_with_free_func(entry_free);
	return s;
}

static GtNewsEntry *
entry_new(GtNewsKind kind, const gchar *text)
{
	GtNewsEntry *e = g_new0(GtNewsEntry, 1);

	e->kind = kind;
	e->text = g_strstrip(g_strdup(text));
	return e;
}

GPtrArray *
gt_news_parse(const gchar *text)
{
	GPtrArray *sections = g_ptr_array_new_with_free_func(section_free);
	GtNewsSection *cur = NULL;
	GtNewsEntry *last = NULL;	/* élément que prolonge une ligne commençant par deux espaces */
	gchar **lines;

	if(text == NULL)
		return sections;
	lines = g_strsplit(text, "\n", -1);
	for(gchar **l = lines; *l != NULL; l++)
	{
		gchar *line = *l;
		gsize len = strlen(line);
		GtNewsSection *s;
		gchar *nu;
		gboolean vide;

		if(len > 0 && line[len - 1] == '\r')
			line[len - 1] = '\0';
		s = section_from_heading(line);
		if(s != NULL)
		{
			g_ptr_array_add(sections, s);
			cur = s;
			last = NULL;
			continue;
		}
		if(g_str_has_prefix(line, "# "))
		{
			cur = NULL;	/* titre de niveau 1 : clôt la section en cours, ce qui suit n'est pas affiché */
			last = NULL;
			continue;
		}
		if(cur == NULL)
			continue;	/* préambule */
		nu = g_strstrip(g_strdup(line));
		vide = nu[0] == '\0';
		g_free(nu);
		if(vide)
		{
			last = NULL;	/* ligne vide : ignorée, termine l'élément en cours */
			continue;
		}
		if(g_str_has_prefix(line, "  ") && last != NULL)
		{
			gchar *suite = g_strstrip(g_strdup(line));
			gchar *joint = g_strconcat(last->text, " ", suite, NULL);

			g_free(last->text);
			last->text = joint;
			g_free(suite);
			continue;
		}
		if(g_str_has_prefix(line, "- "))
			last = entry_new(GT_NEWS_ITEM, line + 2);
		else if(g_str_has_prefix(line, "! "))
			last = entry_new(GT_NEWS_WARNING, line + 2);
		else
			last = entry_new(GT_NEWS_TEXT, line);
		g_ptr_array_add(cur->entries, last);
	}
	g_strfreev(lines);
	return sections;
}

static gint
compare_sections_desc(gconstpointer a, gconstpointer b)
{
	const GtNewsSection *sa = *(GtNewsSection *const *)a;
	const GtNewsSection *sb = *(GtNewsSection *const *)b;

	return gt_version_compare(&sb->version, &sa->version);
}

static gboolean
section_has_tag(gconstpointer section, gconstpointer tag)
{
	return g_strcmp0(((const GtNewsSection *)section)->tag, tag) == 0;
}

GPtrArray *
gt_news_select(GPtrArray *sections, GPtrArray *released, const GtVersion *installed, const GtVersion *target, GtNewsDirection *direction)
{
	GPtrArray *out = g_ptr_array_new();
	gint sens = gt_version_compare(target, installed);
	const GtVersion *bas = sens > 0 ? installed : target;
	const GtVersion *haut = sens > 0 ? target : installed;

	if(direction != NULL)
		*direction = sens > 0 ? GT_NEWS_UPGRADE : sens < 0 ? GT_NEWS_ROLLBACK : GT_NEWS_SAME;
	if(sens == 0 || sections == NULL || released == NULL)
		return out;
	for(guint i = 0; i < sections->len; i++)
	{
		GtNewsSection *s = g_ptr_array_index(sections, i);

		if(gt_version_compare(&s->version, bas) <= 0 || gt_version_compare(&s->version, haut) > 0)
			continue;
		if(!g_ptr_array_find_with_equal_func(released, s->tag, g_str_equal, NULL))
			continue;	/* version sans tag (notes préparées à l'avance) : jamais affichée */
		if(g_ptr_array_find_with_equal_func(out, s->tag, section_has_tag, NULL))
			continue;	/* tag en double dans NEWS : la première section l'emporte */
		g_ptr_array_add(out, s);
	}
	g_ptr_array_sort(out, compare_sections_desc);
	return out;
}

gboolean
gt_news_has_warning(GPtrArray *selected)
{
	for(guint i = 0; selected != NULL && i < selected->len; i++)
	{
		GtNewsSection *s = g_ptr_array_index(selected, i);

		for(guint j = 0; j < s->entries->len; j++)
		{
			if(((GtNewsEntry *)g_ptr_array_index(s->entries, j))->kind == GT_NEWS_WARNING)
				return TRUE;
		}
	}
	return FALSE;
}

GtReleaseVerdict
gt_release_verdict(const GtVersion *installed, const gchar *newest, const gchar *skipped)
{
	GtVersion v = {0, 0, 0, 0};

	if(!gt_version_is_release_tag(newest) || !gt_version_parse(newest, &v) || gt_version_compare(&v, installed) <= 0)
		return GT_RELEASE_UP_TO_DATE;
	if(skipped != NULL && g_strcmp0(newest, skipped) == 0)
		return GT_RELEASE_SKIPPED;
	return GT_RELEASE_AVAILABLE;
}
