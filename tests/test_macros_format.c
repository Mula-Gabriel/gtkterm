/* Tests de macros_format.c : séquences d'échappement, expansion des spécificateurs,
 * validation des valeurs, description des arguments. Les octets attendus sont ceux
 * envoyés sur le port : parse_macro_string(format_action_with_args(...)). */

#include <glib.h>
#include <locale.h>
#include <string.h>

#include "macros_format.h"
#include "macros_list.h"
#include "test_util.h"

static GtMacroLists *listes = NULL;	/* « Mode » : Run=R, Stop=S ; aucune liste « x » ni « o » (cas M18) */

/* ---- Échappements ---------------------------------------------------- */

typedef struct
{
	const gchar *entree;
	const gchar *octets;	/* attendu, en hexadécimal */
	const gchar *bug;
} cas_echappement_t;

static const cas_echappement_t cas_echappements[] =
{
	{ "ABC", "41 42 43", NULL },
	{ "\\x41", "41", NULL },
	{ "\\X7f", "7F", NULL },
	{ "\\x4", "04", NULL },
	{ "\\x4G", "04 47", NULL },
	{ "\\x414", "41 34", NULL },		/* comportement actuel, à décider : deux chiffres au plus */
	{ "\\x", "5C 78", NULL },
	{ "\\xZZ", "5C 78 5A 5A", NULL },
	{ "\\x0x", "00 78", NULL },
	{ "\\30", "5C 33 30", NULL },		/* correctif 7a697b2 : plus d'octal */
	{ "\\306", "5C 33 30 36", NULL },
	{ "\\0", "5C 30", NULL },		/* comportement actuel, à décider */
	{ "\\r\\n", "0D 0A", NULL },
	{ "\\t", "09", NULL },
	{ "\\\\", "5C", NULL },
	{ "\\\\x41", "5C 78 34 31", NULL },
	{ "a\\", "61 5C", NULL },
	{ "\\e", "5C 65", NULL },		/* comportement actuel, à décider */
	{ "\\\"", "5C 22", NULL },
};

static void
test_echappement(gconstpointer data)
{
	const cas_echappement_t *c = data;
	GByteArray *octets = parse_macro_string(c->entree);
	gchar *obtenu = test_hex(octets->data, octets->len);
	gchar *detail = g_strdup_printf("'%s' : attendu %s, obtenu %s", c->entree, c->octets, obtenu);

	test_conclude(g_strcmp0(obtenu, c->octets) == 0, c->bug, detail);
	g_free(detail);
	g_free(obtenu);
	g_byte_array_free(octets, TRUE);
}

/* ---- Expansion -------------------------------------------------------- */

typedef struct
{
	const gchar *action;
	const gchar *arg1;	/* NULL : aucun argument */
	const gchar *arg2;	/* NULL : au plus un argument */
	const gchar *attendu;	/* octets attendus, sous forme de chaîne C */
	const gchar *bug;
} cas_expansion_t;

