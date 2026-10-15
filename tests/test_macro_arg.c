/* Tests de macro_arg.c : jeux de caractères, classes, limites, vérification, encodage, description.
 * Les raisons sont comparées en anglais : les tests tournent sans traduction. */

#include <glib.h>
#include <locale.h>
#include <string.h>

#include "macro_arg.h"

static void
test_charset_parse(void)
{
	GtMacroCharset k = GT_MACRO_CHARSET_ASCII;
	gchar *cls = NULL;

	g_assert_true(gt_macro_charset_parse(NULL, &k, &cls));
	g_assert_cmpint(k, ==, GT_MACRO_CHARSET_UTF8);
	g_assert_null(cls);
	g_assert_true(gt_macro_charset_parse("", &k, &cls));
	g_assert_cmpint(k, ==, GT_MACRO_CHARSET_UTF8);
	g_assert_true(gt_macro_charset_parse("printable", &k, NULL));
	g_assert_cmpint(k, ==, GT_MACRO_CHARSET_PRINTABLE);
	g_assert_true(gt_macro_charset_parse("ascii", &k, NULL));
	g_assert_cmpint(k, ==, GT_MACRO_CHARSET_ASCII);
	g_assert_true(gt_macro_charset_parse("latin1", &k, NULL));
	g_assert_cmpint(k, ==, GT_MACRO_CHARSET_LATIN1);
	g_assert_true(gt_macro_charset_parse("class:A-Z0-9_-", &k, &cls));
	g_assert_cmpint(k, ==, GT_MACRO_CHARSET_CLASS);
	g_assert_cmpstr(cls, ==, "A-Z0-9_-");
	g_clear_pointer(&cls, g_free);
	g_assert_false(gt_macro_charset_parse("class:", &k, &cls));
	g_assert_null(cls);
	g_assert_false(gt_macro_charset_parse("class:z-a", &k, &cls));
	g_assert_false(gt_macro_charset_parse("utf8", &k, NULL));
	g_assert_false(gt_macro_charset_parse("PRINTABLE", &k, NULL));
}

static void
test_classes(void)
{
	g_assert_true(gt_macro_class_valid("A-Z0-9_-"));
	g_assert_true(gt_macro_class_valid("-a"));
	g_assert_true(gt_macro_class_valid("a-"));
	g_assert_true(gt_macro_class_valid("a\\-z"));
	g_assert_true(gt_macro_class_valid("\\\\"));
	g_assert_true(gt_macro_class_valid("\xc3\xa0-\xc3\xbf"));	/* à-ÿ */
	g_assert_true(gt_macro_class_valid("-"));
	g_assert_false(gt_macro_class_valid(""));
	g_assert_false(gt_macro_class_valid(NULL));
	g_assert_false(gt_macro_class_valid("z-a"));
	g_assert_false(gt_macro_class_valid("abc\\"));
	g_assert_false(gt_macro_class_valid("a-b-c"));
}

static void
test_classes_taille(void)
{
	/* 256 caractères : accepté ; 257 : refusé */
	g_assert_true(gt_macro_class_valid("\xc4\x80-\xc7\xbf"));	/* U+0100-U+01FF : 256 */
	g_assert_false(gt_macro_class_valid("\xc4\x80-\xc8\x80"));	/* U+0100-U+0200 : 257 */
	g_assert_false(gt_macro_class_valid("\xc4\x80-\xc7\xbf" "a"));	/* 256 + 1 */
}

static void
test_classes_contenu(void)
{
	g_assert_true(gt_macro_class_contains("A-Z0-9_-", 'Q'));
	g_assert_true(gt_macro_class_contains("A-Z0-9_-", '5'));
	g_assert_true(gt_macro_class_contains("A-Z0-9_-", '-'));
	g_assert_true(gt_macro_class_contains("A-Z0-9_-", '_'));
	g_assert_false(gt_macro_class_contains("A-Z0-9_-", 'q'));
	g_assert_true(gt_macro_class_contains("-a", '-'));
	g_assert_true(gt_macro_class_contains("a\\-z", '-'));
	g_assert_false(gt_macro_class_contains("a\\-z", 'b'));
	g_assert_true(gt_macro_class_contains("\\\\", '\\'));
	g_assert_true(gt_macro_class_contains("\xc3\xa0-\xc3\xbf", 0xE9));
	g_assert_false(gt_macro_class_contains("\xc3\xa0-\xc3\xbf", 'e'));
}

