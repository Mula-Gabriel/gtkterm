/* Tests de update_versions.c : versions (tags, git describe), ordre, liste des tags, fichier NEWS, sélection des notes dans les deux sens,
 * verdict du démarrage. */

#include <glib.h>

#include "update_versions.h"

static GtVersion
ver(const gchar *s)
{
	GtVersion out = {0, 0, 0, 0};

	g_assert_true(gt_version_parse(s, &out));
	return out;
}

static void
test_parse(void)
{
	GtVersion x;
	static const gchar *const invalides[] = {"", "v", "v1", "v1.2", "v1.2.3.4", "v01.2.3", "v1.2.03", "v1.2.3-0-gabc1234", "v1.2.3-3", "v1.2.3-3-gxyz1",
	                                         "v1.2.3-3-gab", "V1.2.3", "v1.2.3 ", " v1.2.3", "v1234567890.0.0", "v-1.2.3", "v1.2.3-dirty", "v1.2.3\n", NULL};

	x = ver("v1.3.2");
	g_assert_cmpuint(x.major, ==, 1);
	g_assert_cmpuint(x.minor, ==, 3);
	g_assert_cmpuint(x.patch, ==, 2);
	g_assert_cmpuint(x.distance, ==, 0);
	x = ver("2.0.0");
	g_assert_cmpuint(x.major, ==, 2);
	g_assert_cmpuint(x.distance, ==, 0);
	x = ver("v2.0.0-3-gabc1234");
	g_assert_cmpuint(x.patch, ==, 0);
	g_assert_cmpuint(x.distance, ==, 3);
	x = ver("v10.20.30");
	g_assert_cmpuint(x.minor, ==, 20);
	x = ver("v0.0.0");
	g_assert_cmpuint(x.major, ==, 0);
	for(const gchar *const *s = invalides; *s != NULL; s++)
	{
		GtVersion garde = {7, 7, 7, 7};

		if(gt_version_parse(*s, &garde))
			g_test_fail_printf("« %s » accepté", *s);
		g_assert_cmpuint(garde.major, ==, 7);	/* *out intact en cas de refus */
	}
	g_assert_false(gt_version_parse(NULL, &x));
}

static void
test_release_tag(void)
{
	g_assert_true(gt_version_is_release_tag("v0.0.0"));
	g_assert_true(gt_version_is_release_tag("v1.3.2"));
	g_assert_true(gt_version_is_release_tag("v999999999.0.1"));
	g_assert_false(gt_version_is_release_tag("1.3.2"));
	g_assert_false(gt_version_is_release_tag("v1.3.2-3-gabc1234"));
	g_assert_false(gt_version_is_release_tag("v1.3"));
	g_assert_false(gt_version_is_release_tag("essai"));
	g_assert_false(gt_version_is_release_tag(""));
	g_assert_false(gt_version_is_release_tag(NULL));
	g_assert_false(gt_version_is_release_tag("v1.3.2\n"));
	g_assert_false(gt_version_is_release_tag("v1.3.2;touch x"));
}

static void
plus_petite(const gchar *a, const gchar *b)
{
	GtVersion va = ver(a), vb = ver(b);

	if(!(gt_version_compare(&va, &vb) < 0 && gt_version_compare(&vb, &va) > 0))
		g_test_fail_printf("%s devrait précéder %s", a, b);
}

static void
test_compare(void)
{
	GtVersion a = ver("v2.0.0"), b = ver("2.0.0");

	plus_petite("v1.9.3", "v1.10.0");
	plus_petite("v1.3.2", "v2.0.0");
	plus_petite("v2.0.0", "v2.0.0-3-gabc1234");
	plus_petite("v2.0.0-3-gabc1234", "v2.0.1");
	plus_petite("v2.0.0-3-gabc1234", "v2.0.0-4-gdef5678");
	plus_petite("v0.9.9", "v1.0.0");
	g_assert_cmpint(gt_version_compare(&a, &b), ==, 0);
}

static void
test_installed(void)
{
	GtVersion x;

	gt_version_installed("v2.0.0-3-gabc1234", "2.0.0", &x);
	g_assert_cmpuint(x.major, ==, 2);
	g_assert_cmpuint(x.distance, ==, 3);
	gt_version_installed("unknown", "1.3.2", &x);
	g_assert_cmpuint(x.major, ==, 1);
	g_assert_cmpuint(x.minor, ==, 3);
	g_assert_cmpuint(x.patch, ==, 2);
	gt_version_installed(NULL, NULL, &x);
	g_assert_cmpuint(x.major + x.minor + x.patch + x.distance, ==, 0);
	gt_version_installed("", "pas une version", &x);
	g_assert_cmpuint(x.major + x.minor + x.patch + x.distance, ==, 0);
}