static const cas_expansion_t cas_expansions[] =
{
	{ "V%d\\r", "42", NULL, "V42\r", NULL },
	{ "%hhd", "200", NULL, "-56", NULL },		/* tronqué ; la validation refuse 200 en amont */
	{ "%hhu", "300", NULL, "44", NULL },
	{ "%hd", "70000", NULL, "4464", NULL },
	{ "%u", "-1", NULL, "4294967295", NULL },
	{ "%lu", "-1", NULL, "18446744073709551615", NULL },
	{ "%d", "-5", NULL, "-5", NULL },
	{ "%+d", "5", NULL, "+5", NULL },
	{ "%-5d|", "5", NULL, "5    |", NULL },
	{ "%i", "42", NULL, "42", NULL },
	{ "%d", "12abc", NULL, "12", NULL },
	{ "%d", "", NULL, "0", NULL },
	{ "%x", "255", NULL, "ff", NULL },
	{ "%X", "255", NULL, "FF", NULL },
	{ "%02X", "10", NULL, "0A", NULL },
	{ "%x", "0x1f", NULL, "1f", NULL },
	{ "%X", "0x1f", NULL, "1F", NULL },
	{ "%d", "0x10", NULL, "16", NULL },		/* décision 2 : 0x accepté pour tous les entiers */
	{ "%u", "0x10", NULL, "16", NULL },
	{ "%d", "010", NULL, "10", NULL },
	{ "%o", "010", NULL, "12", NULL },
	{ "%x", "ff", NULL, "0", NULL },	/* « ff » sans 0x n'est pas un nombre : refusé à la validation */
	{ "%x", "010", NULL, "a", NULL },	/* décision 2 : décimal, jamais d'octal */
	{ "%o", "8", NULL, "10", NULL },
	{ "%#x", "255", NULL, "0xff", NULL },		/* aucune liste « x » : drapeau # de printf */
	{ "%#08x", "255", NULL, "0x0000ff", NULL },
	{ "%#o", "8", NULL, "010", NULL },
	{ "%f", "2.00000", NULL, "2.00000", NULL },	/* 9f48921 : zéros de fin conservés */
	{ "%f", "1e3", NULL, "1e3", NULL },		/* comportement actuel : valeur brute */
	{ "%f", "abc", NULL, "abc", NULL },		/* comportement actuel : la validation refuse « abc » en amont */
	{ "%.2f", "3.14159", NULL, "3.14", NULL },
	{ "%5.1f", "2", NULL, "  2.0", NULL },
	{ "%lf", "2.50", NULL, "2.50", NULL },
	{ "%e", "1234.5", NULL, "1.234500e+03", NULL },
	{ "%hf", "2.0", NULL, "2.0", NULL },
	{ "%5s", "abc", NULL, "  abc", NULL },
	{ "%s%s", "a", "b", "ab", NULL },
	{ "A\\r%sB", "x", NULL, "A\rxB", NULL },
	{ "%s", "a\\b", NULL, "a\\b", NULL },
	{ "%s", "\\x41", NULL, "\\x41", NULL },		/* f26847f : pas d'interprétation dans %s */
	{ "%s", "\\r", NULL, "\\r", NULL },
	{ "%s", "50%", NULL, "50%", NULL },
	{ "%5s", "a\\b", NULL, "  a\\b", NULL },	/* largeur : 5 octets attendus */
	{ "%cn", "\\", NULL, "\\n", NULL },		/* « \ » puis « n », pas un saut de ligne */
	{ "%c", "A", NULL, "A", NULL },
	{ "%c", "", NULL, " ", NULL },			/* comportement actuel, à décider */
	{ "100%%", "1", NULL, "100%", NULL },
	{ "100%%", NULL, NULL, "100%", NULL },
	{ "%d %d", "1", NULL, "1 %d", NULL },		/* argument manquant : spécificateur laissé tel quel */
	{ "%d", NULL, NULL, "%d", NULL },
	{ "plain", NULL, NULL, "plain", NULL },
	{ "[Speed] %d", "5", NULL, "5", NULL },
	{ "[a] %d [b] %d", "1", "2", "1 2", NULL },
	{ "XY[a]%d", "5", NULL, "XY5", NULL },
	{ "XY[a%%]%d", "5", NULL, "XY5", NULL },	/* le « Y » disparaît */
	{ "arr[0] %d", "5", NULL, "arr5", NULL },	/* à décider : « [0] » pris pour un libellé */
	{ "50% off", "9", NULL, "5011ff", NULL },	/* à décider : « % o » est un spécificateur valide */
	{ "%#Mode", "S", NULL, "S", NULL },
	{ "%#Mode", "\\x02", NULL, "\\x02", NULL },	/* à décider : valeurs de liste sans échappements */
	{ "%d%&", "1", NULL, "1%&", NULL },		/* le séparateur %& est traité par le panneau */
};

static void
test_expansion(gconstpointer data)
{
	const cas_expansion_t *c = data;
	const gchar *args[2] = { c->arg1, c->arg2 };
	gint n_args = c->arg2 ? 2 : (c->arg1 ? 1 : 0);
	gchar *texte = format_action_with_args(listes, c->action, args, n_args);
	GByteArray *octets = parse_macro_string(texte);
	gchar *attendu = test_hex((const guint8 *)c->attendu, strlen(c->attendu));
	gchar *obtenu = test_hex(octets->data, octets->len);
	gchar *detail = g_strdup_printf("'%s' [%s][%s] : attendu %s, obtenu %s", c->action, c->arg1 ? c->arg1 : "-", c->arg2 ? c->arg2 : "-", attendu, obtenu);

	test_conclude(g_strcmp0(obtenu, attendu) == 0, c->bug, detail);
	g_free(detail);
	g_free(obtenu);
	g_free(attendu);
	g_byte_array_free(octets, TRUE);
	g_free(texte);
}

