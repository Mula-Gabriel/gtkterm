/* macro_set.c — jeu de macros sans GTK (voir macro_set.h). */

#include <string.h>

#include "macro_set.h"
#include "macros_format.h"
#include "macro_arg.h"

struct _GtMacroSet
{
	GPtrArray    *macros;	/* GtMacro *, possédées, dans l'ordre d'affichage */
	GtMacroLists *lists;	/* jamais NULL */
};

/* Compteur du processus : un identifiant n'est jamais réutilisé, même par un autre jeu (fil principal seulement) */
static guint next_id = 1;

static void
macro_free(gpointer data)
{
	GtMacro *m = data;

	/* apply_edits laisse des trous (NULL) dans l'ancien tableau pour les macros reprises */
	if(m == NULL)
		return;
	g_free(m->label);
	g_free(m->shortcut);
	g_free(m->action);
	g_free(m->tab);
	g_strfreev(m->args);
	g_strfreev(m->arg_min);
	g_strfreev(m->arg_max);
	g_strfreev(m->arg_charset);
	g_free(m);
}

static gchar *
normalized(const gchar *s)
{
	return g_strdup(s != NULL ? s : "");
}

/* « None » était la valeur « pas de raccourci » de l'ancienne fenêtre d'édition */
static gchar *
normalized_shortcut(const gchar *s)
{
	if(s == NULL || strcmp(s, "None") == 0)
		return g_strdup("");
	return g_strdup(s);
}

static guint
clamp_period(guint period_ms)
{
	if(period_ms == 0)
		return GT_MACRO_PERIOD_DEFAULT_MS;
	return CLAMP(period_ms, GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS);
}

static void
set_fields(GtMacro *m, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	g_free(m->label);
	g_free(m->shortcut);
	g_free(m->action);
	g_free(m->tab);
	m->label = normalized(label);
	m->shortcut = normalized_shortcut(shortcut);
	m->action = normalized(action);
	m->tab = normalized(tab);
}

static GtMacro *
macro_new(const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	GtMacro *m = g_new0(GtMacro, 1);

	m->id = next_id++;
	set_fields(m, label, shortcut, action, tab);
	m->polling_period_ms = GT_MACRO_PERIOD_DEFAULT_MS;
	return m;
}

/* Valide en place les trois vecteurs de limites (chacun NULL ou de n éléments, ""-complétés) pour les types des arguments ;
 * rend le nombre d'entrées remises à "". Une paire min > max est remise à "" des deux côtés. */
static gint
sanitize_limits(const macro_arg_info_t *infos, gint n, gchar **min, gchar **max, gchar **charset)
{
	gint replaced = 0;

	for(gint k = 0; k < n; k++)
	{
		const gchar *type = infos[k].type;
		gchar **entry[3] = { min, max, charset };

		for(gint v = 0; v < 3; v++)
		{
			gboolean ok;

			if(entry[v] == NULL || entry[v][k][0] == '\0')
				continue;
			if(v == 2)
				ok = gt_macro_arg_limits_valid(type, "", "", entry[v][k]);
			else
				ok = gt_macro_arg_limit_valid(type, entry[v][k]);
			if(!ok)
			{
				g_free(entry[v][k]);
				entry[v][k] = g_strdup("");
				replaced++;
			}
		}
		if(min != NULL && max != NULL && min[k][0] != '\0' && max[k][0] != '\0'
		   && !gt_macro_arg_limits_valid(type, min[k], max[k], ""))
		{
			g_free(min[k]);
			g_free(max[k]);
			min[k] = g_strdup("");
			max[k] = g_strdup("");
			replaced += 2;
		}
	}
	return replaced;
}

static gboolean
vector_all_empty(gchar **v)
{
	for(guint k = 0; v[k] != NULL; k++)
		if(v[k][0] != '\0')
			return FALSE;
	return TRUE;
}

/* Un vecteur dont la longueur ne correspond plus au nombre d'arguments est abandonné */
static void
drop_if_length(gchar ***v, gint n)
{
	if(*v != NULL && g_strv_length(*v) != (guint)n)
		g_clear_pointer(v, g_strfreev);
}

/* Les arguments enregistrés et leurs limites ne restent que s'ils correspondent encore au nombre de spécificateurs ;
 * chaque limite est revalidée pour le type courant de son argument (sans avertissement) */