static void
test_list_from_refs(void)
{
	static const gchar *const attendu[] = {"v2.0.0", "v1.10.0", "v1.9.3", "v1.3.2"};
	GPtrArray *t = gt_version_list_from_refs("v1.3.2\nessai\nv2.0.0\nv1.10.0\r\nv1.9.3\nv2.0.0\n1.0.0\nv3.0.0-rc1\n\n");

	g_assert_cmpuint(t->len, ==, G_N_ELEMENTS(attendu));
	for(guint i = 0; i < t->len; i++)
		g_assert_cmpstr(g_ptr_array_index(t, i), ==, attendu[i]);
	g_ptr_array_unref(t);
	t = gt_version_list_from_refs(NULL);
	g_assert_cmpuint(t->len, ==, 0);
	g_ptr_array_unref(t);
	t = gt_version_list_from_refs("");
	g_assert_cmpuint(t->len, ==, 0);
	g_ptr_array_unref(t);
}

static const gchar NEWS_TEXTE[] =
	"# Nouveautés de GTKTerm\n"
	"- préambule ignoré\n"
	"\n"
	"## v2.1.0 — 2026-11-02\n"
	"- Nouveauté A\n"
	"  suite de A\n"
	"! Attention B\n"
	"\n"
	"Texte libre C\n"
	"## v2.0.0 — 2026-10-20\r\n"
	"- Onglets\r\n"
	"! Configuration convertie ;\n"
	"  remettre la copie.\n"
	"## pas-une-version\n"
	"- dans v2.0.0\n"
	"## v1.3.2\n"
	"- Gestionnaire par versions\n"
	"## v9.0.0 — à venir\n"
	"- pas encore publiée\n"
	"## v2.0.0 — doublon\n"
	"- ignorée\n";

static GtNewsEntry *
entree(GtNewsSection *s, guint i)
{
	g_assert_cmpuint(i, <, s->entries->len);
	return g_ptr_array_index(s->entries, i);
}

static void
test_news_parse(void)
{
	GPtrArray *s = gt_news_parse(NEWS_TEXTE);
	GtNewsSection *a, *b, *c;

	g_assert_cmpuint(s->len, ==, 5);
	a = g_ptr_array_index(s, 0);
	g_assert_cmpstr(a->tag, ==, "v2.1.0");
	g_assert_cmpstr(a->title, ==, "v2.1.0 — 2026-11-02");
	g_assert_cmpuint(a->entries->len, ==, 3);
	g_assert_cmpint(entree(a, 0)->kind, ==, GT_NEWS_ITEM);
	g_assert_cmpstr(entree(a, 0)->text, ==, "Nouveauté A suite de A");
	g_assert_cmpint(entree(a, 1)->kind, ==, GT_NEWS_WARNING);
	g_assert_cmpstr(entree(a, 1)->text, ==, "Attention B");
	g_assert_cmpint(entree(a, 2)->kind, ==, GT_NEWS_TEXT);
	g_assert_cmpstr(entree(a, 2)->text, ==, "Texte libre C");
	b = g_ptr_array_index(s, 1);
	g_assert_cmpstr(b->tag, ==, "v2.0.0");
	g_assert_cmpstr(b->title, ==, "v2.0.0 — 2026-10-20");	/* \r retiré */
	g_assert_cmpuint(b->entries->len, ==, 4);
	g_assert_cmpstr(entree(b, 0)->text, ==, "Onglets");
	g_assert_cmpint(entree(b, 1)->kind, ==, GT_NEWS_WARNING);
	g_assert_cmpstr(entree(b, 1)->text, ==, "Configuration convertie ; remettre la copie.");
	g_assert_cmpint(entree(b, 2)->kind, ==, GT_NEWS_TEXT);
	g_assert_cmpstr(entree(b, 2)->text, ==, "## pas-une-version");
	g_assert_cmpstr(entree(b, 3)->text, ==, "dans v2.0.0");
	c = g_ptr_array_index(s, 2);
	g_assert_cmpstr(c->title, ==, "v1.3.2");
	g_ptr_array_unref(s);
	s = gt_news_parse(NULL);
	g_assert_cmpuint(s->len, ==, 0);
	g_ptr_array_unref(s);
	s = gt_news_parse("sans section\n- rien\n");
	g_assert_cmpuint(s->len, ==, 0);
	g_ptr_array_unref(s);
}