static void
test_limites(void)
{
	g_assert_true(gt_macro_arg_limit_valid("d", NULL));
	g_assert_true(gt_macro_arg_limit_valid("d", ""));
	g_assert_true(gt_macro_arg_limit_valid("d", "-5"));
	g_assert_true(gt_macro_arg_limit_valid("d", "0x10"));
	g_assert_true(gt_macro_arg_limit_valid("x", "0x10"));
	g_assert_false(gt_macro_arg_limit_valid("d", "1.5"));
	g_assert_false(gt_macro_arg_limit_valid("d", "abc"));
	g_assert_true(gt_macro_arg_limit_valid("hhu", "255"));
	g_assert_false(gt_macro_arg_limit_valid("hhu", "300"));
	/* « -1 » est lu comme UINT64_MAX par strtoull : valide pour llu (comportement actuel de macro_type_value_valid), refusé pour u */
	g_assert_true(gt_macro_arg_limit_valid("llu", "-1"));
	g_assert_false(gt_macro_arg_limit_valid("u", "-1"));
	g_assert_true(gt_macro_arg_limit_valid("f", "1.5"));
	g_assert_false(gt_macro_arg_limit_valid("f", "nan"));
	g_assert_false(gt_macro_arg_limit_valid("f", "inf"));
	g_assert_false(gt_macro_arg_limit_valid("Lf", "abc"));
	/* %s : longueurs 0..65535 */
	g_assert_true(gt_macro_arg_limit_valid("s", "0"));
	g_assert_true(gt_macro_arg_limit_valid("s", "65535"));
	g_assert_false(gt_macro_arg_limit_valid("s", "65536"));
	g_assert_false(gt_macro_arg_limit_valid("s", "-1"));
	g_assert_false(gt_macro_arg_limit_valid("s", "0x5"));
	g_assert_false(gt_macro_arg_limit_valid("s", "1.5"));
	/* c et liste : aucune limite */
	g_assert_true(gt_macro_arg_limit_valid("c", ""));
	g_assert_false(gt_macro_arg_limit_valid("c", "1"));
	g_assert_false(gt_macro_arg_limit_valid("l", "1"));

	g_assert_true(gt_macro_arg_type_has_limits("d"));
	g_assert_true(gt_macro_arg_type_has_limits("s"));
	g_assert_true(gt_macro_arg_type_has_limits("Lf"));
	g_assert_false(gt_macro_arg_type_has_limits("c"));
	g_assert_false(gt_macro_arg_type_has_limits("l"));
	g_assert_false(gt_macro_arg_type_has_limits(NULL));
}

static void
test_limites_couple(void)
{
	g_assert_true(gt_macro_arg_limits_valid("d", "0", "100", NULL));
	g_assert_true(gt_macro_arg_limits_valid("d", "5", "5", ""));
	g_assert_false(gt_macro_arg_limits_valid("d", "10", "5", NULL));
	g_assert_true(gt_macro_arg_limits_valid("d", "", "5", NULL));
	g_assert_true(gt_macro_arg_limits_valid("d", "-3", "0x10", NULL));
	g_assert_false(gt_macro_arg_limits_valid("f", "2.5", "1.5", NULL));
	g_assert_true(gt_macro_arg_limits_valid("x", "0", "0xFF", NULL));
	g_assert_false(gt_macro_arg_limits_valid("x", "0x20", "0x10", NULL));
	g_assert_true(gt_macro_arg_limits_valid("s", "1", "8", "printable"));
	g_assert_false(gt_macro_arg_limits_valid("s", "9", "8", NULL));
	g_assert_true(gt_macro_arg_limits_valid("s", "", "", "class:A-Z"));
	g_assert_false(gt_macro_arg_limits_valid("s", "", "", "class:z-a"));
	g_assert_false(gt_macro_arg_limits_valid("s", "", "", "bidule"));
	/* jeu de caractères sur un autre type que %s : refusé */
	g_assert_false(gt_macro_arg_limits_valid("d", "", "", "ascii"));
	g_assert_false(gt_macro_arg_limits_valid("c", "", "", "ascii"));
	g_assert_true(gt_macro_arg_limits_valid("c", "", "", ""));
	g_assert_true(gt_macro_arg_limits_valid("l", NULL, NULL, NULL));
	g_assert_false(gt_macro_arg_limits_valid("l", "1", NULL, NULL));
}

static GtMacroArgVerdict
check(const gchar *type, const gchar *value, const gchar *min, const gchar *max, const gchar *charset, const gchar *attendu)
{
	gchar *reason = NULL;
	GtMacroArgVerdict v = gt_macro_arg_check(type, value, min, max, charset, &reason);

	g_assert_cmpstr(reason, ==, attendu);
	g_free(reason);
	return v;
}