static void
check_args(const GtMacroSet *set, GtMacro *m)
{
	gint n = 0;
	macro_arg_info_t *infos;
	gchar ***vecs[3] = { &m->arg_min, &m->arg_max, &m->arg_charset };

	if(m->args == NULL && m->arg_min == NULL && m->arg_max == NULL && m->arg_charset == NULL)
		return;
	infos = macro_get_arg_infos(set->lists, m->action, &n);
	drop_if_length(&m->args, n);
	for(gint v = 0; v < 3; v++)
		drop_if_length(vecs[v], n);
	sanitize_limits(infos, n, m->arg_min, m->arg_max, m->arg_charset);
	for(gint v = 0; v < 3; v++)
		if(*vecs[v] != NULL && vector_all_empty(*vecs[v]))
			g_clear_pointer(vecs[v], g_strfreev);
	macro_arg_infos_free(infos, n);
}

static GtMacro *
find_mutable(const GtMacroSet *set, guint id)
{
	if(set == NULL || id == 0)
		return NULL;
	for(guint i = 0; i < set->macros->len; i++)
	{
		GtMacro *m = g_ptr_array_index(set->macros, i);

		if(m->id == id)
			return m;
	}
	return NULL;
}

GtMacroSet *
gt_macro_set_new(void)
{
	GtMacroSet *set = g_new0(GtMacroSet, 1);

	set->macros = g_ptr_array_new_with_free_func(macro_free);
	set->lists = gt_macro_lists_new();
	return set;
}

void
gt_macro_set_free(GtMacroSet *set)
{
	if(set == NULL)
		return;
	g_ptr_array_unref(set->macros);
	gt_macro_lists_free(set->lists);
	g_free(set);
}

guint
gt_macro_set_count(const GtMacroSet *set)
{
	return set != NULL ? set->macros->len : 0;
}

const GtMacro *
gt_macro_set_get(const GtMacroSet *set, guint index)
{
	if(set == NULL || index >= set->macros->len)
		return NULL;
	return g_ptr_array_index(set->macros, index);
}

const GtMacro *
gt_macro_set_find(const GtMacroSet *set, guint id)
{
	return find_mutable(set, id);
}

gint
gt_macro_set_index_of(const GtMacroSet *set, guint id)
{
	if(set == NULL || id == 0)
		return -1;
	for(guint i = 0; i < set->macros->len; i++)
		if(((GtMacro *)g_ptr_array_index(set->macros, i))->id == id)
			return (gint)i;
	return -1;
}

const GtMacro *
gt_macro_set_find_label(const GtMacroSet *set, const gchar *label)
{
	if(set == NULL || label == NULL)
		return NULL;
	for(guint i = 0; i < set->macros->len; i++)
	{
		GtMacro *m = g_ptr_array_index(set->macros, i);

		if(strcmp(m->label, label) == 0)
			return m;
	}
	return NULL;
}

guint
gt_macro_set_append(GtMacroSet *set, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	GtMacro *m = macro_new(label, shortcut, action, tab);

	g_ptr_array_add(set->macros, m);
	return m->id;
}

const GtMacroLists *
gt_macro_set_get_lists(const GtMacroSet *set)
{
	return set != NULL ? set->lists : NULL;
}

void
gt_macro_set_take_lists(GtMacroSet *set, GtMacroLists *lists)
{
	/* Les listes déjà en place : rien à libérer (elles seraient libérées puis gardées) */
	if(lists != set->lists)
	{
		gt_macro_lists_free(set->lists);
		set->lists = lists != NULL ? lists : gt_macro_lists_new();
	}
	for(guint i = 0; i < set->macros->len; i++)
		check_args(set, g_ptr_array_index(set->macros, i));
}

gboolean
gt_macro_set_set_arg(GtMacroSet *set, guint id, gint arg_index, const gchar *value)
{
	GtMacro *m = find_mutable(set, id);
	gint n;

	if(m == NULL)
		return FALSE;
	n = macro_count_format_args(set->lists, m->action);
	if(arg_index < 0 || arg_index >= n)
		return FALSE;
	if(m->args == NULL)
	{
		m->args = g_new0(gchar *, n + 1);
		for(gint k = 0; k < n; k++)
			m->args[k] = g_strdup("");
	}
	g_free(m->args[arg_index]);
	m->args[arg_index] = g_strdup(value != NULL ? value : "");
	return TRUE;
}