/* Titre de niveau 1 : clôt la section en cours ; le texte qui suit n'est pas affiché. NEWS_PUBLIE reprend le début du NEWS réel (sans lire de fichier). */
static const gchar NEWS_PUBLIE[] =
	"# Nouveautés de GTKTerm\n"
	"\n"
	"Une section par version publiée (tag vX.Y.Z), de la plus récente à la plus ancienne. Format lu par la fenêtre « Mises à jour » :\n"
	"« ## vX.Y.Z — date » ouvre une section, « - » une nouveauté, « ! » un avertissement (ce qui casse ou demande une action) ;\n"
	"une ligne qui commence par deux espaces prolonge l'élément précédent. Une section sans tag publié n'est jamais affichée.\n"
	"\n"
	"## v1.3.2 — 2026-10-12\n"
	"- Mises à jour par versions publiées : Aide › Rechercher des mises à jour… liste les versions, affiche leurs nouveautés et\n"
	"  avertissements, et installe celle que vous choisissez, y compris une plus ancienne pour revenir en arrière.\n"
	"- Ports : un lien udev numéroté par appareil Metro (ttyM5_1, ttyM5_2…), liens udev affichés en gras.\n"
	"\n"
	"# Historique (versions antérieures à 1.3.2, texte d’origine)\n"
	"\n"
	"1.3.1  : Compile fix (Thanks to neOh).\n"
	"\n"
	"1.3    : Added BR translation and other updates.\n"
	"\t   Thanks to JuniorPolegato, cweickhmann.\n";

static void
test_news_titre_niveau_1(void)
{
	GPtrArray *s = gt_news_parse("## v1.0.0\n- un\n# Historique\n1.3.1 : ancien\n- pas une note\n## v0.9.0\n- neuf\n");
	GtNewsSection *a, *b;

	g_assert_cmpuint(s->len, ==, 2);
	a = g_ptr_array_index(s, 0);
	g_assert_cmpstr(a->tag, ==, "v1.0.0");
	g_assert_cmpuint(a->entries->len, ==, 1);
	g_assert_cmpstr(entree(a, 0)->text, ==, "un");
	b = g_ptr_array_index(s, 1);
	g_assert_cmpstr(b->tag, ==, "v0.9.0");
	g_assert_cmpuint(b->entries->len, ==, 1);
	g_assert_cmpstr(entree(b, 0)->text, ==, "neuf");
	g_ptr_array_unref(s);

	s = gt_news_parse(NEWS_PUBLIE);
	g_assert_cmpuint(s->len, ==, 1);
	a = g_ptr_array_index(s, 0);
	g_assert_cmpstr(a->tag, ==, "v1.3.2");
	g_assert_cmpuint(a->entries->len, ==, 2);
	g_assert_cmpint(entree(a, 0)->kind, ==, GT_NEWS_ITEM);
	g_assert_cmpstr(entree(a, 0)->text, ==, "Mises à jour par versions publiées : Aide › Rechercher des mises à jour… liste les versions, affiche leurs nouveautés et "
	                                        "avertissements, et installe celle que vous choisissez, y compris une plus ancienne pour revenir en arrière.");
	g_assert_cmpint(entree(a, 1)->kind, ==, GT_NEWS_ITEM);
	g_assert_cmpstr(entree(a, 1)->text, ==, "Ports : un lien udev numéroté par appareil Metro (ttyM5_1, ttyM5_2…), liens udev affichés en gras.");
	g_ptr_array_unref(s);
}

/* Sélection : tags attendus dans l'ordre, sens attendu */
static void
selection(GPtrArray *sections, GPtrArray *released, const gchar *installed, const gchar *target, GtNewsDirection sens, const gchar *const *tags)
{
	GtVersion vi = ver(installed), vt = ver(target);
	GtNewsDirection d = 99;
	GPtrArray *sel = gt_news_select(sections, released, &vi, &vt, &d);
	guint n = g_strv_length((gchar **)tags);

	g_assert_cmpint(d, ==, sens);
	g_assert_cmpuint(sel->len, ==, n);
	for(guint i = 0; i < n; i++)
		g_assert_cmpstr(((GtNewsSection *)g_ptr_array_index(sel, i))->tag, ==, tags[i]);
	g_ptr_array_unref(sel);
}