static void
test_verification_nombres(void)
{
	g_assert_cmpint(check("d", "50", "0", "100", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("d", "0", "0", "100", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("d", "100", "0", "100", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("d", "-1", "0", "100", NULL, "below minimum 0"), ==, GT_MACRO_ARG_BELOW_MIN);
	g_assert_cmpint(check("d", "101", "0", "100", NULL, "above maximum 100"), ==, GT_MACRO_ARG_ABOVE_MAX);
	g_assert_cmpint(check("d", "-5", "-10", "", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("d", "abc", "0", "100", NULL, "not a valid integer"), ==, GT_MACRO_ARG_BAD_VALUE);
	g_assert_cmpint(check("d", "0x20", "0", "0x10", NULL, "above maximum 0x10"), ==, GT_MACRO_ARG_ABOVE_MAX);
	/* vide = 0 */
	g_assert_cmpint(check("d", "", "1", "", NULL, "below minimum 1"), ==, GT_MACRO_ARG_BELOW_MIN);
	g_assert_cmpint(check("d", "", "-1", "1", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("d", NULL, "", "-1", NULL, "above maximum -1"), ==, GT_MACRO_ARG_ABOVE_MAX);
	/* non signés et hexadécimal */
	g_assert_cmpint(check("u", "5", "10", "", NULL, "below minimum 10"), ==, GT_MACRO_ARG_BELOW_MIN);
	g_assert_cmpint(check("u", "4000000000", "", "3000000000", NULL, "above maximum 3000000000"), ==, GT_MACRO_ARG_ABOVE_MAX);
	g_assert_cmpint(check("x", "0xFF", "0x10", "0xFF", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("x", "FF", "", "", NULL, "not a valid hexadecimal"), ==, GT_MACRO_ARG_BAD_VALUE);
	g_assert_cmpint(check("hhu", "300", "", "", NULL, "not a valid unsigned"), ==, GT_MACRO_ARG_BAD_VALUE);
	/* décimaux */
	g_assert_cmpint(check("f", "1.5", "1.0", "2.0", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("f", "2.5", "1.0", "2.0", NULL, "above maximum 2.0"), ==, GT_MACRO_ARG_ABOVE_MAX);
	g_assert_cmpint(check("f", "0.5", "1.0", "", NULL, "below minimum 1.0"), ==, GT_MACRO_ARG_BELOW_MIN);
	g_assert_cmpint(check("f", "", "-1", "1", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("f", "nan", "0", "1", NULL, "not a valid decimal"), ==, GT_MACRO_ARG_BAD_VALUE);
	g_assert_cmpint(check("f", "inf", "", "1", NULL, "not a valid decimal"), ==, GT_MACRO_ARG_BAD_VALUE);
	/* sans limite, nan reste accepté comme aujourd'hui */
	g_assert_cmpint(check("f", "nan", "", "", NULL, NULL), ==, GT_MACRO_ARG_OK);
	/* c et liste : jamais de limite */
	g_assert_cmpint(check("c", "x", "", "", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("l", "b", NULL, NULL, NULL, NULL), ==, GT_MACRO_ARG_OK);
	/* raison optionnelle */
	g_assert_cmpint(gt_macro_arg_check("d", "200", "0", "100", NULL, NULL), ==, GT_MACRO_ARG_ABOVE_MAX);
}

static void
test_verification_texte(void)
{
	g_assert_cmpint(check("s", "abc", "1", "8", NULL, NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("s", "", "1", "8", NULL, "0 bytes, at least 1"), ==, GT_MACRO_ARG_TOO_SHORT);
	g_assert_cmpint(check("s", "abcdefghi", "1", "8", NULL, "9 bytes, at most 8"), ==, GT_MACRO_ARG_TOO_LONG);
	g_assert_cmpint(check("s", "", "", "", NULL, NULL), ==, GT_MACRO_ARG_OK);
	/* « é » : 2 octets en UTF-8, 1 en latin1 */
	g_assert_cmpint(check("s", "\xc3\xa9", "", "1", NULL, "2 bytes, at most 1"), ==, GT_MACRO_ARG_TOO_LONG);
	g_assert_cmpint(check("s", "\xc3\xa9", "", "1", "latin1", NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("s", "\xc3\xa9", "2", "2", "", NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("s", "\xe2\x82\xac", "", "", "latin1", "character \"\xe2\x82\xac\" not allowed"), ==, GT_MACRO_ARG_BAD_CHAR);
	/* printable : 0x20-0x7E */
	g_assert_cmpint(check("s", "a b~", "", "", "printable", NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("s", "a\tb", "", "", "printable", "character \"\t\" not allowed"), ==, GT_MACRO_ARG_BAD_CHAR);
	g_assert_cmpint(check("s", "\xc3\xa9", "", "", "printable", "character \"\xc3\xa9\" not allowed"), ==, GT_MACRO_ARG_BAD_CHAR);
	g_assert_cmpint(check("s", "a\tb", "", "", "ascii", NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("s", "\xc3\xa9", "", "", "ascii", "character \"\xc3\xa9\" not allowed"), ==, GT_MACRO_ARG_BAD_CHAR);
	/* classe */
	g_assert_cmpint(check("s", "AB_9-", "", "", "class:A-Z0-9_-", NULL), ==, GT_MACRO_ARG_OK);
	g_assert_cmpint(check("s", "ABc", "", "", "class:A-Z0-9_-", "character \"c\" not allowed"), ==, GT_MACRO_ARG_BAD_CHAR);
	/* ordre : jeu de caractères avant longueur */
	g_assert_cmpint(check("s", "abc!", "", "2", "class:a-c", "character \"!\" not allowed"), ==, GT_MACRO_ARG_BAD_CHAR);
	/* UTF-8 invalide */
	g_assert_cmpint(check("s", "\xff", "", "", NULL, "not a valid text"), ==, GT_MACRO_ARG_BAD_VALUE);
}

static void
test_encodage(void)
{
	gsize len = 99;
	gchar *s = gt_macro_arg_encode("latin1", "\xc3\xa9", &len);

	g_assert_cmpuint(len, ==, 1);
	g_assert_cmpuint((guchar)s[0], ==, 0xE9);
	g_free(s);

	s = gt_macro_arg_encode("latin1", "a\xc3\xa9z", &len);
	g_assert_cmpuint(len, ==, 3);
	g_assert_cmpmem(s, len, "a\xe9z", 3);
	g_free(s);

	g_assert_null(gt_macro_arg_encode("latin1", "\xe2\x82\xac", &len));

	s = gt_macro_arg_encode(NULL, "\xc3\xa9", &len);
	g_assert_cmpuint(len, ==, 2);
	g_assert_cmpstr(s, ==, "\xc3\xa9");
	g_free(s);

	s = gt_macro_arg_encode("ascii", "abc", NULL);
	g_assert_cmpstr(s, ==, "abc");
	g_free(s);

	s = gt_macro_arg_encode("latin1", "", &len);
	g_assert_cmpuint(len, ==, 0);
	g_free(s);
}

static void
decrire(const gchar *type, const gchar *liste, const gchar *min, const gchar *max, const gchar *charset, const gchar *def,
        const gchar *attendu)
{
	gchar *d = gt_macro_arg_describe(type, liste, min, max, charset, def);

	g_assert_cmpstr(d, ==, attendu);
	g_free(d);
}

static void
test_description(void)
{
	decrire("d", NULL, "0", "100", NULL, "50", "integer, 0 \xe2\x80\x93 100 (default 50)");
	decrire("f", NULL, "", "5.0", NULL, "", "decimal, \xe2\x89\xa4 5.0");
	decrire("f", NULL, "1", "", NULL, NULL, "decimal, \xe2\x89\xa5 1");
	decrire("s", NULL, "1", "8", "printable", "", "text, 1 \xe2\x80\x93 8 bytes, printable ASCII");
	decrire("s", NULL, "", "", "", "", "text");
	decrire("s", NULL, "", "4", "ascii", "ab", "text, \xe2\x89\xa4 4 bytes, ASCII (default ab)");
	decrire("s", NULL, "2", "", "latin1", NULL, "text, \xe2\x89\xa5 2 bytes, 8 bits (Latin-1)");
	decrire("s", NULL, "", "", "class:A-Z", NULL, "text, characters A-Z");
	decrire("l", "Modes", NULL, NULL, NULL, "b", "list Modes (default b)");
	decrire("c", NULL, "", "", "", "", "character");
	decrire("x", NULL, "", "", "", "0x10", "hexadecimal (default 0x10)");
	decrire("u", NULL, "", "", "", "", "unsigned");
	decrire("o", NULL, "", "", "", "", "octal");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	setlocale(LC_ALL, "C");

	g_test_add_func("/macro_arg/charset-parse", test_charset_parse);
	g_test_add_func("/macro_arg/classes", test_classes);
	g_test_add_func("/macro_arg/classes-taille", test_classes_taille);
	g_test_add_func("/macro_arg/classes-contenu", test_classes_contenu);
	g_test_add_func("/macro_arg/limites", test_limites);
	g_test_add_func("/macro_arg/limites-couple", test_limites_couple);
	g_test_add_func("/macro_arg/verification-nombres", test_verification_nombres);
	g_test_add_func("/macro_arg/verification-texte", test_verification_texte);
	g_test_add_func("/macro_arg/encodage", test_encodage);
	g_test_add_func("/macro_arg/description", test_description);
	return g_test_run();
}