gboolean
gt_macro_set_set_args(GtMacroSet *set, guint id, const gchar *const *args, gint n_args)
{
	GtMacro *m = find_mutable(set, id);
	gint n;

	if(m == NULL)
		return FALSE;
	n = macro_count_format_args(set->lists, m->action);
	if(n_args != n)
		return FALSE;
	g_clear_pointer(&m->args, g_strfreev);
	if(n == 0)
		return TRUE;
	m->args = g_new0(gchar *, n + 1);
	for(gint k = 0; k < n; k++)
		m->args[k] = g_strdup(args != NULL && args[k] != NULL ? args[k] : "");
	return TRUE;
}

gboolean
gt_macro_set_clear_args(GtMacroSet *set, guint id)
{
	GtMacro *m = find_mutable(set, id);

	if(m == NULL)
		return FALSE;
	g_clear_pointer(&m->args, g_strfreev);
	return TRUE;
}

/* Copie un vecteur de n éléments (NULL entrées → "") ; NULL si le vecteur est NULL ; *ok FALSE si sa longueur n'est pas n */
static gchar **
copy_vector(const gchar *const *v, gint n, gboolean *ok)
{
	gchar **c;

	if(v == NULL)
		return NULL;
	if(g_strv_length((gchar **)v) != (guint)n)
	{
		*ok = FALSE;
		return NULL;
	}
	c = g_new0(gchar *, n + 1);
	for(gint k = 0; k < n; k++)
		c[k] = g_strdup(v[k]);
	return c;
}

gint
gt_macro_set_set_limits(GtMacroSet *set, guint id, const gchar *const *min, const gchar *const *max,
                        const gchar *const *charset, gint n_args)
{
	GtMacro *m = find_mutable(set, id);
	macro_arg_info_t *infos;
	gchar **v[3];
	const gchar *const *src[3] = { min, max, charset };
	gboolean ok = TRUE;
	gint n = 0, replaced = 0;

	if(m == NULL)
		return -1;
	infos = macro_get_arg_infos(set->lists, m->action, &n);
	if(n_args != n)
		ok = FALSE;
	for(gint i = 0; i < 3; i++)
		v[i] = ok ? copy_vector(src[i], n, &ok) : NULL;
	if(!ok)
	{
		for(gint i = 0; i < 3; i++)
			g_strfreev(v[i]);
		macro_arg_infos_free(infos, n);
		return -1;
	}
	replaced = sanitize_limits(infos, n, v[0], v[1], v[2]);
	macro_arg_infos_free(infos, n);
	g_strfreev(m->arg_min);
	g_strfreev(m->arg_max);
	g_strfreev(m->arg_charset);
	m->arg_min = v[0] != NULL && !vector_all_empty(v[0]) ? v[0] : (g_strfreev(v[0]), NULL);
	m->arg_max = v[1] != NULL && !vector_all_empty(v[1]) ? v[1] : (g_strfreev(v[1]), NULL);
	m->arg_charset = v[2] != NULL && !vector_all_empty(v[2]) ? v[2] : (g_strfreev(v[2]), NULL);
	return replaced;
}

gboolean
gt_macro_set_set_polling(GtMacroSet *set, guint id, gboolean polling, guint period_ms)
{
	GtMacro *m = find_mutable(set, id);

	if(m == NULL)
		return FALSE;
	m->polling = polling;
	m->polling_period_ms = clamp_period(period_ms);
	return TRUE;
}

GtMacroSet *
gt_macro_set_copy(const GtMacroSet *set)
{
	GtMacroSet *copy = gt_macro_set_new();

	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe */
	gt_macro_set_take_lists(copy, gt_macro_lists_copy(set->lists));
	for(guint i = 0; i < set->macros->len; i++)
	{
		const GtMacro *m = g_ptr_array_index(set->macros, i);
		guint id = gt_macro_set_append(copy, m->label, m->shortcut, m->action, m->tab);

		if(m->args != NULL)
			gt_macro_set_set_args(copy, id, (const gchar *const *)m->args, (gint)g_strv_length(m->args));
		if(m->arg_min != NULL || m->arg_max != NULL || m->arg_charset != NULL)
		{
			gint n = (gint)g_strv_length(m->arg_min != NULL ? m->arg_min : m->arg_max != NULL ? m->arg_max : m->arg_charset);

			gt_macro_set_set_limits(copy, id, (const gchar *const *)m->arg_min, (const gchar *const *)m->arg_max,
			                        (const gchar *const *)m->arg_charset, n);
		}
		gt_macro_set_set_polling(copy, id, m->polling, m->polling_period_ms);
	}
	return copy;
}