static void
test_news_select(void)
{
	GPtrArray *sections = gt_news_parse(NEWS_TEXTE);
	GPtrArray *released = gt_version_list_from_refs("v2.1.0\nv2.0.0\nv1.3.2\nv0.0.1\n");
	static const gchar *const montee[] = {"v2.1.0", "v2.0.0", NULL};
	static const gchar *const seule_210[] = {"v2.1.0", NULL};
	static const gchar *const seule_200[] = {"v2.0.0", NULL};
	static const gchar *const seule_132[] = {"v1.3.2", NULL};
	static const gchar *const aucune[] = {NULL};
	GtVersion vi = ver("v1.3.2"), vt = ver("v2.1.0");
	GPtrArray *sel;

	selection(sections, released, "v1.3.2", "v2.1.0", GT_NEWS_UPGRADE, montee);
	selection(sections, released, "v2.1.0", "v1.3.2", GT_NEWS_ROLLBACK, montee);
	selection(sections, released, "v2.0.0-3-gabc1234", "v2.0.0", GT_NEWS_ROLLBACK, aucune);
	selection(sections, released, "v2.0.0-3-gabc1234", "v2.1.0", GT_NEWS_UPGRADE, seule_210);
	selection(sections, released, "v2.0.0-3-gabc1234", "v1.3.2", GT_NEWS_ROLLBACK, seule_200);
	selection(sections, released, "v1.3.2", "v1.3.2", GT_NEWS_SAME, aucune);
	selection(sections, released, "v0.0.1", "v1.3.2", GT_NEWS_UPGRADE, seule_132);
	selection(sections, released, "v2.1.0", "v9.0.0", GT_NEWS_UPGRADE, aucune);	/* v9.0.0 sans tag : jamais affichée */
	/* Première section du fichier pour un tag en double */
	sel = gt_news_select(sections, released, &vi, &vt, NULL);
	g_assert_cmpstr(((GtNewsSection *)g_ptr_array_index(sel, 1))->title, ==, "v2.0.0 — 2026-10-20");
	g_assert_true(gt_news_has_warning(sel));
	g_ptr_array_unref(sel);
	vi = ver("v0.0.1");
	vt = ver("v1.3.2");
	sel = gt_news_select(sections, released, &vi, &vt, NULL);
	g_assert_false(gt_news_has_warning(sel));
	g_ptr_array_unref(sel);
	sel = gt_news_select(NULL, released, &vi, &vt, NULL);
	g_assert_cmpuint(sel->len, ==, 0);
	g_ptr_array_unref(sel);
	g_ptr_array_unref(released);
	g_ptr_array_unref(sections);
}

static void
test_verdict(void)
{
	GtVersion v132 = ver("v1.3.2"), v200 = ver("v2.0.0"), dev = ver("v2.0.0-3-gabc1234");

	g_assert_cmpint(gt_release_verdict(&v132, "v2.0.0", NULL), ==, GT_RELEASE_AVAILABLE);
	g_assert_cmpint(gt_release_verdict(&v132, "v2.0.0", ""), ==, GT_RELEASE_AVAILABLE);
	g_assert_cmpint(gt_release_verdict(&v132, "v2.0.0", "v2.0.0"), ==, GT_RELEASE_SKIPPED);
	g_assert_cmpint(gt_release_verdict(&v132, "v2.0.0", "v1.9.0"), ==, GT_RELEASE_AVAILABLE);
	g_assert_cmpint(gt_release_verdict(&v200, "v2.0.0", NULL), ==, GT_RELEASE_UP_TO_DATE);
	g_assert_cmpint(gt_release_verdict(&dev, "v2.0.0", NULL), ==, GT_RELEASE_UP_TO_DATE);
	g_assert_cmpint(gt_release_verdict(&v200, "v1.3.2", NULL), ==, GT_RELEASE_UP_TO_DATE);
	g_assert_cmpint(gt_release_verdict(&v132, NULL, NULL), ==, GT_RELEASE_UP_TO_DATE);
	g_assert_cmpint(gt_release_verdict(&v132, "essai", NULL), ==, GT_RELEASE_UP_TO_DATE);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/update-versions/parse", test_parse);
	g_test_add_func("/update-versions/release-tag", test_release_tag);
	g_test_add_func("/update-versions/compare", test_compare);
	g_test_add_func("/update-versions/installed", test_installed);
	g_test_add_func("/update-versions/list-from-refs", test_list_from_refs);
	g_test_add_func("/update-versions/news-parse", test_news_parse);
	g_test_add_func("/update-versions/news-titre-niveau-1", test_news_titre_niveau_1);
	g_test_add_func("/update-versions/news-select", test_news_select);
	g_test_add_func("/update-versions/verdict", test_verdict);
	return g_test_run();
}