/* ---- Validation ------------------------------------------------------- */

typedef struct
{
	const gchar *type;
	const gchar *valeur;
	gboolean attendu;
	const gchar *bug;
} cas_validation_t;

static const cas_validation_t cas_validations[] =
{
	{ "hhd", "127", TRUE, NULL },
	{ "hhd", "128", FALSE, NULL },
	{ "hhd", "-128", TRUE, NULL },
	{ "hhd", "-129", FALSE, NULL },
	{ "hhu", "255", TRUE, NULL },
	{ "hhu", "256", FALSE, NULL },
	{ "d", "2147483647", TRUE, NULL },
	{ "d", "2147483648", FALSE, NULL },
	{ "u", "-1", FALSE, NULL },
	{ "u", "4294967295", TRUE, NULL },
	{ "x", "0x10", TRUE, NULL },
	{ "x", "010", TRUE, NULL },
	{ "d", "0x10", TRUE, NULL },
	{ "u", "0x10", TRUE, NULL },
	{ "x", "ff", FALSE, NULL },
	{ "x", "09", TRUE, NULL },
	{ "f", "2.5", TRUE, NULL },
	{ "f", "2,5", FALSE, NULL },
	{ "f", "1234567890", FALSE, NULL },	/* comportement actuel, à décider : limite de chiffres */
	{ "lf", "1234567890", TRUE, NULL },
	{ "d", " 12", TRUE, NULL },
	{ "d", "12 ", FALSE, NULL },
	{ "d", "abc", FALSE, NULL },
	{ "d", "", TRUE, NULL },		/* comportement actuel, à décider : vide accepté */
	{ "s", "anything", TRUE, NULL },
};

static void
test_validation(gconstpointer data)
{
	const cas_validation_t *c = data;
	gboolean obtenu = macro_type_value_valid(c->type, c->valeur);
	gchar *detail = g_strdup_printf("%s '%s' : attendu %s, obtenu %s", c->type, c->valeur, c->attendu ? "valide" : "invalide", obtenu ? "valide" : "invalide");

	test_conclude((obtenu != FALSE) == (c->attendu != FALSE), c->bug, detail);
	g_free(detail);
}

static void
test_types_flottants(void)
{
	g_assert_true(macro_type_is_float("f"));
	g_assert_true(macro_type_is_float("lf"));
	g_assert_true(macro_type_is_float("e"));
	g_assert_false(macro_type_is_float("d"));
}

/* M9 : la locale n'est plus forcée à « C » par parsecfg ; le point reste le séparateur décimal des macros. */
static void
test_flottants_sous_locale_fr(void)
{
	gchar *ancienne = g_strdup(setlocale(LC_ALL, NULL));
	const gchar *args[1] = { "3.14159" };
	gchar *texte;
	gboolean point_valide, virgule_valide;

	if(setlocale(LC_ALL, "fr_FR.UTF-8") == NULL)
	{
		g_free(ancienne);
		g_test_skip("locale fr_FR.UTF-8 indisponible");
		return;
	}
	texte = format_action_with_args(listes, "%.2f", args, 1);
	point_valide = macro_type_value_valid("f", "2.5");
	virgule_valide = macro_type_value_valid("f", "2,5");
	setlocale(LC_ALL, ancienne);
	g_free(ancienne);

	g_assert_cmpstr(texte, ==, "3.14");
	g_assert_true(point_valide);
	g_assert_false(virgule_valide);
	g_free(texte);
}

/* ---- Description des arguments --------------------------------------- */

typedef struct
{
	const gchar *action;
	gint nombre;
	const gchar *infos;	/* « type:liste:libellé » séparés par « | », « - » si absent */
} cas_arguments_t;