void
gt_macro_set_apply_edits(GtMacroSet *set, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists)
{
	GPtrArray *old = set->macros;
	GPtrArray *fresh = g_ptr_array_new_with_free_func(macro_free);

	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe */
	if(lists != NULL && lists != set->lists)
	{
		gt_macro_lists_free(set->lists);
		set->lists = lists;
	}
	for(guint i = 0; i < n_rows; i++)
	{
		GtMacro *m = NULL;

		/* Identifiant connu et pas encore repris : la macro garde son identité, ses arguments et son polling (M16) */
		for(guint k = 0; rows[i].id != 0 && k < old->len; k++)
		{
			GtMacro *o = g_ptr_array_index(old, k);

			if(o != NULL && o->id == rows[i].id)
			{
				m = o;
				old->pdata[k] = NULL;
				break;
			}
		}
		if(m == NULL)
			m = macro_new(rows[i].label, rows[i].shortcut, rows[i].action, rows[i].tab);
		else
			set_fields(m, rows[i].label, rows[i].shortcut, rows[i].action, rows[i].tab);
		/* Les valeurs par défaut sont celles de la ligne (vérifiées après, avec les listes définitives) */
		g_clear_pointer(&m->args, g_strfreev);
		if(rows[i].args != NULL && rows[i].args[0] != NULL)
			m->args = g_strdupv((gchar **)rows[i].args);
		g_clear_pointer(&m->arg_min, g_strfreev);
		g_clear_pointer(&m->arg_max, g_strfreev);
		g_clear_pointer(&m->arg_charset, g_strfreev);
		if(rows[i].arg_min != NULL && rows[i].arg_min[0] != NULL)
			m->arg_min = g_strdupv((gchar **)rows[i].arg_min);
		if(rows[i].arg_max != NULL && rows[i].arg_max[0] != NULL)
			m->arg_max = g_strdupv((gchar **)rows[i].arg_max);
		if(rows[i].arg_charset != NULL && rows[i].arg_charset[0] != NULL)
			m->arg_charset = g_strdupv((gchar **)rows[i].arg_charset);
		g_ptr_array_add(fresh, m);
	}
	set->macros = fresh;
	g_ptr_array_unref(old);
	for(guint i = 0; i < set->macros->len; i++)
		check_args(set, g_ptr_array_index(set->macros, i));
}

/* Vecteurs de limites égaux : NULL ≡ vecteur entièrement vide */
static gboolean
limits_equal(gchar **a, gchar **b)
{
	gboolean a_empty = a == NULL || vector_all_empty(a), b_empty = b == NULL || vector_all_empty(b);
	guint na, nb;

	/* Entièrement vides (ou NULL) : égaux quelle que soit leur longueur */
	if(a_empty || b_empty)
		return a_empty && b_empty;
	na = g_strv_length(a);
	nb = g_strv_length(b);
	if(na != nb)
		return FALSE;
	for(guint k = 0; k < na; k++)
		if(strcmp(a[k], b[k]) != 0)
			return FALSE;
	return TRUE;
}

static gboolean
lists_equal(const GtMacroLists *a, const GtMacroLists *b)
{
	gint n = gt_macro_lists_count(a);

	if(n != gt_macro_lists_count(b))
		return FALSE;
	for(gint li = 0; li < n; li++)
	{
		gint ne = gt_macro_lists_entry_count(a, li);

		if(g_strcmp0(gt_macro_lists_name(a, li), gt_macro_lists_name(b, li)) != 0 || ne != gt_macro_lists_entry_count(b, li))
			return FALSE;
		for(gint ei = 0; ei < ne; ei++)
			if(g_strcmp0(gt_macro_lists_entry_display(a, li, ei), gt_macro_lists_entry_display(b, li, ei)) != 0
			   || g_strcmp0(gt_macro_lists_entry_value(a, li, ei), gt_macro_lists_entry_value(b, li, ei)) != 0)
				return FALSE;
	}
	return TRUE;
}

gboolean
gt_macro_set_same_structure(const GtMacroSet *a, const GtMacroSet *b)
{
	if(gt_macro_set_count(a) != gt_macro_set_count(b))
		return FALSE;
	for(guint i = 0; i < gt_macro_set_count(a); i++)
	{
		const GtMacro *ma = gt_macro_set_get(a, i), *mb = gt_macro_set_get(b, i);

		if(strcmp(ma->label, mb->label) != 0 || strcmp(ma->shortcut, mb->shortcut) != 0 || strcmp(ma->action, mb->action) != 0
		   || strcmp(ma->tab, mb->tab) != 0 || !limits_equal(ma->arg_min, mb->arg_min) || !limits_equal(ma->arg_max, mb->arg_max)
		   || !limits_equal(ma->arg_charset, mb->arg_charset))
			return FALSE;
	}
	return lists_equal(gt_macro_set_get_lists(a), gt_macro_set_get_lists(b));
}

GtMacroSet *
gt_macro_set_clone(const GtMacroSet *set)
{
	GtMacroSet *clone = gt_macro_set_new();

	gt_macro_lists_free(clone->lists);
	clone->lists = gt_macro_lists_copy(set->lists);
	for(guint i = 0; i < set->macros->len; i++)
	{
		const GtMacro *m = g_ptr_array_index(set->macros, i);
		GtMacro *c = g_new0(GtMacro, 1);

		/* Champs recopiés tels quels, déjà vérifiés dans l'original : pas de check_args */
		c->id = m->id;
		c->label = g_strdup(m->label);
		c->shortcut = g_strdup(m->shortcut);
		c->action = g_strdup(m->action);
		c->tab = g_strdup(m->tab);
		c->args = g_strdupv(m->args);
		c->arg_min = g_strdupv(m->arg_min);
		c->arg_max = g_strdupv(m->arg_max);
		c->arg_charset = g_strdupv(m->arg_charset);
		c->polling = m->polling;
		c->polling_period_ms = m->polling_period_ms;
		g_ptr_array_add(clone->macros, c);
	}
	return clone;
}

gboolean
gt_macro_set_group_by_tab(GtMacroSet *set)
{
	GPtrArray *keys, *order;
	gboolean changed = FALSE;

	if(set == NULL)
		return FALSE;
	keys = g_ptr_array_new();	/* tab distincts (empruntés), ordre de première apparition */
	for(guint i = 0; i < set->macros->len; i++)
	{
		const GtMacro *m = g_ptr_array_index(set->macros, i);

		if(!g_ptr_array_find_with_equal_func(keys, m->tab, g_str_equal, NULL))
			g_ptr_array_add(keys, m->tab);
	}
	order = g_ptr_array_sized_new(set->macros->len);	/* mêmes pointeurs, non possédés */
	for(guint k = 0; k < keys->len; k++)
		for(guint i = 0; i < set->macros->len; i++)
		{
			GtMacro *m = g_ptr_array_index(set->macros, i);

			if(strcmp(m->tab, g_ptr_array_index(keys, k)) == 0)
				g_ptr_array_add(order, m);
		}
	for(guint i = 0; i < order->len; i++)
	{
		changed |= set->macros->pdata[i] != order->pdata[i];
		set->macros->pdata[i] = order->pdata[i];
	}
	g_ptr_array_unref(order);
	g_ptr_array_unref(keys);
	return changed;
}

gboolean
gt_macro_action_is_separator(const gchar *action, gchar **label_out)
{
	const gchar *end, *rest;

	if(label_out != NULL)
		*label_out = NULL;
	if(action == NULL)
		return FALSE;
	if(action[0] != '[')
		return strcmp(action, "%&") == 0;
	end = strchr(action + 1, ']');
	if(end == NULL)
		return FALSE;
	for(rest = end + 1; *rest == ' '; rest++)
		;
	if(strcmp(rest, "%&") != 0)
		return FALSE;
	if(label_out != NULL)
		*label_out = g_strndup(action + 1, end - action - 1);
	return TRUE;
}

gboolean
gt_macro_is_sendable(const GtMacro *macro)
{
	return macro != NULL && macro->action[0] != '\0' && !gt_macro_action_is_separator(macro->action, NULL);
}

/* Sorte de l'octet issu des caractères origin du texte mis en forme (§2.9) : origine tenant dans la plage d'un argument → ARG (un
 * antislash d'argument, doublé en interne, reste à l'argument) ; sinon plusieurs caractères → ESCAPE ; sinon TEXT. */
static GtMacroSegKind
segment_kind(const GtMacroRange *origin, const GArray *arg_ranges, gint *arg_out)
{
	*arg_out = -1;
	for(guint k = 0; k < arg_ranges->len; k++)
	{
		const GtMacroRange *r = &g_array_index(arg_ranges, GtMacroRange, k);

		if(origin->start >= r->start && origin->start + origin->len <= r->start + r->len)
		{
			*arg_out = (gint)k;
			return GT_MACRO_SEG_ARG;
		}
	}
	return origin->len > 1 ? GT_MACRO_SEG_ESCAPE : GT_MACRO_SEG_TEXT;
}