static const cas_arguments_t cas_arguments[] =
{
	{ "%#Mode %d", 2, "l:Mode:-|d:-:-" },
	{ "%%d", 0, "" },
	{ "%&", 0, "" },
	{ "[Speed] %d", 1, "d:-:Speed" },
	{ "%d %s %f", 3, "d:-:-|s:-:-|f:-:-" },
	{ "plain", 0, "" },
};

static void
test_arguments(gconstpointer data)
{
	const cas_arguments_t *c = data;
	gint n = -1;
	macro_arg_info_t *infos = macro_get_arg_infos(listes, c->action, &n);
	GString *texte = g_string_new(NULL);

	for(gint i = 0; i < n; i++)
		g_string_append_printf(texte, "%s%s:%s:%s", i ? "|" : "", infos[i].type, infos[i].list_name ? infos[i].list_name : "-", infos[i].label ? infos[i].label : "-");
	g_assert_cmpint(macro_count_format_args(listes, c->action), ==, c->nombre);
	g_assert_cmpint(n, ==, c->nombre);
	g_assert_cmpstr(texte->str, ==, c->infos);
	g_string_free(texte, TRUE);
	macro_arg_infos_free(infos, n);
}

/* ---- Enregistrement --------------------------------------------------- */

/* Étiquette de chemin GTest à partir d'un texte : lettres et chiffres gardés, le reste remplacé par « - », 24 caractères au plus. */
static gchar *
etiquette(const gchar *texte)
{
	GString *e = g_string_new(NULL);

	for(const gchar *p = texte; *p != '\0' && e->len < 24; p++)
		g_string_append_c(e, g_ascii_isalnum(*p) ? *p : '-');
	return g_string_free(e, FALSE);
}

#define AJOUTER_TABLE(prefixe, table, champ, fonction) \
	for(guint i = 0; i < G_N_ELEMENTS(table); i++) \
	{ \
		gchar *nom = etiquette(table[i].champ); \
		gchar *chemin = g_strdup_printf("/macros_format/" prefixe "/%02u-%s", i, nom); \
		g_test_add_data_func(chemin, &table[i], fonction); \
		g_free(chemin); \
		g_free(nom); \
	}

/* « %#x » : liste si une liste « x » existe dans les listes passées, drapeau printf sinon (listes NULL comprises). */
static void
test_listes_en_parametre(void)
{
	GtMacroLists *avec_x = gt_macro_lists_new();
	const gchar *args[] = { "255" };
	gchar *texte;

	gt_macro_lists_add(avec_x, "x", "Huit", "8");
	texte = format_action_with_args(NULL, "%#x", args, 1);
	g_assert_cmpstr(texte, ==, "0xff");
	g_free(texte);
	texte = format_action_with_args(avec_x, "%#x", args, 1);
	g_assert_cmpstr(texte, ==, "255");
	g_free(texte);
	g_assert_cmpint(macro_count_format_args(NULL, "%#Mode %d"), ==, 1);
	g_assert_cmpint(macro_count_format_args(listes, "%#Mode %d"), ==, 2);
	gt_macro_lists_free(avec_x);
}

int
main(int argc, char **argv)
{
	int resultat;

	g_test_init(&argc, &argv, NULL);
	/* strtod dépend aujourd'hui de la locale « C » imposée en douce par parsecfg (bogue M9). */
	setlocale(LC_NUMERIC, "C");
	/* Liste « Mode » pour %#Mode ; aucune liste « x » ni « o » (cas M18). */
	listes = gt_macro_lists_new();
	gt_macro_lists_add(listes, "Mode", "Run", "R");
	gt_macro_lists_add(listes, "Mode", "Stop", "S");

	AJOUTER_TABLE("echappement", cas_echappements, entree, test_echappement);
	AJOUTER_TABLE("expansion", cas_expansions, action, test_expansion);
	AJOUTER_TABLE("validation", cas_validations, valeur, test_validation);
	AJOUTER_TABLE("arguments", cas_arguments, action, test_arguments);
	g_test_add_func("/macros_format/types-flottants", test_types_flottants);
	g_test_add_func("/macros_format/flottants-sous-locale-fr", test_flottants_sous_locale_fr);
	g_test_add_func("/macros_format/listes-en-parametre", test_listes_en_parametre);

	resultat = g_test_run();
	gt_macro_lists_free(listes);
	return resultat;
}