static GArray *
build_segments(const GArray *origins, const GArray *arg_ranges)
{
	GArray *segs = g_array_new(FALSE, FALSE, sizeof(GtMacroSegment));

	for(guint b = 0; b < origins->len; b++)
	{
		gint arg;
		GtMacroSegKind kind = segment_kind(&g_array_index(origins, GtMacroRange, b), arg_ranges, &arg);
		GtMacroSegment *last = segs->len > 0 ? &g_array_index(segs, GtMacroSegment, segs->len - 1) : NULL;

		if(last != NULL && last->kind == kind && last->arg == arg)
			last->len++;
		else
		{
			GtMacroSegment seg = { kind, arg, b, 1 };

			g_array_append_val(segs, seg);
		}
	}
	return segs;
}

GBytes *
gt_macro_expand_segments(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args,
                         GArray **segments_out)
{
	const gchar *const *use = args;
	gint n = n_args;
	const gchar *const *charsets = NULL;
	GArray *arg_ranges = NULL, *origins = NULL;
	gchar *formatted;
	GByteArray *bytes;

	if(segments_out != NULL)
		*segments_out = NULL;
	if(macro == NULL)
		return NULL;
	if(n_args < 0)
	{
		use = (const gchar *const *)macro->args;
		n = macro->args != NULL ? (gint)g_strv_length(macro->args) : 0;
	}
	else if(args == NULL)
		n = 0;
	/* arg_charset a exactement un élément par spécificateur de l'action : la mise en forme n'en lit jamais au-delà */
	charsets = (const gchar *const *)macro->arg_charset;
	if(segments_out != NULL)
	{
		arg_ranges = g_array_new(FALSE, FALSE, sizeof(GtMacroRange));
		origins = g_array_new(FALSE, FALSE, sizeof(GtMacroRange));
	}
	formatted = format_action_traced(gt_macro_set_get_lists(set), macro->action, (const gchar **)use, n, charsets, arg_ranges);
	bytes = parse_macro_string_traced(formatted, origins);
	if(segments_out != NULL)
	{
		*segments_out = build_segments(origins, arg_ranges);
		g_array_unref(origins);
		g_array_unref(arg_ranges);
	}
	g_free(formatted);
	return g_byte_array_free_to_bytes(bytes);
}

GBytes *
gt_macro_expand(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args)
{
	return gt_macro_expand_segments(set, macro, args, n_args, NULL);
}

/* Entrée k d'un vecteur de limites (NULL : aucune). */
static const gchar *
limit_at(gchar **v, gint k)
{
	return v != NULL ? v[k] : NULL;
}

gboolean
gt_macro_check_args(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args,
                    gint *index_out, gchar **reason_out)
{
	const gchar *const *use = args;
	gint n = n_args;
	gint count = 0;
	macro_arg_info_t *infos;
	gboolean ok = TRUE;

	if(index_out != NULL)
		*index_out = -1;
	if(reason_out != NULL)
		*reason_out = NULL;
	if(macro == NULL)
		return TRUE;
	if(n_args < 0)
	{
		use = (const gchar *const *)macro->args;
		n = macro->args != NULL ? (gint)g_strv_length(macro->args) : 0;
	}
	if(use == NULL)
		return TRUE;
	infos = macro_get_arg_infos(gt_macro_set_get_lists(set), macro->action, &count);
	for(gint k = 0; k < MIN(n, count) && ok; k++)
	{
		if(infos[k].list_name != NULL)
			continue;
		if(gt_macro_arg_check(infos[k].type, use[k] != NULL ? use[k] : "", limit_at(macro->arg_min, k), limit_at(macro->arg_max, k),
		                      limit_at(macro->arg_charset, k), reason_out) != GT_MACRO_ARG_OK)
		{
			ok = FALSE;
			if(index_out != NULL)
				*index_out = k;
		}
	}
	macro_arg_infos_free(infos, count);
	return ok;
}

gboolean
gt_macro_period_parse(const gchar *text, guint *period_ms)
{
	guint64 value;

	/* g_ascii_string_to_unsigned refuse signe, blancs, texte en trop et dépassement */
	if(text == NULL || !g_ascii_string_to_unsigned(text, 10, GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS, &value, NULL))
		return FALSE;
	*period_ms = (guint)value;
	return TRUE;
}
