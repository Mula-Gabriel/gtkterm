# Mise à jour par versions taguées — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox
> (`- [ ]`) syntax for tracking.

**Goal :** remplacer la mise à jour « tête de `master` » par l'installation d'une version publiée (tag `vX.Y.Z`) choisie dans une fenêtre qui affiche nouveautés et avertissements (`NEWS`),
retour en arrière compris ; publier `v1.3.2` (ligne GTK 3) puis `v2.0.0` (refonte GTK 4).

**Architecture :** deux modules GLib partagés, identiques sur les deux lignes — `update_versions.c` (versions, tags, `NEWS`, sélection des notes, verdict ; pur) et `update_fetch.c` (dépôt nu
du cache, `git clone/fetch/for-each-ref/show` asynchrones) — plus le script `data/gtkterm-update.sh` (installe un tag, se relance sur le script de la version visée, ménage des fichiers par
registre). L'interface est écrite deux fois : `src/update.c` (GTK 3) puis `src/ui/update_window.c` (GTK 4). La ligne GTK 3 est faite d'abord, taguée `v1.3.2`, puis **fusionnée** dans la refonte.

**Tech Stack :** C (GLib/GIO, GTK 3 puis GTK 4), Meson/Ninja, sh POSIX, git, GTest, suite graphique Python (broadway) côté refonte.

**Spec :** `docs/superpowers/specs/2026-10-12-maj-par-tags-design.md`

## Global Constraints

- Deux espaces de travail : **ligne GTK 3** = `~/git/gtkterm-gtk3` (branche `feature/maj-par-tags` partie de `origin/master` = `0a013c2`) ; **ligne refonte** = `~/git/gtkterm` (branche
  `feature/maj-par-tags` partie de `master`). Ne jamais déplacer l'une pour travailler sur l'autre.
- Version publiée = tag qui correspond exactement à `^v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$`. Ordre numérique, jamais la date.
- `git describe --tags --match 'v[0-9]*' --abbrev=7` → `GIT_DESCRIBE` ; à défaut, `VERSION` (`meson.project_version()`), à défaut `0.0.0`.
- `NEWS` en français, à la racine : `## vX.Y.Z …` en-tête de section, `- ` nouveauté, `! ` avertissement, deux espaces en tête = prolongement (joint par une espace), ligne vide ignorée.
- Cache des versions : `$XDG_CACHE_HOME/gtkterm/versions.git` (dépôt nu, `--filter=tree:0`) ; copie de travail du script : `$XDG_CACHE_HOME/gtkterm/src` ; registre :
  `$XDG_CACHE_HOME/gtkterm/installed-files.txt`.
- Toujours `git -c protocol.ext.allow=never`, `GIT_TERMINAL_PROMPT=0`, `LC_ALL=C` ; jamais une valeur (URL, préfixe, tag) interprétée par un shell ; une URL vide ou qui commence par
  `-` est refusée.
- Clé « Skip this version » : `update_skipped_version` (nom de tag). L'ancienne `update_skipped_revision` n'est plus lue ni écrite.
- Script : `gtkterm-update.sh <url> <prefix> <pid> <tag>` ; **une seule autorisation (`pkexec`) par mise à jour** ; `GTKTERM_UPDATE_SUDO` (défaut `pkexec`) et `GTKTERM_UPDATE_DEPS=0` réservés aux tests.
- **Aucune demande de mot de passe pendant le développement** : jamais de `pkexec`/`sudo` lancé par un agent ; un `gtkterm-update.sh` ne s'exécute qu'avec `GTKTERM_UPDATE_SUDO` vers un programme de test, `GTKTERM_UPDATE_DEPS=0`, préfixe et cache temporaires ; jamais l'ancien script.
- Libellés d'interface : msgid anglais par `_()`, traduction dans `po/fr.po`. Le texte de `NEWS` n'est ni traduit ni interprété comme du balisage.
- Style : fichiers nouveaux et modifiés en tabulations, accolades Allman, `if(` sans espace ; lignes jusqu'à 200 colonnes ; commentaires et messages de commit en français.
- Refonte : règles de `CLAUDE.md` (aucun `GtkDialog`, rien au-delà de GTK 4.14, `src/ui/` seul à inclure GTK, widgets testés nommés).
- Commits : messages en français, terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. **Aucune poussée** (`git push`) sans feu vert de l'utilisateur.

## Review Focus

1. **Source injoignable ou réponse vide au démarrage** (réseau coupé, URL fausse, dépôt sans tag) → aucune fenêtre, aucun message ; dans la fenêtre, message clair et « Check now »
   réessaie. Épinglé : `test_update_fetch` (erreurs, dépôt sans tag), scénario graphique 7.
2. **Cache des versions dans un état inattendu** (dossier présent mais vide, ou dossier non vide qui n'est pas un dépôt, URL changée entre deux actualisations) → clone dans un dossier
   vide, erreur lisible sinon, tags de la nouvelle source seulement après changement d'URL. Épinglé : `test_update_fetch` (cas « dossier vide », « changement d'URL », « dossier occupé »).
3. **`NEWS` mal formé** (fins de ligne CRLF, en-tête non-version, section en double, section d'une version pas encore taguée, `NEWS` absent du tag le plus récent) → jamais d'échec, sections
   ignorées ou texte simple. Épinglé : `test_update_versions` (parse/sélection), `test_update_fetch` (sans `NEWS`).
4. **Mise à jour interrompue puis relancée** (terminal fermé pendant la construction : `build/` supprimé, registre présent) → la relance reconstruit et fait le ménage d'après le registre.
   Épinglé : `tests/test_update_script.sh`, cas 6.
5. **Registre altéré** (chemin hors du préfixe, chemin avec `..`, dossier) → jamais supprimé. Épinglé : `tests/test_update_script.sh`, cas 5.

---

## Partie A — ligne GTK 3 (`~/git/gtkterm-gtk3`, branche `feature/maj-par-tags`)

Pas de suite de tests meson sur cette ligne : les tests C des modules partagés y sont **écrits dans `tests/`** (ils serviront tels quels dans la refonte) et compilés à la main ; le test
du script est un script shell lancé à la main.

### Task A1 : module `update_versions` (versions, tags, NEWS, sélection, verdict)

**Files :**
- Create : `src/update_versions.h`, `src/update_versions.c`
- Test : `tests/test_update_versions.c`

**Interfaces :**
- Produces : `GtVersion`, `gt_version_parse()`, `gt_version_is_release_tag()`, `gt_version_compare()`, `gt_version_installed()`, `gt_version_list_from_refs()`, `GtNewsKind`, `GtNewsEntry`,
  `GtNewsSection`, `gt_news_parse()`, `GtNewsDirection`, `gt_news_select()`, `gt_news_has_warning()`, `GtReleaseVerdict`, `gt_release_verdict()` (signatures dans l'en-tête ci-dessous).

- [ ] **Step 1 : écrire l'en-tête `src/update_versions.h`**

```c
/* Versions publiées (tags vX.Y.Z), version installée (git describe) et notes de version (fichier NEWS) : logique pure, GLib seulement, sans
 * sous-processus ni globale. Partagé tel quel par la ligne GTK 3 et la refonte (spec docs/superpowers/specs/2026-10-12-maj-par-tags-design.md). */

#ifndef UPDATE_VERSIONS_H_
#define UPDATE_VERSIONS_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
	guint major;
	guint minor;
	guint patch;
	guint distance;	/* commits après le tag (git describe « vX.Y.Z-N-g<hex> ») ; 0 pour une version publiée */
} GtVersion;

/* « vX.Y.Z », « X.Y.Z » ou « vX.Y.Z-N-g<hex> » (N ≥ 1, 4 à 64 chiffres hexadécimaux) ; nombres décimaux de 1 à 9 chiffres sans zéro de tête
 * superflu. FALSE (et *out intact) sinon. */
gboolean gt_version_parse(const gchar *str, GtVersion *out);
/* TRUE seulement pour un nom de tag de version publiée : exactement « vX.Y.Z » */
gboolean gt_version_is_release_tag(const gchar *str);
/* < 0, 0, > 0 : major, minor, patch, puis distance */
gint gt_version_compare(const GtVersion *a, const GtVersion *b);
/* Version installée : describe (GIT_DESCRIBE) s'il est lisible, sinon project_version (VERSION de meson), sinon 0.0.0 */
void gt_version_installed(const gchar *describe, const gchar *project_version, GtVersion *out);
/* Sortie de « git for-each-ref --format=%(refname:strip=2) refs/tags » → tags de versions publiées (gchar *, libérés par le tableau), sans
 * doublon, du plus récent au plus ancien. Jamais NULL. */
GPtrArray *gt_version_list_from_refs(const gchar *text);

typedef enum
{
	GT_NEWS_ITEM,	/* « - » : nouveauté */
	GT_NEWS_WARNING,	/* « ! » : avertissement */
	GT_NEWS_TEXT	/* toute autre ligne, gardée telle quelle */
} GtNewsKind;

typedef struct
{
	GtNewsKind kind;
	gchar *text;	/* sans le marqueur ; lignes de prolongement jointes par une espace */
} GtNewsEntry;

typedef struct
{
	gchar *tag;	/* « vX.Y.Z » */
	GtVersion version;
	gchar *title;	/* en-tête sans « ## » : « v2.0.0 — 2026-10-20 » */
	GPtrArray *entries;	/* GtNewsEntry * */
} GtNewsSection;

/* Sections de NEWS dans l'ordre du fichier (GtNewsSection *, libérées par le tableau) ; ce qui précède la première section est ignoré. Jamais NULL. */
GPtrArray *gt_news_parse(const gchar *text);

typedef enum
{
	GT_NEWS_SAME,	/* version visée = version installée */
	GT_NEWS_UPGRADE,	/* plus récente */
	GT_NEWS_ROLLBACK	/* plus ancienne : les sections sont celles des versions quittées */
} GtNewsDirection;

/* Sections à montrer pour passer de installed à target : montée, versions v avec installed < v ≤ target ; retour, target < v ≤ installed.
 * Seules celles dont le tag figure dans released (tags publiés), une seule par tag (la première du fichier), de la plus récente à la plus
 * ancienne. Pointeurs empruntés à sections (le tableau rendu ne libère rien). *direction (si non NULL) reçoit le sens. */
GPtrArray *gt_news_select(GPtrArray *sections, GPtrArray *released, const GtVersion *installed, const GtVersion *target, GtNewsDirection *direction);
/* TRUE si une des sections contient un avertissement */
gboolean gt_news_has_warning(GPtrArray *selected);

typedef enum
{
	GT_RELEASE_UP_TO_DATE,	/* aucune version publiée plus récente que l'installée */
	GT_RELEASE_AVAILABLE,	/* newest plus récente et non ignorée */
	GT_RELEASE_SKIPPED	/* newest plus récente mais ignorée (« Skip this version ») */
} GtReleaseVerdict;

/* Démarrage : newest = tag le plus récent (NULL ou pas une version : UP_TO_DATE) ; skipped = tag ignoré (NULL ou "" : aucun). */
GtReleaseVerdict gt_release_verdict(const GtVersion *installed, const gchar *newest, const gchar *skipped);

G_END_DECLS

#endif /* UPDATE_VERSIONS_H_ */
```

- [ ] **Step 2 : écrire le test `tests/test_update_versions.c`**

```c
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
	g_test_add_func("/update-versions/news-select", test_news_select);
	g_test_add_func("/update-versions/verdict", test_verdict);
	return g_test_run();
}
```

- [ ] **Step 3 : vérifier que le test échoue (le module n'existe pas)**

Run : `cd ~/git/gtkterm-gtk3 && mkdir -p build-tests && cc -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined -Isrc -o build-tests/test_update_versions tests/test_update_versions.c
src/update_versions.c $(pkg-config --cflags --libs glib-2.0)`
Expected : échec de compilation (`src/update_versions.c: No such file or directory`).
`build-tests/` est ignoré par git ? Non (`.gitignore` ne contient que `build`) : ajouter la ligne `build-tests` à `.gitignore` dans ce même step.

- [ ] **Step 4 : écrire `src/update_versions.c`**

```c
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
```

Suite du fichier :

```c
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
```

Note : `g_ptr_array_find_with_equal_func(out, s->tag, section_has_tag, NULL)` passe chaque élément de `out` en premier argument et `s->tag` en second (GLib compare `equal_func(element, needle)`).
`g_ptr_array_find_with_equal_func` existe depuis GLib 2.54 : compatible avec les deux lignes.

- [ ] **Step 5 : compiler et lancer le test**

Run : la commande du Step 3, puis `./build-tests/test_update_versions`
Expected : tous les cas `ok`, aucune sortie d'AddressSanitizer/UBSan.

- [ ] **Step 6 : commit**

```bash
cd ~/git/gtkterm-gtk3
git add .gitignore src/update_versions.h src/update_versions.c tests/test_update_versions.c
git commit -m "mise à jour : module update_versions (versions taguées, git describe, NEWS, sélection des notes, verdict)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task A2 : module `update_fetch` (cache des versions, récupération asynchrone)

**Files :**
- Create : `src/update_fetch.h`, `src/update_fetch.c`
- Test : `tests/test_update_fetch.c`

**Interfaces :**
- Consumes : `gt_version_list_from_refs()` (Task A1).
- Produces :
  - `typedef struct { GPtrArray *tags; gchar *news; } GtReleases;` (`tags` : `gchar *`, du plus récent au plus ancien, jamais NULL dans un résultat ; `news` : NEWS du tag le plus récent ou NULL)
  - `void gt_releases_free(GtReleases *r);` (NULL accepté)
  - `gchar *gt_releases_cache_dir(void);` (`$XDG_CACHE_HOME/gtkterm/versions.git`, `g_free`)
  - `void gt_releases_fetch_async(const gchar *url, const gchar *cache_dir, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data);`
  - `GtReleases *gt_releases_fetch_finish(GAsyncResult *res, GError **error);` (erreurs : `G_IO_ERROR_INVALID_ARGUMENT` URL vide ou commençant par `-` ; `G_IO_ERROR_FAILED` message
    de git ; `G_IO_ERROR_CANCELLED`)

- [ ] **Step 1 : écrire l'en-tête `src/update_fetch.h`**

```c
/* Versions publiées et notes de version d'une source de mise à jour (sous-projet « mise à jour par versions taguées ») : dépôt nu du cache
 * ($XDG_CACHE_HOME/gtkterm/versions.git), cloné avec --filter=tree:0 (historique et tags, sans fichiers) puis mis à jour par fetch des tags ;
 * liste des tags vX.Y.Z ; NEWS du tag le plus récent. GLib/GIO seulement, git en sous-processus (jamais de shell). Partagé par les deux lignes. */

#ifndef UPDATE_FETCH_H_
#define UPDATE_FETCH_H_

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct
{
	GPtrArray *tags;	/* gchar * : versions publiées, de la plus récente à la plus ancienne ; jamais NULL dans un résultat */
	gchar *news;	/* texte de NEWS du tag le plus récent ; NULL si absent ou illisible (pas une erreur) */
} GtReleases;

void gt_releases_free(GtReleases *r);
/* $XDG_CACHE_HOME/gtkterm/versions.git (g_free) */
gchar *gt_releases_cache_dir(void);
/* cache_dir sans HEAD : git clone --bare --filter=tree:0 -- <url> <cache_dir> (dossiers parents créés) ; sinon remote set-url origin -- <url> ;
 * puis fetch --prune --force origin +refs/tags/*:refs/tags/* ; for-each-ref refs/tags ; show refs/tags/<plus récent>:NEWS.
 * git -c protocol.ext.allow=never, GIT_TERMINAL_PROMPT=0, LC_ALL=C. Le rappel est toujours appelé, jamais pendant cet appel ; annulation :
 * le git en cours est tué. */
void gt_releases_fetch_async(const gchar *url, const gchar *cache_dir, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data);
/* Résultat (gt_releases_free) ou NULL + erreur : G_IO_ERROR_INVALID_ARGUMENT (URL vide ou commençant par « - »), G_IO_ERROR_FAILED (première
 * ligne du message de git), G_IO_ERROR_CANCELLED. */
GtReleases *gt_releases_fetch_finish(GAsyncResult *res, GError **error);

G_END_DECLS

#endif /* UPDATE_FETCH_H_ */
```

- [ ] **Step 2 : écrire le test `tests/test_update_fetch.c`**

```c
/* Tests de update_fetch.c : dépôt amont local (file://) avec tags annotés et NEWS ; premier clone filtré, actualisation (tag ajouté, tag
 * supprimé), changement d'URL, NEWS absent, dépôt sans tag, dossier de cache vide ou occupé, erreurs d'URL, annulation. */

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>

#include "update_fetch.h"

/* Supprime un arbre de fichiers */
static void
supprimer_arbre(const gchar *path)
{
	GDir *d = NULL;

	if(!g_file_test(path, G_FILE_TEST_IS_SYMLINK) && g_file_test(path, G_FILE_TEST_IS_DIR))
		d = g_dir_open(path, 0, NULL);
	if(d != NULL)
	{
		const gchar *name;

		while((name = g_dir_read_name(d)) != NULL)
		{
			gchar *child = g_build_filename(path, name, NULL);

			supprimer_arbre(child);
			g_free(child);
		}
		g_dir_close(d);
		g_rmdir(path);
	}
	else
		g_unlink(path);
}

/* git -C dir <arguments…> ; échec du test si git échoue */
static void git(const gchar *dir, ...) G_GNUC_NULL_TERMINATED;

static void
git(const gchar *dir, ...)
{
	GPtrArray *argv = g_ptr_array_new();
	gint status = 0;
	GError *error = NULL;
	const gchar *arg;
	va_list ap;

	g_ptr_array_add(argv, (gpointer)"git");
	g_ptr_array_add(argv, (gpointer)"-C");
	g_ptr_array_add(argv, (gpointer)dir);
	va_start(ap, dir);
	while((arg = va_arg(ap, const gchar *)) != NULL)
		g_ptr_array_add(argv, (gpointer)arg);
	va_end(ap);
	g_ptr_array_add(argv, NULL);
	g_assert_true(g_spawn_sync(NULL, (gchar **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
	                           NULL, NULL, &status, &error));
	g_assert_no_error(error);
	g_assert_true(g_spawn_check_wait_status(status, NULL));
	g_ptr_array_free(argv, TRUE);
}

/* Commit (NEWS = news, ou NEWS supprimé si news == NULL) puis tag annoté (tag != NULL) */
static void
version(const gchar *dir, const gchar *news, const gchar *tag)
{
	gchar *f = g_build_filename(dir, "NEWS", NULL);
	gchar *autre = g_build_filename(dir, "fichier.txt", NULL);
	static guint n;
	gchar *contenu = g_strdup_printf("%u\n", ++n);

	if(news != NULL)
		g_assert_true(g_file_set_contents(f, news, -1, NULL));
	else
		g_unlink(f);
	g_assert_true(g_file_set_contents(autre, contenu, -1, NULL));
	git(dir, "add", "-A", NULL);
	git(dir, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false", "commit", "-q", "-m", "version", NULL);
	if(tag != NULL)
		git(dir, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "tag.gpgsign=false", "tag", "-a", tag, "-m", tag, NULL);
	g_free(contenu);
	g_free(autre);
	g_free(f);
}

static gchar *
depot_nouveau(const gchar *racine, const gchar *nom)
{
	gchar *dir = g_build_filename(racine, nom, NULL);

	g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
	git(dir, "init", "-q", NULL);
	return dir;
}

typedef struct
{
	GMainLoop *loop;
	GtReleases *r;
	GError *error;
	gboolean appele;
} Attente;

static void
on_fetch(GObject *source, GAsyncResult *res, gpointer user_data)
{
	Attente *a = user_data;

	a->r = gt_releases_fetch_finish(res, &a->error);
	a->appele = TRUE;
	g_main_loop_quit(a->loop);
}

static GtReleases *
recuperer(const gchar *url, const gchar *cache, gboolean annuler, GError **error)
{
	Attente a = {g_main_loop_new(NULL, FALSE), NULL, NULL, FALSE};
	GCancellable *c = g_cancellable_new();

	gt_releases_fetch_async(url, cache, c, on_fetch, &a);
	g_assert_false(a.appele);	/* jamais de rappel pendant l'appel _async */
	if(annuler)
		g_cancellable_cancel(c);
	g_main_loop_run(a.loop);
	g_main_loop_unref(a.loop);
	g_object_unref(c);
	if(a.error != NULL)
		g_propagate_error(error, a.error);
	return a.r;
}

static void
tags_attendus(GtReleases *r, const gchar *const *tags)
{
	guint n = g_strv_length((gchar **)tags);

	g_assert_nonnull(r);
	g_assert_cmpuint(r->tags->len, ==, n);
	for(guint i = 0; i < n; i++)
		g_assert_cmpstr(g_ptr_array_index(r->tags, i), ==, tags[i]);
}

static gboolean
git_present(void)
{
	gchar *prog = g_find_program_in_path("git");

	g_free(prog);
	return prog != NULL;
}

static void
test_actualisation(void)
{
	gchar *racine, *amont, *amont2, *cache, *head, *url, *url2;
	GtReleases *r;
	GError *error = NULL;
	static const gchar *const t1[] = {"v1.1.0", "v1.0.0", NULL};
	static const gchar *const t2[] = {"v1.2.0", "v1.1.0", NULL};
	static const gchar *const t3[] = {"v5.0.0", NULL};

	if(!git_present())
	{
		g_test_skip("git absent");
		return;
	}
	racine = g_dir_make_tmp("gtkterm-fetch-XXXXXX", NULL);
	amont = depot_nouveau(racine, "amont");
	version(amont, "## v1.0.0\n- un\n", "v1.0.0");
	version(amont, "## v1.1.0\n- deux\n## v1.0.0\n- un\n", "v1.1.0");
	git(amont, "tag", "essai", NULL);	/* tag léger qui n'est pas une version */
	cache = g_build_filename(racine, "cache", "gtkterm", "versions.git", NULL);	/* parents absents */
	url = g_strconcat("file://", amont, NULL);

	/* 1. Premier passage : clone filtré, tags de versions seulement, NEWS du plus récent */
	r = recuperer(url, cache, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, t1);
	g_assert_cmpstr(r->news, ==, "## v1.1.0\n- deux\n## v1.0.0\n- un\n");
	gt_releases_free(r);
	head = g_build_filename(cache, "HEAD", NULL);
	g_assert_true(g_file_test(head, G_FILE_TEST_EXISTS));

	/* 2. Actualisation : tag ajouté, tag supprimé en amont */
	version(amont, "## v1.2.0\n! attention\n", "v1.2.0");
	git(amont, "tag", "-d", "v1.0.0", NULL);
	r = recuperer(url, cache, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, t2);
	g_assert_cmpstr(r->news, ==, "## v1.2.0\n! attention\n");
	gt_releases_free(r);

	/* 3. Changement d'URL : seuls les tags de la nouvelle source restent */
	amont2 = depot_nouveau(racine, "amont2");
	version(amont2, "## v5.0.0\n- autre\n", "v5.0.0");
	url2 = g_strconcat("file://", amont2, NULL);
	r = recuperer(url2, cache, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, t3);
	gt_releases_free(r);

	/* 4. Tag le plus récent sans NEWS : pas une erreur */
	version(amont2, NULL, "v6.0.0");
	r = recuperer(url2, cache, FALSE, &error);
	g_assert_no_error(error);
	g_assert_cmpuint(r->tags->len, ==, 2);
	g_assert_null(r->news);
	gt_releases_free(r);

	g_free(url2);
	g_free(amont2);
	g_free(head);
	g_free(url);
	g_free(cache);
	g_free(amont);
	supprimer_arbre(racine);
	g_free(racine);
}

static void
test_cas_limites(void)
{
	gchar *racine, *amont, *vide, *occupe, *bidon, *url, *pirate, *opt;
	GtReleases *r;
	GError *error = NULL;
	static const gchar *const aucun[] = {NULL};
	static const gchar *const un[] = {"v1.0.0", NULL};

	if(!git_present())
	{
		g_test_skip("git absent");
		return;
	}
	racine = g_dir_make_tmp("gtkterm-fetch-XXXXXX", NULL);
	amont = depot_nouveau(racine, "amont");
	version(amont, "## v1.0.0\n- un\n", NULL);	/* aucun tag */
	url = g_strconcat("file://", amont, NULL);

	/* Dépôt sans tag : liste vide, pas de NEWS, pas d'erreur */
	vide = g_build_filename(racine, "cache-vide", NULL);
	g_assert_cmpint(g_mkdir_with_parents(vide, 0700), ==, 0);	/* dossier présent mais vide : le clone y va */
	r = recuperer(url, vide, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, aucun);
	g_assert_null(r->news);
	gt_releases_free(r);
	git(amont, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "tag.gpgsign=false", "tag", "-a", "v1.0.0", "-m", "v1.0.0", NULL);
	r = recuperer(url, vide, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, un);
	gt_releases_free(r);

	/* Dossier de cache occupé par autre chose qu'un dépôt : erreur lisible, rien d'écrasé */
	occupe = g_build_filename(racine, "cache-occupe", NULL);
	bidon = g_build_filename(occupe, "a-garder.txt", NULL);
	g_assert_cmpint(g_mkdir_with_parents(occupe, 0700), ==, 0);
	g_assert_true(g_file_set_contents(bidon, "x", -1, NULL));
	r = recuperer(url, occupe, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_cmpuint(strlen(error->message), >, 0);
	g_clear_error(&error);
	g_assert_true(g_file_test(bidon, G_FILE_TEST_EXISTS));

	/* Source injoignable */
	r = recuperer("file:///nonexistent/gtkterm-aucun-depot.git", vide, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_clear_error(&error);

	/* URL vide, ou qui ressemble à une option : refusées avant git, aucune commande lancée */
	r = recuperer("", vide, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
	pirate = g_build_filename(racine, "pirate", NULL);
	opt = g_strdup_printf("--upload-pack=touch %s", pirate);
	r = recuperer(opt, vide, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
	g_assert_false(g_file_test(pirate, G_FILE_TEST_EXISTS));

	/* Annulation aussitôt après le lancement */
	r = recuperer(url, vide, TRUE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error(&error);

	g_free(opt);
	g_free(pirate);
	g_free(bidon);
	g_free(occupe);
	g_free(vide);
	g_free(url);
	g_free(amont);
	supprimer_arbre(racine);
	g_free(racine);
}

static void
test_cache_dir(void)
{
	gchar *d = gt_releases_cache_dir();

	g_assert_true(g_str_has_suffix(d, "/gtkterm/versions.git"));
	g_free(d);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	/* git des tests : ni la configuration globale ni celle du système (signature, crochets, branche par défaut) */
	g_setenv("GIT_CONFIG_GLOBAL", "/dev/null", TRUE);
	g_setenv("GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_test_add_func("/update-fetch/actualisation", test_actualisation);
	g_test_add_func("/update-fetch/cas-limites", test_cas_limites);
	g_test_add_func("/update-fetch/cache-dir", test_cache_dir);
	return g_test_run();
}
```

- [ ] **Step 3 : vérifier que le test échoue**

Run : `cd ~/git/gtkterm-gtk3 && cc -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined -Isrc -o build-tests/test_update_fetch tests/test_update_fetch.c src/update_fetch.c
src/update_versions.c $(pkg-config --cflags --libs gio-2.0)`
Expected : échec (`src/update_fetch.c: No such file or directory`).

- [ ] **Step 4 : écrire `src/update_fetch.c`**

```c
/* Versions publiées et notes de version d'une source de mise à jour : voir update_fetch.h. */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "update_fetch.h"
#include "update_versions.h"

typedef enum
{
	STEP_CLONE,	/* cache sans HEAD : clone nu filtré */
	STEP_SET_URL,	/* cache présent : l'URL a pu changer */
	STEP_FETCH,	/* tags de la source, tags disparus supprimés */
	STEP_LIST,	/* for-each-ref refs/tags */
	STEP_NEWS	/* show <tag le plus récent>:NEWS */
} Step;

typedef struct
{
	gchar *url;
	gchar *cache_dir;
	Step step;
	GtReleases *releases;	/* résultat en construction */
} FetchState;

void
gt_releases_free(GtReleases *r)
{
	if(r == NULL)
		return;
	if(r->tags != NULL)
		g_ptr_array_unref(r->tags);
	g_free(r->news);
	g_free(r);
}

static void
fetch_state_free(gpointer p)
{
	FetchState *st = p;

	g_free(st->url);
	g_free(st->cache_dir);
	gt_releases_free(st->releases);
	g_free(st);
}

gchar *
gt_releases_cache_dir(void)
{
	return g_build_filename(g_get_user_cache_dir(), "gtkterm", "versions.git", NULL);
}

static gchar *
first_line(const gchar *text)
{
	gchar **lines;
	gchar *line = NULL;

	if(text == NULL)
		return NULL;
	lines = g_strsplit(text, "\n", -1);
	for(gchar **l = lines; *l != NULL && line == NULL; l++)
	{
		g_strstrip(*l);
		if(**l != '\0')
			line = g_strdup(*l);
	}
	g_strfreev(lines);
	return line;
}

static void on_step_done(GObject *source, GAsyncResult *res, gpointer user_data);

static void
argv_add(GPtrArray *argv, const gchar *arg)
{
	g_ptr_array_add(argv, g_strdup(arg));
}

/* Lance l'étape courante ; la référence de la tâche passe au rappel */
static void
run_step(GTask *task)
{
	FetchState *st = g_task_get_task_data(task);
	GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
	GSubprocessLauncher *launcher;
	GSubprocess *proc;
	GError *error = NULL;

	argv_add(argv, "git");
	/* protocol.ext.allow=never : une URL « ext:: » ne lance aucune commande */
	argv_add(argv, "-c");
	argv_add(argv, "protocol.ext.allow=never");
	if(st->step != STEP_CLONE)
	{
		argv_add(argv, "-C");
		argv_add(argv, st->cache_dir);
	}
	switch(st->step)
	{
		case STEP_CLONE:
			/* « -- » : l'URL n'est jamais prise pour une option ; tree:0 : historique et tags, sans arbres ni fichiers */
			argv_add(argv, "clone");
			argv_add(argv, "--bare");
			argv_add(argv, "--quiet");
			argv_add(argv, "--filter=tree:0");
			argv_add(argv, "--");
			argv_add(argv, st->url);
			argv_add(argv, st->cache_dir);
			break;
		case STEP_SET_URL:
			argv_add(argv, "remote");
			argv_add(argv, "set-url");
			argv_add(argv, "origin");
			argv_add(argv, "--");
			argv_add(argv, st->url);
			break;
		case STEP_FETCH:
			argv_add(argv, "fetch");
			argv_add(argv, "--quiet");
			argv_add(argv, "--prune");
			argv_add(argv, "--force");
			argv_add(argv, "origin");
			argv_add(argv, "+refs/tags/*:refs/tags/*");
			break;
		case STEP_LIST:
			argv_add(argv, "for-each-ref");
			argv_add(argv, "--format=%(refname:strip=2)");
			argv_add(argv, "refs/tags");
			break;
		case STEP_NEWS:
			/* Tag validé par gt_version_list_from_refs : « vX.Y.Z » seulement */
			argv_add(argv, "show");
			g_ptr_array_add(argv, g_strdup_printf("refs/tags/%s:NEWS", (const gchar *)g_ptr_array_index(st->releases->tags, 0)));
			break;
	}
	g_ptr_array_add(argv, NULL);
	/* GIT_TERMINAL_PROMPT=0 : un dépôt qui demande une authentification échoue au lieu d'attendre une saisie invisible ; LC_ALL=C : message
	 * d'erreur en ASCII (communicate_utf8) */
	launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_setenv(launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);
	g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
	proc = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv->pdata, &error);
	g_object_unref(launcher);
	g_ptr_array_unref(argv);
	if(proc == NULL)
	{
		g_task_return_error(task, error);
		g_object_unref(task);
		return;
	}
	/* Entrée fermée aussitôt (stdin_buf NULL) : git ne lit jamais l'entrée de GTKTerm */
	g_subprocess_communicate_utf8_async(proc, NULL, g_task_get_cancellable(task), on_step_done, task);
	g_object_unref(proc);
}

static void
return_releases(GTask *task, FetchState *st)
{
	GtReleases *r = st->releases;

	st->releases = NULL;
	g_task_return_pointer(task, r, (GDestroyNotify)gt_releases_free);
}

static void
on_step_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	FetchState *st = g_task_get_task_data(task);
	GSubprocess *proc = G_SUBPROCESS(source);
	gchar *out = NULL;
	gchar *err_text = NULL;
	GError *error = NULL;
	gboolean ok = g_subprocess_communicate_utf8_finish(proc, res, &out, &err_text, &error);

	/* Annulation (fenêtre fermée) : git peut encore attendre le réseau, il ne survit pas à l'actualisation */
	if(!ok)
		g_subprocess_force_exit(proc);
	if(g_task_return_error_if_cancelled(task))
		g_clear_error(&error);
	else if(st->step == STEP_NEWS)
	{
		/* NEWS absent ou illisible (pas en UTF-8) : pas une erreur, zone de notes vide */
		if(ok && g_subprocess_get_successful(proc))
		{
			st->releases->news = out;
			out = NULL;
		}
		g_clear_error(&error);
		return_releases(task, st);
	}
	else if(!ok)
		g_task_return_error(task, error);
	else if(!g_subprocess_get_successful(proc))
	{
		gchar *line = first_line(err_text);

		if(line != NULL)
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", line);
		else
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, _("git failed (exit status %d)"), g_subprocess_get_exit_status(proc));
		g_free(line);
	}
	else if(st->step == STEP_LIST)
	{
		st->releases->tags = gt_version_list_from_refs(out);
		if(st->releases->tags->len == 0)
			return_releases(task, st);
		else
		{
			st->step = STEP_NEWS;
			g_free(out);
			g_free(err_text);
			run_step(task);
			return;
		}
	}
	else
	{
		/* CLONE → FETCH (le fetch explicite aligne les tags dans tous les cas), SET_URL → FETCH, FETCH → LIST */
		st->step = st->step == STEP_FETCH ? STEP_LIST : STEP_FETCH;
		g_free(out);
		g_free(err_text);
		run_step(task);
		return;
	}
	g_free(out);
	g_free(err_text);
	g_object_unref(task);
}

void
gt_releases_fetch_async(const gchar *url, const gchar *cache_dir, GCancellable *cancellable, GAsyncReadyCallback cb, gpointer user_data)
{
	GTask *task = g_task_new(NULL, cancellable, cb, user_data);
	FetchState *st;
	gchar *head;

	g_task_set_source_tag(task, gt_releases_fetch_async);
	if(url == NULL || *url == '\0' || cache_dir == NULL)
	{
		/* GTask diffère le rappel à l'itération suivante : jamais d'appel pendant cette fonction */
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("No update source URL."));
		g_object_unref(task);
		return;
	}
	if(*url == '-')
	{
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Invalid update source URL: %s"), url);
		g_object_unref(task);
		return;
	}
	st = g_new0(FetchState, 1);
	st->url = g_strdup(url);
	st->cache_dir = g_strdup(cache_dir);
	st->releases = g_new0(GtReleases, 1);
	g_task_set_task_data(task, st, fetch_state_free);
	head = g_build_filename(cache_dir, "HEAD", NULL);
	st->step = g_file_test(head, G_FILE_TEST_EXISTS) ? STEP_SET_URL : STEP_CLONE;
	g_free(head);
	if(st->step == STEP_CLONE)
	{
		gchar *parent = g_path_get_dirname(cache_dir);

		g_mkdir_with_parents(parent, 0700);
		g_free(parent);
	}
	run_step(task);
}

GtReleases *
gt_releases_fetch_finish(GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(g_task_is_valid(res, NULL), NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}
```

- [ ] **Step 5 : compiler et lancer le test**

Run : la commande du Step 3, puis `./build-tests/test_update_fetch`
Expected : tous les cas `ok`, aucune sortie d'AddressSanitizer/UBSan. Si le cas « changement d'URL » échoue parce que `remote set-url origin -- <url>` n'est pas accepté par la version de
git du poste, remplacer l'étape par `git -C <cache> config remote.origin.url <url>` (argument séparé, jamais interprété ; l'URL ne peut pas commencer par `-`, refusée plus haut) et relancer.

- [ ] **Step 6 : commit**

```bash
cd ~/git/gtkterm-gtk3
git add src/update_fetch.h src/update_fetch.c tests/test_update_fetch.c
git commit -m "mise à jour : module update_fetch (dépôt nu du cache, tags et NEWS de la source, asynchrone et annulable)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task A3 : script d'installation par tag (`data/gtkterm-update.sh`) et son test

**Files :**
- Modify (réécriture) : `data/gtkterm-update.sh`
- Test : `tests/test_update_script.sh`

**Interfaces :**
- Produces : `gtkterm-update.sh <url> <prefix> <pid> <tag>` ; variables `GTKTERM_UPDATE_SUDO` (défaut `pkexec`), `GTKTERM_UPDATE_DEPS` (`0` : dépendances sautées), `GTKTERM_UPDATE_REEXEC`
  (interne) ; registre `$XDG_CACHE_HOME/gtkterm/installed-files.txt`. **Une seule autorisation (`$SUDO`) par mise à jour.**

> **RÈGLE ABSOLUE (incident du 2026-10-12) : aucune demande de mot de passe.** Ne jamais exécuter un `gtkterm-update.sh` sans `GTKTERM_UPDATE_SUDO` pointant vers un programme de test
> (jamais `pkexec`, jamais `sudo`), `GTKTERM_UPDATE_DEPS=0`, et préfixe + `XDG_CACHE_HOME` temporaires. **Ne jamais exécuter l'ancien script** (celui de `HEAD`, qui appelle `pkexec` sans
> échappatoire), même pour une étape « RED ». Ne lancer le script que par `tests/test_update_script.sh`. Avant de lancer le test, vérifier : `grep -n 'GTKTERM_UPDATE_SUDO' data/gtkterm-update.sh`
> doit montrer la variable (le nouveau script est en place).

- [ ] **Step 1 : écrire le test `tests/test_update_script.sh`**

```sh
#!/bin/sh
# Test du script de mise à jour (data/gtkterm-update.sh), sans réseau, sans root, sans aucune demande de mot de passe : dépôt amont local à versions
# taguées (petit projet meson qui installe des fichiers), préfixe et cache temporaires, pkexec remplacé par un compteur qui exécute sans privilège,
# dépendances sautées.
# Usage : sh tests/test_update_script.sh <chemin de data/gtkterm-update.sh>   (sortie TAP)
set -eu
[ $# -eq 1 ] || { echo "usage: $0 <gtkterm-update.sh>" >&2; exit 2; }
SCRIPT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
# Garde-fou : un script sans échappatoire de test appellerait pkexec (demande de mot de passe) ; on refuse de le lancer.
grep -q 'GTKTERM_UPDATE_SUDO' "$SCRIPT" || { echo "Bail out! $SCRIPT n'a pas l'échappatoire GTKTERM_UPDATE_SUDO : refus de le lancer"; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# Compteur d'autorisations : remplace pkexec, journalise puis exécute la commande sans privilège
printf '#!/bin/sh\necho autorisation >> "%s/autorisations.log"\nexec "$@"\n' "$T" > "$T/faux-pkexec"
chmod +x "$T/faux-pkexec"
# Refus d'autorisation simulé (« Annuler ») : journalise, n'exécute rien, échoue comme pkexec (126)
printf '#!/bin/sh\necho refus >> "%s/autorisations.log"\nexit 126\n' "$T" > "$T/refus-pkexec"
chmod +x "$T/refus-pkexec"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 XDG_CACHE_HOME="$T/cache" GTKTERM_UPDATE_SUDO="$T/faux-pkexec" GTKTERM_UPDATE_DEPS=0 HOME="$T/home"
mkdir -p "$HOME"
PREFIX="$T/prefix"
UP="$T/amont"
URL="file://$UP"
DATA="$PREFIX/share/faux"
REG="$XDG_CACHE_HOME/gtkterm/installed-files.txt"
LOG="$T/autorisations.log"
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $1"; }
ko() { n=$((n + 1)); echo "not ok $n - $1"; fails=$((fails + 1)); }
check() { d="$1"; shift; if "$@"; then ok "$d"; else ko "$d"; fi; }
autorisations() { if [ -f "$LOG" ]; then wc -l < "$LOG" | tr -d ' '; else echo 0; fi; }

# version <tag> <variante du script, vide : copie exacte> <fichiers installés…>
version() {
	tag="$1"; variante="$2"; shift 2
	rm -rf "$UP/files"
	mkdir -p "$UP/files" "$UP/data" "$UP/bin"
	{
		echo "project('faux', version : '${tag#v}')"
		printf 'install_data('
		for f in "$@"; do printf "'files/%s', " "$f"; done
		echo "install_dir : get_option('datadir') / 'faux')"
		echo "install_data('bin/gtkterm', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')"
	} > "$UP/meson.build"
	echo "option('tests', type : 'boolean', value : true)" > "$UP/meson_options.txt"
	for f in "$@"; do echo "$tag" > "$UP/files/$f"; done
	printf '#!/bin/sh\nexit 0\n' > "$UP/bin/gtkterm"
	chmod +x "$UP/bin/gtkterm"
	cp "$SCRIPT" "$UP/data/gtkterm-update.sh"
	[ -z "$variante" ] || echo "# variante $variante" >> "$UP/data/gtkterm-update.sh"
	git -C "$UP" add -A
	git -C "$UP" -c user.name=T -c user.email=t@example.invalid commit -qm "$tag"
	git -C "$UP" -c user.name=T -c user.email=t@example.invalid tag -a "$tag" -m "$tag"
}

run() { sh "$SCRIPT" "$URL" "$PREFIX" "" "$1" > "$T/sortie.txt" 2>&1; }
contient() { grep -Fq -- "$1" "$T/sortie.txt"; }
vaut() { [ -f "$1" ] && [ "$(cat "$1")" = "$2" ]; }
absent() { [ ! -e "$1" ]; }
une_autorisation() { avant="$1"; [ "$(autorisations)" -eq $((avant + 1)) ]; }

mkdir -p "$UP"
git -C "$UP" init -q
version v1.0.0 "" a.txt ancien.txt
version v1.1.0 "1.1" a.txt b.txt
git -C "$UP" tag essai

# 1. Installation de v1.0.0 (copie de travail neuve)
a=$(autorisations)
check "installation v1.0.0" run v1.0.0
check "une seule autorisation pour v1.0.0" une_autorisation "$a"
check "a.txt de v1.0.0" vaut "$DATA/a.txt" v1.0.0
check "ancien.txt installé" vaut "$DATA/ancien.txt" v1.0.0
check "registre créé" grep -Fqx "$DATA/ancien.txt" "$REG"
check "build/ appartient à l'utilisateur" [ -O "$XDG_CACHE_HOME/gtkterm/src/build/build.ninja" ]

# 2. Montée en v1.1.0 : relance sur le script de v1.1.0, ménage de ancien.txt, toujours une seule autorisation
a=$(autorisations)
check "installation v1.1.0" run v1.1.0
check "une seule autorisation pour v1.1.0 (relance comprise)" une_autorisation "$a"
check "relance sur le script de la version visée" contient "Continuing with the update script of v1.1.0"
check "b.txt installé" vaut "$DATA/b.txt" v1.1.0
check "a.txt de v1.1.0" vaut "$DATA/a.txt" v1.1.0
check "ancien.txt retiré" absent "$DATA/ancien.txt"
check "message de retrait" contient "Removing $DATA/ancien.txt"

# 3. Retour à v1.0.0 : b.txt retiré, ancien.txt revenu
check "retour v1.0.0" run v1.0.0
check "b.txt retiré au retour" absent "$DATA/b.txt"
check "ancien.txt revenu" vaut "$DATA/ancien.txt" v1.0.0
check "copie de travail sur v1.0.0" [ "$(git -C "$XDG_CACHE_HOME/gtkterm/src" describe --tags --exact-match)" = v1.0.0 ]

# 4. Tags refusés, tag absent, URL refusée : arrêt sans rien installer ni demander d'autorisation
a=$(autorisations)
for mauvais in "" 1.0.0 v1.0 "v1.0.0;touch $T/pirate" "v01.0.0" "essai" "-v1.0.0"; do
	check "tag refusé « $mauvais »" sh -c '! sh "$1" "$2" "$3" "" "$4" >/dev/null 2>&1' sh "$SCRIPT" "$URL" "$PREFIX" "$mauvais"
done
check "aucune commande injectée" absent "$T/pirate"
check "tag absent de la source" sh -c '! sh "$1" "$2" "$3" "" v9.9.9 > "$4" 2>&1 && grep -q "not found" "$4"' sh "$SCRIPT" "$URL" "$PREFIX" "$T/s.txt"
check "URL en forme d'option refusée" sh -c '! sh "$1" "--upload-pack=touch $2" "$3" "" v1.0.0 >/dev/null 2>&1' sh "$SCRIPT" "$T/pirate2" "$PREFIX"
check "aucune commande injectée par l'URL" absent "$T/pirate2"
check "aucune autorisation demandée pour un refus" [ "$(autorisations)" -eq "$a" ]

# 5. Registre altéré : chemin hors du préfixe, chemin avec « .. », dossier : jamais supprimés
echo dehors > "$T/dehors.txt"
mkdir -p "$PREFIX/share/dossier"
{ echo "$T/dehors.txt"; echo "$PREFIX/../dehors.txt"; echo "$PREFIX/share/dossier"; } >> "$REG"
check "réinstallation avec registre altéré" run v1.0.0
check "fichier hors préfixe gardé" vaut "$T/dehors.txt" dehors
check "dossier gardé" [ -d "$PREFIX/share/dossier" ]

# 6. Mise à jour interrompue (build/ supprimé, registre présent) puis relancée : ménage d'après le registre
rm -rf "$XDG_CACHE_HOME/gtkterm/src/build"
check "relance après interruption (v1.1.0)" run v1.1.0
check "ancien.txt retiré d'après le registre" absent "$DATA/ancien.txt"

# 7. Sans registre ni journal (installation d'avant le registre) : ancien lanceur retiré s'il n'est pas fourni par la version installée
rm -f "$REG"
rm -rf "$XDG_CACHE_HOME/gtkterm/src/build"
mkdir -p "$PREFIX/share/applications"
echo ancien > "$PREFIX/share/applications/gtkterm.desktop"
check "installation sans registre" run v1.0.0
check "ancien lanceur retiré" absent "$PREFIX/share/applications/gtkterm.desktop"
check "registre recréé" [ -s "$REG" ]

# 8. Autorisation refusée (« Annuler ») : arrêt immédiat, message clair, rien d'installé, aucune nouvelle demande
a=$(autorisations)
check "refus : le script échoue" sh -c '! GTKTERM_UPDATE_SUDO="$1" sh "$2" "$3" "$4" "" v1.1.0 > "$5" 2>&1' sh "$T/refus-pkexec" "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "refus : une seule demande, jamais répétée" une_autorisation "$a"
check "refus : message" contient "GTKTerm was not updated"
check "refus : version en place intacte" vaut "$DATA/a.txt" v1.0.0
check "refus : rien de la version refusée" absent "$DATA/b.txt"

echo "1..$n"
[ "$fails" -eq 0 ]
```

- [ ] **Step 2 : RED sans aucun risque** — ne PAS lancer l'ancien script. Preuve d'échec acceptée : lancer le test contre l'ancien script, que le garde-fou du test refuse sans rien
  exécuter : `sh tests/test_update_script.sh data/gtkterm-update.sh` ; Expected : `Bail out! … n'a pas l'échappatoire GTKTERM_UPDATE_SUDO`, code 1, aucune demande d'autorisation.

- [ ] **Step 3 : réécrire `data/gtkterm-update.sh`**

Le même fichier servira aux deux lignes : seul le bloc `DEPS=` diffère (ici GTK 3, tel qu'il est aujourd'hui sur cette ligne ; vérifier avec `git show HEAD:data/gtkterm-update.sh`).

```sh
#!/bin/sh
# Mise à jour de GTKTerm vers une version publiée (tag vX.Y.Z) : copie de travail dans le cache, dépendances, construction, installation,
# ménage des fichiers de l'ancienne version, redémarrage. L'installation ne dépend d'aucune copie de travail d'origine.
#
# Usage : gtkterm-update.sh <url-du-dépôt> <préfixe> <pid-à-redémarrer> <tag>
# Une seule autorisation (pkexec) par mise à jour : dépendances, construction (redescendue à l'utilisateur par runuser), installation et ménage.
# Pour les tests seulement : GTKTERM_UPDATE_SUDO remplace pkexec, GTKTERM_UPDATE_DEPS=0 saute l'installation des dépendances.
# Contrat entre versions : ce script passe sur le tag demandé puis se relance sur le script de CETTE version, avec les mêmes arguments ; c'est la
# version visée qui choisit ses dépendances et sa construction (GTK 3 pour v1.3.x, GTK 4 pour v2.x).

set -eu

REPO_URL="${1:-}"
PREFIX="${2:-/usr/local}"
OLD_PID="${3:-}"
TAG="${4:-}"
SUDO="${GTKTERM_UPDATE_SUDO:-pkexec}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/gtkterm"
WORKDIR="$CACHE/src"
REGISTRY="$CACHE/installed-files.txt"

die() {
	echo "!!! $*" >&2
	exit 2
}

# L'URL vient de la configuration (modifiable) : jamais une option, jamais un transport « ext:: » qui lancerait une commande.
case "$REPO_URL" in
	'') die "Missing repository URL." ;;
	-*|ext::*) die "Refusing repository URL: $REPO_URL" ;;
esac
# Seule une version publiée vX.Y.Z (le premier filtre écarte aussi les retours à la ligne, que grep -x lirait ligne par ligne)
case "$TAG" in
	''|*[!v0-9.]*) die "Missing or invalid version tag '$TAG' (expected vX.Y.Z)." ;;
esac
printf '%s\n' "$TAG" | grep -Eqx 'v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})' ||
	die "Missing or invalid version tag '$TAG' (expected vX.Y.Z)."

echo ">>> GTKTerm update to $TAG"
echo ">>> repo:    $REPO_URL"
echo ">>> prefix:  $PREFIX"
echo ">>> workdir: $WORKDIR"

# --- 1. source : copie de travail sur le tag demandé ---------------------
if [ -d "$WORKDIR/.git" ]; then
	git -C "$WORKDIR" remote set-url origin -- "$REPO_URL"
else
	echo ">>> Cloning..."
	rm -rf "$WORKDIR"
	mkdir -p "$CACHE"
	git -c protocol.ext.allow=never clone --no-checkout -- "$REPO_URL" "$WORKDIR"
fi
git -C "$WORKDIR" -c protocol.ext.allow=never fetch --force origin '+refs/tags/*:refs/tags/*'
git -C "$WORKDIR" rev-parse -q --verify "refs/tags/$TAG^{commit}" >/dev/null || die "Version $TAG not found in $REPO_URL."
git -C "$WORKDIR" checkout --quiet --force --detach "refs/tags/$TAG"
# Aucun fichier d'une autre version ne reste ; build/ est gardé jusqu'à l'étape 4 (journal d'installation de la version en place)
git -C "$WORKDIR" clean -ffdxq -e build

# --- 2. continuer avec le script de la version obtenue --------------------
NEW_SCRIPT="$WORKDIR/data/gtkterm-update.sh"
if [ -z "${GTKTERM_UPDATE_REEXEC:-}" ] && [ -r "$NEW_SCRIPT" ] && ! cmp -s "$0" "$NEW_SCRIPT"; then
	echo ">>> Continuing with the update script of $TAG..."
	GTKTERM_UPDATE_REEXEC=1 exec sh "$NEW_SCRIPT" "$REPO_URL" "$PREFIX" "$OLD_PID" "$TAG"
fi

# --- 3. commande d'installation des dépendances (exécutée plus bas, dans l'unique autorisation) ---
INSTALL=""
if [ "${GTKTERM_UPDATE_DEPS:-1}" != 0 ]; then
	PM=""
	if [ -r /etc/os-release ]; then
		. /etc/os-release
	fi
	ID_ALL="${ID:-} ${ID_LIKE:-}"
	case " $ID_ALL " in
		*arch*) PM="pacman" ;;
		*debian*|*ubuntu*) PM="apt" ;;
		*fedora*|*rhel*) PM="dnf" ;;
	esac
	if command -v pacman >/dev/null 2>&1 && [ -z "$PM" ]; then PM="pacman"; fi
	if command -v apt-get >/dev/null 2>&1 && [ -z "$PM" ]; then PM="apt"; fi
	if command -v dnf >/dev/null 2>&1 && [ -z "$PM" ]; then PM="dnf"; fi

	# >>> dépendances de cette ligne (GTK 3) >>>
	case "$PM" in
		pacman)
			DEPS="gtk3 vte3 libgudev lua gtksourceview4 meson ninja gcc pkgconf git"
			INSTALL="pacman -S --needed --noconfirm $DEPS" ;;
		apt)
			DEPS="libgtk-3-dev libvte-2.91-dev libgudev-1.0-dev liblua5.4-dev libgtksourceview-4-dev meson ninja-build gcc pkg-config git"
			INSTALL="apt-get update && apt-get install -y $DEPS" ;;
		dnf)
			DEPS="gtk3-devel vte291-devel libgudev-devel lua-devel gtksourceview4-devel meson ninja-build gcc pkgconf-pkg-config git"
			INSTALL="dnf install -y $DEPS" ;;
		*)
			echo "!!! Unsupported distribution." >&2
			echo "!!! Install these libraries manually, then re-run:" >&2
			echo "!!!   gtk3, vte-2.91, gudev-1.0, lua5.4, gtksourceview-4, meson, ninja, gcc, pkg-config, git" >&2
			exit 3 ;;
	esac
	# <<< dépendances de cette ligne <<<
fi

# --- 4. registre des fichiers installés (utilisateur) ---------------------
# Amorcé une seule fois avec le journal de la version en place (installation d'avant le registre), avant que build/ ne soit supprimé
if [ ! -e "$REGISTRY" ] && [ -r "$WORKDIR/build/meson-logs/install-log.txt" ]; then
	cp "$WORKDIR/build/meson-logs/install-log.txt" "$REGISTRY"
fi
MESON=$(command -v meson) || die "meson not found."
NINJA=$(command -v ninja) || NINJA=$(command -v ninja-build) || die "ninja not found."

# --- 5. une seule autorisation : dépendances, construction, installation, ménage ---
# Script fixe : toutes les valeurs arrivent en $1…$9, jamais dans la chaîne. La construction redescend à l'utilisateur (runuser) : build/ lui
# appartient. Sans root (tests), elle se fait directement. Fichiers du registre absents du nouveau journal : retirés seulement sous le préfixe,
# sans « .. », fichiers ou liens (jamais un dossier), n'appartenant à aucun paquet ; sans registre, ancien lanceur d'avant GTK 4 retiré s'il n'est
# pas fourni par la version installée.
ROOT_SCRIPT='
set -e
install="$1"; user="$2"; home="$3"; workdir="$4"; prefix="$5"; meson="$6"; ninja="$7"; registry="$8"; legacy="$9"
if [ -n "$install" ]; then
	echo ">>> Installing build dependencies..."
	sh -c "$install"
fi
as_user() {
	if [ "$(id -u)" = 0 ] && [ "$(id -u "$user")" != 0 ]; then
		command -v runuser >/dev/null 2>&1 || { echo "!!! runuser not found (util-linux)." >&2; exit 1; }
		runuser -u "$user" -- env HOME="$home" "$@"
	else
		"$@"
	fi
}
echo ">>> Configuring & building..."
rm -rf "$workdir/build"
as_user "$meson" setup --prefix="$prefix" -Dtests=false "$workdir/build" "$workdir"
as_user "$ninja" -C "$workdir/build"
echo ">>> Installing..."
"$meson" install -C "$workdir/build" --no-rebuild
newlog="$workdir/build/meson-logs/install-log.txt"
owned() {
	{ command -v dpkg >/dev/null 2>&1 && dpkg -S "$1" >/dev/null 2>&1; } ||
	{ command -v rpm >/dev/null 2>&1 && rpm -qf "$1" >/dev/null 2>&1; } ||
	{ command -v pacman >/dev/null 2>&1 && pacman -Qo "$1" >/dev/null 2>&1; }
}
removable() {
	case "$1" in "$prefix"/*) ;; *) return 1 ;; esac
	case "$1" in */../*|*/..) return 1 ;; esac
	grep -Fqx -- "$1" "$newlog" && return 1
	[ -L "$1" ] || [ -f "$1" ] || return 1
	owned "$1" && return 1
	return 0
}
if [ -r "$registry" ]; then
	while IFS= read -r f; do
		case "$f" in ""|"#"*) continue ;; esac
		if removable "$f"; then
			echo ">>> Removing $f (not part of this version)"
			rm -f -- "$f"
		fi
	done < "$registry"
elif [ -n "$legacy" ] && removable "$legacy"; then
	echo ">>> Removing $legacy (old launcher)"
	rm -f -- "$legacy"
fi
'
echo ">>> Building and installing $TAG (one authorization for the whole update)..."
if ! $SUDO sh -c "$ROOT_SCRIPT" sh "$INSTALL" "$(id -un)" "$HOME" "$WORKDIR" "$PREFIX" "$MESON" "$NINJA" "$REGISTRY" "$PREFIX/share/applications/gtkterm.desktop"; then
	die "Authorization refused or update failed: GTKTerm was not updated."
fi

# --- 6. registre mis à jour (utilisateur) ----------------------------------
cp "$WORKDIR/build/meson-logs/install-log.txt" "$REGISTRY"

echo ">>> Update to $TAG complete."

# --- 7. redémarrage -------------------------------------------------------
if [ -n "$OLD_PID" ]; then
	echo ">>> Redémarrage de GTKTerm..."
	kill "$OLD_PID" 2>/dev/null || true
	sleep 1
fi
# Relancer l'exécutable qui vient d'être installé (pas le premier gtkterm du PATH). nohup le protège du SIGHUP envoyé à la fermeture du terminal
# de mise à jour ; « disown » n'existe pas sous /bin/sh (dash).
nohup "$PREFIX/bin/gtkterm" >/dev/null 2>&1 &
```

Remarques : `$SUDO` n'est appelé **qu'une fois** par exécution (le test le compte). Le cas 8 du test simule « Annuler » : le faux `pkexec` échoue (126) sans rien exécuter, le script
s'arrête avec « Authorization refused or update failed: GTKTerm was not updated. » et ne redemande rien. Le cas 7 (« sans registre ») passe par la règle de l'ancien lanceur : `removable`
vérifie aussi que le nouveau journal ne le contient pas (la ligne GTK 3 installe son propre `gtkterm.desktop` : il n'est alors jamais retiré).

- [ ] **Step 4 : lancer le test**

Run : `cd ~/git/gtkterm-gtk3 && grep -n GTKTERM_UPDATE_SUDO data/gtkterm-update.sh && sh -n data/gtkterm-update.sh && dash -n data/gtkterm-update.sh && sh tests/test_update_script.sh data/gtkterm-update.sh`
Expected : toutes les lignes `ok`, dernière ligne `1..N`, code de sortie 0, **aucune demande de mot de passe**.

- [ ] **Step 5 : commit**

```bash
cd ~/git/gtkterm-gtk3
git add data/gtkterm-update.sh tests/test_update_script.sh
git commit -m "mise à jour : le script installe un tag vX.Y.Z en une seule autorisation, se relance sur le script de la version visée et fait le ménage par registre

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task A4 : fenêtre « Updates » GTK 3, `GIT_DESCRIBE`, clé `update_skipped_version`

**Files :**
- Modify : `meson.build` (bloc `GIT_REVISION`, ~l. 31-40), `src/meson.build` (liste `sources`), `src/update.h`, `src/update.c` (réécriture), `src/gtkterm.c:41-46,86`, `src/interface.c`
  (`populate_help_menu` ~l. 636, `help_about_callback` ~l. 878-940), `src/term_config.h:65-67`, `src/term_config.c` (l. ~100, ~141, ~1653, ~1835, ~2036), `po/POTFILES.in`, `po/fr.po`

**Interfaces :**
- Consumes : Tasks A1, A2 (toutes les fonctions listées).
- Produces : `void update_window_show(GtkWindow *parent);` et `void update_startup_check(GtkWindow *parent);` (`src/update.h`) ; `config.update_skipped_version` (`gchar[32]`).

Pas de test automatique d'interface sur cette ligne (aucun harnais GTK 3) : la vérification se fait par la compilation et par la procédure de la Task A6.

- [ ] **Step 1 : `GIT_DESCRIBE` dans `meson.build`** — après le bloc qui calcule `git_revision` et avant `conf.set_quoted('GIT_REVISION', git_revision)`, ajouter :

```meson
# Version installée (mise à jour par versions taguées) : tag vX.Y.Z exact, ou vX.Y.Z-N-g<hash> après lui ; « unknown » sans tag joignable.
git_describe = 'unknown'
if git_prog.found()
	git_desc_result = run_command(git_prog, 'describe', '--tags', '--match', 'v[0-9]*', '--abbrev=7', check : false)
	if git_desc_result.returncode() == 0
		git_describe = git_desc_result.stdout().strip()
	endif
endif
conf.set_quoted('GIT_DESCRIBE', git_describe)
```

- [ ] **Step 2 : sources** — dans `src/meson.build`, remplacer `'update.c',` / `'update.h',` par :

```meson
	'update.c',
	'update.h',
	'update_fetch.c',
	'update_fetch.h',
	'update_versions.c',
	'update_versions.h',
```

- [ ] **Step 3 : clé `update_skipped_version`**
  - `src/term_config.h`, après `gchar update_prefix[256];` : `gchar update_skipped_version[32];	// « Skip this version » : tag ignoré ; vide : aucun`
  - `src/term_config.c` : déclarer `gchar **update_skipped_version_cfg;` après `gchar **update_prefix_cfg;` ; dans la table `cfg[]`, après `{"update_prefix", CFG_STRING, &update_prefix_cfg},`,
    ajouter `{"update_skipped_version", CFG_STRING, &update_skipped_version_cfg},` ; au chargement, après le bloc `update_prefix` (~l. 1653-1656) :

```c
			if(update_skipped_version_cfg != NULL && update_skipped_version_cfg[i] != NULL)
				g_strlcpy(config.update_skipped_version, update_skipped_version_cfg[i], sizeof(config.update_skipped_version));
			else
				config.update_skipped_version[0] = '\0';
```

    dans les valeurs par défaut (~l. 1835), après `update_prefix` : `config.update_skipped_version[0] = '\0';` ; à l'enregistrement (~l. 2036), après `update_prefix` :
    `cfgStoreValue(cfg, "update_skipped_version", config.update_skipped_version, CFG_INI, pos);`
  - Vérifier avec `grep -n update_prefix_cfg src/term_config.c` qu'aucun autre endroit (libération, copie) ne traite `update_prefix_cfg` ; s'il y en a, y traiter `update_skipped_version_cfg` pareil.

- [ ] **Step 4 : `src/update.h`**

```c
/* Fenêtre « Updates » (ligne GTK 3) et vérification au démarrage : versions publiées (tags vX.Y.Z), notes de NEWS, installation d'une version
 * au choix, retour en arrière compris. Spec : docs/superpowers/specs/2026-10-12-maj-par-tags-design.md. */

#ifndef UPDATE_H_
#define UPDATE_H_

#include <gtk/gtk.h>

/* Aide › Check for updates… : ouvre la fenêtre (ou la présente si elle est ouverte) et actualise la liste des versions. */
void update_window_show(GtkWindow *parent);
/* Démarrage (si update_check_startup) : actualise en arrière-plan ; ouvre la fenêtre seulement si une version plus récente que l'installée
 * existe et n'est pas ignorée. Silencieuse sinon, erreurs comprises. */
void update_startup_check(GtkWindow *parent);

#endif /* UPDATE_H_ */
```

- [ ] **Step 5 : réécrire `src/update.c`**

```c
/* Fenêtre « Updates » (ligne GTK 3) : voir update.h. Logique dans update_versions.c (versions, notes) et update_fetch.c (git) ; ici,
 * seulement l'interface et le lancement du script. */

#include "config.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>

#include "update.h"
#include "update_fetch.h"
#include "update_versions.h"
#include "term_config.h"

extern struct configuration_port config;

typedef struct
{
	GtkWidget *window;
	GtkWidget *url;
	GtkWidget *startup;
	GtkWidget *check;
	GtkWidget *versions;	/* GtkComboBoxText : identifiant = tag, texte = libellé */
	GtkWidget *notes;	/* GtkBox vertical de GtkLabel */
	GtkWidget *result;
	GtkWidget *skip;
	GtkWidget *install;
	GtReleases *releases;	/* dernière actualisation réussie ; NULL avant */
	GPtrArray *sections;	/* gt_news_parse(releases->news) */
	GCancellable *cancellable;	/* actualisation en cours */
} UpdateWindow;

static UpdateWindow *win;	/* fenêtre ouverte ; NULL sinon */
static GtkWindow *startup_parent;
static GCancellable *startup_cancellable;

static void
installed_version(GtVersion *v)
{
	gt_version_installed(GIT_DESCRIBE, VERSION, v);
}

static void
show_message(GtkWindow *parent, GtkMessageType type, const gchar *text)
{
	GtkWidget *m = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK, "%s", text);

	g_signal_connect(m, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show(m);
}

/* ---- lancer le script dans un terminal visible -------------------- */

typedef struct
{
	const gchar *name;
	const gchar *flag;	/* "-e", "--", ou NULL (kitty) */
	const gchar *extra;	/* options (police, zoom) placées avant -e / -- */
} TerminalDef;

static const TerminalDef terminals[] =
{
	{"xterm", "-e", "-fa Monospace -fs 12"},
	{"x-terminal-emulator", "-e", NULL},
	{"gnome-terminal", "--", "--zoom=1.5"},
	{"xfce4-terminal", "-e", "--font 'Monospace 12'"},
	{"konsole", "-e", NULL},
	{"lxterminal", "-e", NULL},
	{"qterminal", "-e", NULL},
	{"urxvt", "-e", "-fn 'xft:Monospace:size=12'"},
	{"kitty", NULL, "-o font_size=14"},
	{NULL, NULL, NULL}
};

static const TerminalDef *
find_terminal(void)
{
	for(gint i = 0; terminals[i].name != NULL; i++)
	{
		gchar *path = g_find_program_in_path(terminals[i].name);

		if(path != NULL)
		{
			g_free(path);
			return &terminals[i];
		}
	}
	return NULL;
}

static void
run_update_script(GtkWindow *parent, const gchar *tag)
{
	const TerminalDef *def;
	gchar *q_script, *q_url, *q_prefix, *q_tag, *inner_cmd, *inner, *cmd;
	GError *error = NULL;

	/* Seule une version publiée vX.Y.Z part vers le script */
	if(!gt_version_is_release_tag(tag))
		return;
	def = find_terminal();
	if(def == NULL)
	{
		show_message(parent, GTK_MESSAGE_ERROR, _("No terminal emulator found. Please install xterm or similar."));
		return;
	}
	/* Chaque valeur passe par g_shell_quote : une apostrophe dans l'URL ou le préfixe ne casse plus la commande. « read _ » : sous dash,
	 * « read » sans nom échoue et le terminal se fermait avant que l'on puisse lire la sortie. */
	q_script = g_shell_quote(PACKAGE_DATA_DIR "/gtkterm-update.sh");
	q_url = g_shell_quote(config.update_url);
	q_prefix = g_shell_quote(config.update_prefix);
	q_tag = g_shell_quote(tag);
	inner_cmd = g_strdup_printf("sh %s %s %s %d %s; echo; echo '>>> Appuyez sur Entrée pour fermer...'; read _", q_script, q_url, q_prefix, (gint)getpid(), q_tag);
	inner = g_shell_quote(inner_cmd);
	if(def->flag == NULL)
		cmd = g_strdup_printf("%s %s sh -c %s", def->name, def->extra ? def->extra : "", inner);
	else
		cmd = g_strdup_printf("%s %s %s sh -c %s", def->name, def->extra ? def->extra : "", def->flag, inner);
	if(!g_spawn_command_line_async(cmd, &error))
	{
		gchar *text = g_strdup_printf(_("Failed to launch update terminal:\n%s"), error->message);

		show_message(parent, GTK_MESSAGE_ERROR, text);
		g_free(text);
		g_clear_error(&error);
	}
	else
		show_message(parent, GTK_MESSAGE_INFO, _("The update is running in a terminal window.\nFollow the instructions there and restart GTKTerm when done."));
	g_free(cmd);
	g_free(inner);
	g_free(inner_cmd);
	g_free(q_tag);
	g_free(q_prefix);
	g_free(q_url);
	g_free(q_script);
}

/* ---- notes ---------------------------------------------------------- */

static void
install_css(void)
{
	static gboolean done;
	GtkCssProvider *css;

	if(done)
		return;
	done = TRUE;
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css, ".gt-update-warning { color: #c01c28; font-weight: bold; } .gt-update-heading { font-weight: bold; }", -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

static void
notes_clear(void)
{
	GList *children = gtk_container_get_children(GTK_CONTAINER(win->notes));

	for(GList *l = children; l != NULL; l = l->next)
		gtk_widget_destroy(GTK_WIDGET(l->data));
	g_list_free(children);
}

/* Texte de NEWS tel quel : jamais de balisage Pango */
static void
notes_add(const gchar *text, const gchar *css_class, gint indent)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_selectable(GTK_LABEL(l), TRUE);
	gtk_widget_set_margin_start(l, indent);
	if(css_class != NULL)
		gtk_style_context_add_class(gtk_widget_get_style_context(l), css_class);
	gtk_box_pack_start(GTK_BOX(win->notes), l, FALSE, FALSE, 0);
	gtk_widget_show(l);
}

static const gchar *
selected_tag(void)
{
	return gtk_combo_box_get_active_id(GTK_COMBO_BOX(win->versions));
}

/* Sections retenues pour le tag choisi (pointeurs empruntés) ; NULL sans sélection */
static GPtrArray *
selected_sections(const gchar *tag, GtNewsDirection *dir)
{
	GtVersion installed, target;

	if(tag == NULL || win->releases == NULL || !gt_version_parse(tag, &target))
		return NULL;
	installed_version(&installed);
	return gt_news_select(win->sections, win->releases->tags, &installed, &target, dir);
}

static void
refresh_selection(void)
{
	const gchar *tag = selected_tag();
	GtNewsDirection dir = GT_NEWS_SAME;
	GPtrArray *sel = selected_sections(tag, &dir);
	gchar *label;

	notes_clear();
	if(sel == NULL)
	{
		gtk_widget_set_sensitive(win->install, FALSE);
		gtk_widget_hide(win->skip);
		return;
	}
	if(dir == GT_NEWS_SAME)
		notes_add(_("This version is installed."), NULL, 0);
	if(gt_news_has_warning(sel))
	{
		notes_add(_("Warnings"), "gt-update-warning", 0);
		for(guint i = 0; i < sel->len; i++)
		{
			GtNewsSection *s = g_ptr_array_index(sel, i);

			for(guint j = 0; j < s->entries->len; j++)
			{
				GtNewsEntry *e = g_ptr_array_index(s->entries, j);

				if(e->kind == GT_NEWS_WARNING)
				{
					gchar *t = g_strdup_printf("%s : %s", s->tag, e->text);

					notes_add(t, "gt-update-warning", 12);
					g_free(t);
				}
			}
		}
	}
	if(dir == GT_NEWS_ROLLBACK && sel->len > 0)
		notes_add(_("You are leaving these versions:"), "gt-update-heading", 0);
	for(guint i = 0; i < sel->len; i++)
	{
		GtNewsSection *s = g_ptr_array_index(sel, i);

		notes_add(s->title, "gt-update-heading", 0);
		for(guint j = 0; j < s->entries->len; j++)
		{
			GtNewsEntry *e = g_ptr_array_index(s->entries, j);
			gchar *t = e->kind == GT_NEWS_ITEM ? g_strconcat("• ", e->text, NULL) : e->kind == GT_NEWS_WARNING ? g_strconcat("⚠ ", e->text, NULL) : g_strdup(e->text);

			notes_add(t, e->kind == GT_NEWS_WARNING ? "gt-update-warning" : NULL, 12);
			g_free(t);
		}
	}
	if(dir == GT_NEWS_UPGRADE)
		label = g_strdup_printf(_("Update to %s"), tag);
	else if(dir == GT_NEWS_ROLLBACK)
		label = g_strdup_printf(_("Roll back to %s"), tag);
	else
		label = g_strdup_printf(_("Reinstall %s"), tag);
	gtk_button_set_label(GTK_BUTTON(win->install), label);
	g_free(label);
	gtk_widget_set_sensitive(win->install, TRUE);
	gtk_widget_set_visible(win->skip, dir == GT_NEWS_UPGRADE);
	g_ptr_array_unref(sel);
}

/* Prend possession de r */
static void
set_releases(GtReleases *r)
{
	GtVersion installed;

	gt_releases_free(win->releases);
	if(win->sections != NULL)
		g_ptr_array_unref(win->sections);
	win->releases = r;
	win->sections = gt_news_parse(r->news);
	installed_version(&installed);
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(win->versions));
	for(guint i = 0; i < r->tags->len; i++)
	{
		const gchar *tag = g_ptr_array_index(r->tags, i);
		GtVersion v;
		gboolean latest = i == 0, inst;
		gchar *text;

		gt_version_parse(tag, &v);
		inst = gt_version_compare(&v, &installed) == 0;
		if(latest && inst)
			text = g_strdup_printf(_("%s — latest, installed"), tag);
		else if(latest)
			text = g_strdup_printf(_("%s — latest"), tag);
		else if(inst)
			text = g_strdup_printf(_("%s — installed"), tag);
		else
			text = g_strdup(tag);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(win->versions), tag, text);
		g_free(text);
	}
	gtk_widget_set_sensitive(win->versions, r->tags->len > 0);
	if(r->tags->len > 0)
		gtk_combo_box_set_active(GTK_COMBO_BOX(win->versions), 0);
	refresh_selection();
}

static void
set_result_for(GtReleases *r)
{
	GtVersion installed, newest;

	installed_version(&installed);
	if(r->tags->len == 0)
		gtk_label_set_text(GTK_LABEL(win->result), _("No published version found."));
	else if(gt_version_parse(g_ptr_array_index(r->tags, 0), &newest) && gt_version_compare(&newest, &installed) > 0)
	{
		gchar *t = g_strdup_printf(_("A new version is available (%s)."), (const gchar *)g_ptr_array_index(r->tags, 0));

		gtk_label_set_text(GTK_LABEL(win->result), t);
		g_free(t);
	}
	else
		gtk_label_set_text(GTK_LABEL(win->result), _("GTKTerm is up to date."));
}

/* ---- actualisation -------------------------------------------------- */

static void
on_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GCancellable *c = user_data;
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre fermée (annulée) ou actualisation remplacée : rien à afficher */
	if(!g_cancellable_is_cancelled(c) && win != NULL && win->cancellable == c)
	{
		g_clear_object(&win->cancellable);
		gtk_widget_set_sensitive(win->check, TRUE);
		if(r == NULL)
		{
			gchar *t = g_strdup_printf(_("Could not check for updates: %s"), error->message);

			gtk_label_set_text(GTK_LABEL(win->result), t);
			g_free(t);
		}
		else
		{
			set_result_for(r);
			set_releases(r);
			r = NULL;
		}
	}
	gt_releases_free(r);
	g_clear_error(&error);
	g_object_unref(c);
}

/* « Check now », Entrée dans le champ, ouverture : actualise l'URL AFFICHÉE, après l'avoir enregistrée si elle a changé */
static void
start_check(void)
{
	gchar *url = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(win->url))));
	gchar *cache;

	if(*url == '\0')
	{
		gtk_label_set_text(GTK_LABEL(win->result), _("Please enter the URL of the source repository."));
		g_free(url);
		return;
	}
	if(g_strcmp0(url, config.update_url) != 0)
	{
		g_strlcpy(config.update_url, url, sizeof(config.update_url));
		save_config_silent();
	}
	if(win->cancellable != NULL)
	{
		g_cancellable_cancel(win->cancellable);
		g_clear_object(&win->cancellable);
	}
	win->cancellable = g_cancellable_new();
	gtk_widget_set_sensitive(win->check, FALSE);
	gtk_label_set_text(GTK_LABEL(win->result), _("Checking…"));
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(url, cache, win->cancellable, on_fetched, g_object_ref(win->cancellable));
	g_free(cache);
	g_free(url);
}

/* ---- boutons -------------------------------------------------------- */

static void
on_confirm_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	gchar *tag = g_strdup(g_object_get_data(G_OBJECT(dialog), "tag"));

	gtk_widget_destroy(GTK_WIDGET(dialog));
	if(response == GTK_RESPONSE_YES && win != NULL)
		run_update_script(GTK_WINDOW(win->window), tag);
	g_free(tag);
}

static void
on_install_clicked(GtkButton *button, gpointer user_data)
{
	const gchar *tag = selected_tag();
	GPtrArray *sel = selected_sections(tag, NULL);
	gboolean warn = gt_news_has_warning(sel);

	if(sel != NULL)
		g_ptr_array_unref(sel);
	if(tag == NULL)
		return;
	if(!warn)
	{
		run_update_script(GTK_WINDOW(win->window), tag);
		return;
	}
	/* Avertissements : confirmation (non bloquante) avant de lancer le script */
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
	                                      _("Install %s?"), tag);

	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", _("See the warnings above."));
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_NO);
	g_object_set_data_full(G_OBJECT(d), "tag", g_strdup(tag), g_free);
	g_signal_connect(d, "response", G_CALLBACK(on_confirm_response), NULL);
	gtk_widget_show(d);
}

static void
on_skip_clicked(GtkButton *button, gpointer user_data)
{
	const gchar *tag = selected_tag();

	if(tag == NULL)
		return;
	g_strlcpy(config.update_skipped_version, tag, sizeof(config.update_skipped_version));
	save_config_silent();
	gtk_widget_destroy(win->window);
}

static void
on_startup_toggled(GtkToggleButton *button, gpointer user_data)
{
	config.update_check_startup = gtk_toggle_button_get_active(button);
	save_config_silent();
}

static void
on_check_clicked(GtkWidget *widget, gpointer user_data)
{
	start_check();
}

static void
on_versions_changed(GtkComboBox *combo, gpointer user_data)
{
	refresh_selection();
}

static void
on_destroy(GtkWidget *widget, gpointer user_data)
{
	if(win->cancellable != NULL)
	{
		g_cancellable_cancel(win->cancellable);
		g_clear_object(&win->cancellable);
	}
	gt_releases_free(win->releases);
	if(win->sections != NULL)
		g_ptr_array_unref(win->sections);
	g_free(win);
	win = NULL;
}

static GtkWidget *
row_label(const gchar *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	return l;
}

static void
build_window(GtkWindow *parent)
{
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *installed_label, *row, *scroll, *buttons, *close;
	gchar *installed_text;

	install_css();
	win = g_new0(UpdateWindow, 1);
	win->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win->window), _("Updates"));
	gtk_window_set_transient_for(GTK_WINDOW(win->window), parent);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(win->window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(win->window), 640, 520);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Installed version:")), 0, 0, 1, 1);
	installed_text = g_strdup_printf("%s (%.7s)", GIT_DESCRIBE, GIT_REVISION);
	installed_label = row_label(installed_text);
	g_free(installed_text);
	gtk_label_set_selectable(GTK_LABEL(installed_label), TRUE);
	gtk_grid_attach(GTK_GRID(grid), installed_label, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Update source:")), 0, 1, 1, 1);
	win->url = gtk_entry_new();
	gtk_widget_set_hexpand(win->url, TRUE);
	gtk_entry_set_width_chars(GTK_ENTRY(win->url), 50);
	gtk_entry_set_text(GTK_ENTRY(win->url), config.update_url);
	gtk_grid_attach(GTK_GRID(grid), win->url, 1, 1, 1, 1);

	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	win->startup = gtk_check_button_new_with_label(_("Check for updates at startup"));
	/* État posé avant le branchement du signal : ouvrir la fenêtre n'écrit rien */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win->startup), config.update_check_startup);
	win->check = gtk_button_new_with_label(_("Check now"));
	gtk_box_pack_start(GTK_BOX(row), win->startup, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(row), win->check, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), row, 0, 2, 2, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Version:")), 0, 3, 1, 1);
	win->versions = gtk_combo_box_text_new();
	gtk_widget_set_sensitive(win->versions, FALSE);
	gtk_grid_attach(GTK_GRID(grid), win->versions, 1, 3, 1, 1);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 220);
	gtk_widget_set_vexpand(scroll, TRUE);
	win->notes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(win->notes), 6);
	gtk_container_add(GTK_CONTAINER(scroll), win->notes);
	gtk_grid_attach(GTK_GRID(grid), scroll, 0, 4, 2, 1);

	win->result = row_label("");
	gtk_label_set_line_wrap(GTK_LABEL(win->result), TRUE);
	gtk_grid_attach(GTK_GRID(grid), win->result, 0, 5, 2, 1);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	win->skip = gtk_button_new_with_label(_("Skip this version"));
	win->install = gtk_button_new_with_label(_("Install"));
	gtk_widget_set_sensitive(win->install, FALSE);
	close = gtk_button_new_with_label(_("Close"));
	gtk_box_pack_start(GTK_BOX(buttons), win->skip, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), close, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), win->install, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), buttons, 0, 6, 2, 1);

	gtk_container_add(GTK_CONTAINER(win->window), grid);

	g_signal_connect(win->window, "destroy", G_CALLBACK(on_destroy), NULL);
	g_signal_connect(win->check, "clicked", G_CALLBACK(on_check_clicked), NULL);
	g_signal_connect(win->url, "activate", G_CALLBACK(on_check_clicked), NULL);
	g_signal_connect(win->startup, "toggled", G_CALLBACK(on_startup_toggled), NULL);
	g_signal_connect(win->versions, "changed", G_CALLBACK(on_versions_changed), NULL);
	g_signal_connect(win->skip, "clicked", G_CALLBACK(on_skip_clicked), NULL);
	g_signal_connect(win->install, "clicked", G_CALLBACK(on_install_clicked), NULL);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), win->window);

	gtk_widget_show_all(win->window);
	gtk_widget_hide(win->skip);
}

void
update_window_show(GtkWindow *parent)
{
	if(win == NULL)
	{
		build_window(parent);
		start_check();
	}
	gtk_window_present(GTK_WINDOW(win->window));
}

/* ---- démarrage ------------------------------------------------------ */

static void
on_startup_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre déjà ouverte à la main : elle actualise elle-même */
	if(r != NULL && win == NULL)
	{
		GtVersion installed;
		const gchar *newest = r->tags->len > 0 ? g_ptr_array_index(r->tags, 0) : NULL;

		installed_version(&installed);
		if(gt_release_verdict(&installed, newest, config.update_skipped_version[0] != '\0' ? config.update_skipped_version : NULL) == GT_RELEASE_AVAILABLE)
		{
			build_window(startup_parent);
			set_result_for(r);
			set_releases(r);
			r = NULL;
			gtk_window_present(GTK_WINDOW(win->window));
		}
	}
	if(error != NULL)
		g_debug("update check: %s", error->message);
	g_clear_error(&error);
	gt_releases_free(r);
	g_clear_object(&startup_cancellable);
}

void
update_startup_check(GtkWindow *parent)
{
	gchar *cache;

	if(!config.update_check_startup || startup_cancellable != NULL)
		return;
	startup_parent = parent;
	startup_cancellable = g_cancellable_new();
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(config.update_url, cache, startup_cancellable, on_startup_fetched, NULL);
	g_free(cache);
}
```

- [ ] **Step 6 : branchements**
  - `src/gtkterm.c` : dans `startup_update_check()`, remplacer `update_check(GTK_WINDOW(Fenetre), FALSE);` par `update_startup_check(GTK_WINDOW(Fenetre));`.
  - `src/interface.c`, `populate_help_menu()` : avant l'élément « About », ajouter

```c
	item = gtk_menu_item_new_with_mnemonic(_("Check for updates…"));
	connect_menu_item_callback(item, G_CALLBACK(help_updates_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
```

    avec, près de `help_about_callback` (prototype à côté de celui de `help_about_callback`, ~l. 148) :

```c
void help_updates_callback(GtkAction *action, gpointer data)
{
	update_window_show(GTK_WINDOW(Fenetre));
}
```

  - `src/interface.c`, `help_about_callback()` : retirer le champ « Update source » (`url_box` … `gtk_widget_show_all(url_box);`), le bouton « Check for updates now », la boucle
    `do { … } while(resp == 1);` (remplacée par `gtk_dialog_run(GTK_DIALOG(dialog));`) et l'enregistrement de l'URL qui suit (`g_strlcpy(config.update_url, …); save_config_silent();`) : l'URL
    se règle désormais dans la fenêtre « Updates ». Garder `#include "update.h"` (pour `update_window_show`).

- [ ] **Step 7 : traductions** — ajouter `src/update.c` et `src/update_fetch.c` à `po/POTFILES.in` (ordre alphabétique de la liste), puis
  `ninja -C build gtkterm-pot gtkterm-update-po` (si ces cibles n'existent pas sur cette ligne, `ninja -C build -t targets | grep -i pot` pour trouver leur nom). Dans `po/fr.po`, traduire
  chaque nouveau msgid (et retirer le marqueur `fuzzy` des entrées touchées) :

| msgid | msgstr |
|---|---|
| `Check for updates…` | `Rechercher des mises à jour…` |
| `Updates` | `Mises à jour` |
| `Installed version:` | `Version installée :` |
| `Update source:` | `Source des mises à jour :` |
| `Check for updates at startup` | `Vérifier au démarrage` |
| `Check now` | `Vérifier maintenant` |
| `Version:` | `Version :` |
| `Skip this version` | `Ignorer cette version` |
| `Install` | `Installer` |
| `Close` | `Fermer` |
| `This version is installed.` | `Cette version est installée.` |
| `Warnings` | `Avertissements` |
| `You are leaving these versions:` | `Vous quittez ces versions :` |
| `Update to %s` | `Mettre à jour vers %s` |
| `Roll back to %s` | `Revenir à %s` |
| `Reinstall %s` | `Réinstaller %s` |
| `%s — latest, installed` | `%s — la plus récente, installée` |
| `%s — latest` | `%s — la plus récente` |
| `%s — installed` | `%s — installée` |
| `No published version found.` | `Aucune version publiée trouvée.` |
| `A new version is available (%s).` | `Une nouvelle version est disponible (%s).` |
| `GTKTerm is up to date.` | `GTKTerm est à jour.` |
| `Could not check for updates: %s` | `Impossible de vérifier les mises à jour : %s` |
| `Please enter the URL of the source repository.` | `Saisissez l'URL du dépôt source.` |
| `Checking…` | `Vérification…` |
| `Install %s?` | `Installer %s ?` |
| `See the warnings above.` | `Voir les avertissements ci-dessus.` |
| `No terminal emulator found. Please install xterm or similar.` | `Aucun émulateur de terminal trouvé. Installez xterm ou équivalent.` |
| `Failed to launch update terminal:\n%s` | `Impossible de lancer le terminal de mise à jour :\n%s` |
| `The update is running in a terminal window.\nFollow the instructions there and restart GTKTerm when done.` | `La mise à jour se déroule dans une fenêtre de terminal.\nSuivez les instructions et redémarrez GTKTerm à la fin.` |
| `No update source URL.` | `Aucune URL de source de mise à jour.` |
| `Invalid update source URL: %s` | `URL de source de mise à jour invalide : %s` |
| `git failed (exit status %d)` | `git a échoué (code de sortie %d)` |

  Si une entrée existe déjà dans `po/fr.po` avec une traduction, garder celle-ci.

- [ ] **Step 8 : construire**

Run : `cd ~/git/gtkterm-gtk3 && (test -d build || meson setup build) && meson setup --reconfigure build && ninja -C build 2>&1 | grep -E "warning|error" ; ninja -C build && grep GIT_DESCRIBE build/config.h`
Expected : construction sans erreur ni nouvel avertissement venant de `update*.c` ; `#define GIT_DESCRIBE "unknown"` (aucun tag encore) ou `"v…"`. Ne **pas** lancer `./build/src/gtkterm`
(pas de fenêtre sur l'écran réel) : la vérification à l'écran est celle de la Task A6.

- [ ] **Step 9 : commit**

```bash
cd ~/git/gtkterm-gtk3
git add meson.build src/meson.build src/update.h src/update.c src/gtkterm.c src/interface.c src/term_config.h src/term_config.c po/POTFILES.in po/fr.po po/gtkterm.pot
git commit -m "mise à jour : fenêtre « Updates » GTK 3 (choix de la version, nouveautés et avertissements, retour en arrière), GIT_DESCRIBE, version ignorée enregistrée

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(Si `ninja … gtkterm-update-po` a aussi touché les autres `po/*.po`, les ajouter au même commit.)

### Task A5 : `NEWS` et `tools/publier-version.sh`

**Files :**
- Create : `NEWS`, `tools/publier-version.sh`, `tests/test_publier_version.sh`

**Interfaces :**
- Produces : `tools/publier-version.sh vX.Y.Z` (refus : arbre sale, nom invalide, tag existant, `meson.build` ≠ `X.Y.Z`, section `## vX.Y.Z` absente de `NEWS` ; sinon tag annoté
  « GTKTerm vX.Y.Z » sur `HEAD`, rien n'est poussé).

- [ ] **Step 1 : écrire le test `tests/test_publier_version.sh`**

```sh
#!/bin/sh
# Test de tools/publier-version.sh sur un dépôt jetable (sortie TAP). Usage : sh tests/test_publier_version.sh <chemin de tools/publier-version.sh>
set -eu
OUTIL=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_AUTHOR_NAME=T GIT_AUTHOR_EMAIL=t@example.invalid GIT_COMMITTER_NAME=T GIT_COMMITTER_EMAIL=t@example.invalid
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $1"; }
ko() { n=$((n + 1)); echo "not ok $n - $1"; fails=$((fails + 1)); }
check() { d="$1"; shift; if "$@"; then ok "$d"; else ko "$d"; fi; }
refuse() { d="$1"; shift; if (cd "$T/d" && sh "$OUTIL" "$@" >/dev/null 2>&1); then ko "$d"; else ok "$d"; fi; }

mkdir -p "$T/d"
cd "$T/d"
git init -q
printf "project(\n\t'gtkterm',\n\t'c',\n\tversion: '1.3.2',\n\tmeson_version : '>= 1.0.0'\n)\n" > meson.build
printf '# Nouveautés\n\n## v1.3.2 — 2026-10-12\n- un\n' > NEWS
git add -A
git commit -qm un

refuse "nom invalide" 1.3.2
refuse "nom invalide (v1.3)" v1.3
refuse "version différente de meson.build" v1.3.3
echo sale > sale.txt
refuse "arbre sale" v1.3.2
rm sale.txt
sed -i 's/## v1.3.2/## v1.3.1/' NEWS
git commit -qam "notes"
refuse "section NEWS absente" v1.3.2
sed -i 's/## v1.3.1/## v1.3.2/' NEWS
git commit -qam "notes"
check "publication acceptée" sh "$OUTIL" v1.3.2
check "tag annoté créé" [ "$(git cat-file -t v1.3.2)" = tag ]
check "message du tag" [ "$(git tag -l --format='%(contents:subject)' v1.3.2)" = "GTKTerm v1.3.2" ]
refuse "tag déjà présent" v1.3.2
check "rien n'est poussé (aucun remote touché)" [ -z "$(git remote)" ]

echo "1..$n"
[ "$fails" -eq 0 ]
```

- [ ] **Step 2 : vérifier l'échec** — Run : `cd ~/git/gtkterm-gtk3 && sh tests/test_publier_version.sh tools/publier-version.sh` ; Expected : échec (`tools/publier-version.sh` absent).

- [ ] **Step 3 : écrire `tools/publier-version.sh`** (puis `chmod +x tools/publier-version.sh`)

```sh
#!/bin/sh
# Publie une version de GTKTerm : vérifie l'arbre de travail, meson.build et NEWS, puis crée le tag annoté vX.Y.Z sur HEAD. Ne pousse rien.
# Usage : tools/publier-version.sh vX.Y.Z
set -eu

TAG="${1:-}"

die() {
	echo "publier-version : $*" >&2
	exit 1
}

case "$TAG" in
	''|*[!v0-9.]*) die "version invalide « $TAG » (attendu vX.Y.Z)" ;;
esac
printf '%s\n' "$TAG" | grep -Eqx 'v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})' || die "version invalide « $TAG » (attendu vX.Y.Z)"
cd "$(git rev-parse --show-toplevel)"
[ -z "$(git status --porcelain)" ] || die "l'arbre de travail n'est pas propre"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
	die "le tag $TAG existe déjà"
fi
VERSION=$(sed -n "s/^[[:space:]]*version[[:space:]]*:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n 1)
[ "$VERSION" = "${TAG#v}" ] || die "meson.build porte la version « $VERSION », pas « ${TAG#v} »"
MOTIF=$(printf '%s' "$TAG" | sed 's/\./\\./g')
grep -Eq "^## $MOTIF([[:space:]]|\$)" NEWS || die "NEWS n'a pas de section « ## $TAG »"
git tag -a "$TAG" -m "GTKTerm $TAG"
echo "Tag $TAG créé sur $(git rev-parse --short HEAD)."
echo "À pousser (sur feu vert) : git push origin $TAG && git push Maison $TAG"
```

- [ ] **Step 4 : écrire `NEWS`**

```
# Nouveautés de GTKTerm

Une section par version publiée (tag vX.Y.Z), de la plus récente à la plus ancienne. Format lu par la fenêtre « Mises à jour » :
« ## vX.Y.Z — date » ouvre une section, « - » une nouveauté, « ! » un avertissement (ce qui casse ou demande une action) ;
une ligne qui commence par deux espaces prolonge l'élément précédent. Une section sans tag publié n'est jamais affichée.

## v1.3.2 — 2026-10-12
- Mises à jour par versions publiées : Aide › Rechercher des mises à jour… liste les versions, affiche leurs nouveautés et
  avertissements, et installe celle que vous choisissez, y compris une plus ancienne pour revenir en arrière.
- Ports : un lien udev numéroté par appareil Metro (ttyM5_1, ttyM5_2…), liens udev affichés en gras.
```

(La date est celle du jour où le tag sera posé, Task A7 : la corriger alors si besoin.)

- [ ] **Step 5 : lancer le test** — Run : `cd ~/git/gtkterm-gtk3 && sh tests/test_publier_version.sh tools/publier-version.sh` ; Expected : toutes les lignes `ok`, code 0.

- [ ] **Step 6 : commit**

```bash
cd ~/git/gtkterm-gtk3
git add NEWS tools/publier-version.sh tests/test_publier_version.sh
git commit -m "publication : fichier NEWS et tools/publier-version.sh (contrôles puis tag annoté, sans poussée)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task A6 : vérification de la ligne GTK 3 avec un dépôt de test local (utilisateur)

**Files :** aucun (procédure). Le script de préparation est écrit dans le dossier temporaire de session, pas dans le dépôt.

- [ ] **Step 1 : tests automatiques de la ligne** — Run :
  `cd ~/git/gtkterm-gtk3 && ./build-tests/test_update_versions && ./build-tests/test_update_fetch && sh tests/test_update_script.sh data/gtkterm-update.sh && sh tests/test_publier_version.sh tools/publier-version.sh`
  (recompiler les deux tests C avec les commandes des Tasks A1/A2 avant) ; Expected : tout `ok`.

- [ ] **Step 2 : préparer un dépôt source local** — créer `$SCRATCH/depot-test.git` (dépôt nu) contenant la branche de travail avec deux tags factices au-dessus d'elle, pour exercer la
  fenêtre sans rien publier :

```sh
SCRATCH=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/94f905d6-fc8a-4b29-89a2-25763d0bc74f/scratchpad
rm -rf "$SCRATCH/depot-test.git" "$SCRATCH/depot-test-wt"
git clone -q --bare ~/git/gtkterm-gtk3 "$SCRATCH/depot-test.git"
git -C "$SCRATCH/depot-test.git" tag -a v1.3.2 -m v1.3.2 feature/maj-par-tags
git clone -q "$SCRATCH/depot-test.git" "$SCRATCH/depot-test-wt"
cd "$SCRATCH/depot-test-wt" && git checkout -q feature/maj-par-tags
{ sed -n '1,/^## v1.3.2/{/^## v1.3.2/!p}' NEWS; printf '## v1.3.3 — essai\n- Version factice de test\n! Avertissement factice : vérifie la confirmation\n\n'; sed -n '/^## v1.3.2/,$p' NEWS; } > NEWS.new
mv NEWS.new NEWS
sed -i "s/version: '1.3.2'/version: '1.3.3'/" meson.build
git -c user.name=T -c user.email=t@example.invalid commit -qam "v1.3.3 factice" && git tag -a v1.3.3 -m v1.3.3 && git push -q origin HEAD:refs/heads/essai v1.3.3
```

- [ ] **Step 3 : procédure manuelle, à donner à l'utilisateur** (l'agent n'ouvre aucune fenêtre sur l'écran réel) :
  1. `./build/src/gtkterm` depuis `~/git/gtkterm-gtk3` ; Aide › Rechercher des mises à jour… ; mettre `file://<SCRATCH>/depot-test.git` dans « Source des mises à jour », « Vérifier
     maintenant ».
  2. Attendu : liste `v1.3.3 — la plus récente`, `v1.3.2` ; notes de v1.3.3 avec l'avertissement factice en tête ; bouton « Mettre à jour vers v1.3.3 » ; « Ignorer cette version » visible.
  3. Choisir v1.3.2 : « Réinstaller v1.3.2 » ou « Revenir à v1.3.2 » selon `GIT_DESCRIBE` (sans tag local : version de `meson.build` = 1.3.2 → « Réinstaller ») ; « Ignorer… » masqué.
  4. « Ignorer cette version » sur v1.3.3, fermer GTKTerm, relancer : aucune fenêtre au démarrage ; `grep update_skipped_version ~/.config/.gtktermrc` montre `v1.3.3`.
  5. Ne pas cliquer « Mettre à jour » à cette étape (installation réelle dans `/usr/local`) : l'installation de bout en bout est faite en Task B6.

- [ ] **Step 4 : nettoyer** — remettre la source par défaut dans la fenêtre (`https://github.com/Mula-Gabriel/gtkterm.git`) et effacer la version ignorée de test
  (`sed -i '/update_skipped_version/d' ~/.config/.gtktermrc`, GTKTerm fermé). Garder `$SCRATCH/depot-test.git` pour la Task B6.

### Task A7 : écraser, fusionner dans `master` (ligne GTK 3), tag `v1.3.2`

**Files :** aucun nouveau.

- [ ] **Step 1 : écraser en commits thématiques** (règle de l'utilisateur : specs/plans, implémentation, outillage/doc) :

```bash
cd ~/git/gtkterm-gtk3
git reset --soft 0a013c2
git reset -q
git add docs/superpowers/specs/2026-10-12-maj-par-tags-design.md docs/superpowers/plans/2026-10-12-maj-par-tags.md
git commit -m "docs : mise à jour par versions taguées (spec et plan)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git add meson.build src/ po/ data/gtkterm-update.sh .gitignore
git commit -m "mise à jour par versions taguées : choix de la version (retour en arrière compris), nouveautés et avertissements de NEWS, script qui installe un tag et fait le ménage

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git add NEWS tools/ tests/
git commit -m "outillage : NEWS, tools/publier-version.sh, tests des modules partagés, du script et de l'outil de publication

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short   # attendu : vide
```

- [ ] **Step 2 : fusionner dans `master` de la ligne** :

```bash
git checkout master
git merge --no-ff feature/maj-par-tags -m "merge: mise à jour par versions taguées, ligne GTK 3 (feature/maj-par-tags)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 3 : poser `v1.3.2`** — corriger la date de l'en-tête `## v1.3.2` de `NEWS` si ce n'est plus le jour même (commit « NEWS : date de v1.3.2 »), puis
  `tools/publier-version.sh v1.3.2` ; Expected : « Tag v1.3.2 créé sur … ». Puis `git branch -d feature/maj-par-tags` (branche locale seulement : elle n'a jamais été poussée).
  **Ne rien pousser.**

---

## Partie B — ligne refonte (`~/git/gtkterm`, branche `feature/maj-par-tags`)

### Task B1 : fusion de la ligne GTK 3

**Files :** résolution de conflits ; `src/meson.build` (core), `tests/meson.build`, `data/gtkterm-update.sh` (bloc dépendances GTK 4).

- [ ] **Step 1 : branche et fusion**

```bash
cd ~/git/gtkterm
git checkout -b feature/maj-par-tags master
git fetch ~/git/gtkterm-gtk3 master:refs/remotes/gtk3/master --tags   # v1.3.2 arrive aussi
git merge --no-ff gtk3/master -m "merge: ligne GTK 3 v1.3.2 (mise à jour par versions taguées) dans la refonte"
```

- [ ] **Step 2 : résoudre** (`git status` liste les conflits) :
  - fichiers GTK 3 supprimés ou réécrits par la refonte (`src/update.c`, `src/update.h`, `src/interface.c`, `src/gtkterm.c`, `src/term_config.*`, et tout autre « deleted by us ») : `git rm` ;
  - `meson.build`, `src/meson.build`, `po/*` (dont `po/fr.po`, `po/POTFILES.in`, `po/gtkterm.pot`), `.gitignore` : version de la refonte (`git checkout --ours -- <fichier>`), ainsi que `meson_options.txt` (la refonte déclare déjà `tests` et ses autres options) ;
  - `data/gtkterm-update.sh` : prendre la version de la ligne GTK 3 (`git checkout --theirs -- data/gtkterm-update.sh`) puis remplacer le bloc entre `# >>> dépendances de cette ligne` et
    `# <<< dépendances de cette ligne` par les dépendances GTK 4 de la refonte :

```sh
	# >>> dépendances de cette ligne (GTK 4 ≥ 4.14, VTE gtk4 ≥ 0.76, GtkSourceView 5 ≥ 5.12 : Ubuntu 24.04 ou plus récent) >>>
	case "$PM" in
		pacman)
			DEPS="gtk4 vte4 libgudev lua gtksourceview5 meson ninja gcc pkgconf git"
			INSTALL="pacman -S --needed --noconfirm $DEPS" ;;
		apt)
			DEPS="libgtk-4-dev libvte-2.91-gtk4-dev libgudev-1.0-dev liblua5.4-dev libgtksourceview-5-dev meson ninja-build gcc pkg-config git"
			INSTALL="apt-get update && apt-get install -y $DEPS" ;;
		dnf)
			DEPS="gtk4-devel vte291-gtk4-devel libgudev-devel lua-devel gtksourceview5-devel meson ninja-build gcc pkgconf-pkg-config git"
			INSTALL="dnf install -y $DEPS" ;;
		*)
			echo "!!! Unsupported distribution." >&2
			echo "!!! Install these libraries manually, then re-run:" >&2
			echo "!!!   gtk4 (>= 4.14), vte-2.91-gtk4 (>= 0.76), gudev-1.0, lua5.4, gtksourceview-5 (>= 5.12), meson, ninja, gcc, pkg-config, git" >&2
			exit 3 ;;
	esac
	# <<< dépendances de cette ligne <<<
```

  - fichiers nouveaux venus sans conflit : `src/update_versions.*`, `src/update_fetch.*`, `NEWS`, `tools/publier-version.sh`, `tests/test_update_versions.c`, `tests/test_update_fetch.c`,
    `tests/test_update_script.sh`, `tests/test_publier_version.sh`, la spec et le plan : les garder tels quels.
  - Vérifier : `git diff gtk3/master -- src/update_versions.c src/update_versions.h src/update_fetch.c src/update_fetch.h` est **vide**.

- [ ] **Step 3 : brancher les modules et les tests**
  - `src/meson.build`, `core_sources`, après `'update_check.h',` : `'update_fetch.c',`, `'update_fetch.h',`, `'update_versions.c',`, `'update_versions.h',`.
  - `tests/meson.build`, `unit_tests`, après `'test_update_check' : [],` : `'test_update_versions' : [],` et `'test_update_fetch' : [],`. `test_update_fetch` lance git : lui donner un délai
    de 60 s en le sortant de la boucle générique si le délai par défaut (30 s) ne suffit pas sur le poste (mesurer avec `meson test -C build test_update_fetch -v`).
  - `tests/meson.build`, après la boucle `unit_tests` :

```meson
# Script de mise à jour et outil de publication (sh, git, meson, ninja ; aucun réseau ni root) : dépôts et préfixes jetables.
sh_prog = find_program('sh')
test('update_script', sh_prog, args : [files('test_update_script.sh'), files('../data/gtkterm-update.sh')], protocol : 'tap', suite : 'unit',
	env : test_env, timeout : 300)
test('publier_version', sh_prog, args : [files('test_publier_version.sh'), files('../tools/publier-version.sh')], protocol : 'tap', suite : 'unit',
	env : test_env, timeout : 60)
```

  - `po/POTFILES.in` : ajouter `src/update_fetch.c` (ordre de la liste).

- [ ] **Step 4 : construire et tester** — Run : `cd ~/git/gtkterm && ninja -C build && meson test -C build --suite unit` ; Expected : construction OK ; `test_update_versions`,
  `test_update_fetch`, `update_script`, `publier_version` OK ; les autres tests inchangés (`test_update_check` passe encore : rien n'y a changé).

- [ ] **Step 5 : conclure la fusion**

```bash
git add -A
git commit --no-edit   # conclut le commit de fusion (message du Step 1) ; ajouter la ligne Co-Authored-By au message si l'éditeur s'ouvre
git log --oneline -1 --merges   # le commit de fusion, à deux parents
```

Ce commit de fusion **ne sera jamais écrasé** (Task B6) : il rend `master` de la ligne GTK 3 ancêtre du nouveau `master`.

### Task B2 : `GIT_DESCRIBE` et clé `update_skipped_version` (refonte)

**Files :**
- Create : `src/git_describe.h.in`
- Modify : `src/meson.build:89-98`, `src/ui/meson.build:70`, `tests/gui/meson.build:12-13`, `src/app_config.h:20,35-39`, `src/app_config.c:88-92,108-119,195`, `tests/test_app_config.c:533-597`

**Interfaces :**
- Produces : `git_describe.h` (`#define GIT_DESCRIBE "…"`, généré) ; `settings.update_skipped_version` ; `void config_save_skipped_version(const gchar *tag);` (remplace
  `config_save_skipped_revision`).

- [ ] **Step 1 : adapter le test** — dans `tests/test_app_config.c`, renommer : `update_skipped_revision` → `update_skipped_version` (champ et clé), `config_save_skipped_revision` →
  `config_save_skipped_version`, `test_revision_ignoree` → `test_version_ignoree` (et son chemin dans `main()`), valeur `"0123abc"` → `"v2.1.0"` :
  `sed -i 's/update_skipped_revision/update_skipped_version/g; s/config_save_skipped_revision/config_save_skipped_version/g; s/test_revision_ignoree/test_version_ignoree/g; s/"0123abc"/"v2.1.0"/g' tests/test_app_config.c`
  puis relire le commentaire au-dessus du test (« M34 : « Skip this version » enregistré… ») et y écrire « tag ignoré ». Ajouter à la fin de `test_version_ignoree`, avant
  `g_object_unref(s);` : l'ancienne clé n'est plus lue —

```c
	/* L'ancienne clé (une révision) n'est plus lue */
	kf = charger_rc();
	g_key_file_set_string(kf, "general", "update_skipped_revision", "0123abc");
	g_assert_true(g_key_file_save_to_file(kf, chemin_rc, NULL));
	g_key_file_free(kf);
	config_file_init();
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_null(settings.update_skipped_version);
```

  (`chemin_rc` et `charger_rc()` existent déjà dans ce fichier.)

- [ ] **Step 2 : vérifier l'échec** — Run : `ninja -C build 2>&1 | tail -3` ; Expected : erreur de compilation (`config_save_skipped_version` / `update_skipped_version` inconnus).

- [ ] **Step 3 : implémenter** — `src/app_config.h` : champ `gchar *update_skipped_version;	/* « Skip this version » : tag vX.Y.Z ignoré ; NULL : aucun */`, prototype
  `void config_save_skipped_version(const gchar *tag);	/* [general] update_skipped_version ; NULL efface */`, commentaire des écritures partielles mis à jour. `src/app_config.c` : même
  renommage (`sed -i 's/update_skipped_revision/update_skipped_version/g; s/config_save_skipped_revision/config_save_skipped_version/g' src/app_config.c src/app_config.h`), paramètre `rev`
  renommé `tag`. Rien d'autre ne change (lecture, écriture partielle après relecture, vide = aucune).

- [ ] **Step 4 : `GIT_DESCRIBE`** — créer `src/git_describe.h.in` :

```c
/* Généré par vcs_tag() (src/meson.build) à chaque construction : ne pas modifier git_describe.h. */
#ifndef GIT_DESCRIBE_H_
#define GIT_DESCRIBE_H_

#define GIT_DESCRIBE "@GIT_DESCRIBE@"

#endif
```

  dans `src/meson.build`, après `git_revision_h = vcs_tag(…)` :

```meson
# Version installée (mise à jour par versions taguées) : tag vX.Y.Z exact, ou vX.Y.Z-N-g<hash> après lui ; « unknown » sans tag joignable.
git_describe_h = vcs_tag(
	command : ['git', '-c', 'safe.directory=*', 'describe', '--tags', '--match', 'v[0-9]*', '--abbrev=7'],
	fallback : 'unknown',
	input : 'git_describe.h.in',
	output : 'git_describe.h',
	replace_string : '@GIT_DESCRIBE@'
)
```

  `src/ui/meson.build` : `git_describe_h,` après `git_revision_h,` ; `tests/gui/meson.build` : `gui_env.set('GTKTERM_GIT_DESCRIBE_H', git_describe_h.full_path())` après la ligne
  `GTKTERM_GIT_REVISION_H`.

- [ ] **Step 5 : construire et tester** — Run : `ninja -C build && meson test -C build test_app_config && cat build/src/git_describe.h | grep define` ; Expected : OK ;
  `#define GIT_DESCRIBE "v1.3.2-<N>-g<hash>"` (le tag v1.3.2 est arrivé avec la fusion). La fenêtre (`update_window.c`) compile encore : elle n'utilise pas la clé renommée… si elle
  l'utilise (`settings.update_skipped_revision`, `config_save_skipped_revision`), la renommer aussi ici — elle est réécrite en Task B4.

- [ ] **Step 6 : commit**

```bash
git add src/git_describe.h.in src/meson.build src/ui/meson.build tests/gui/meson.build src/app_config.h src/app_config.c tests/test_app_config.c src/ui/update_window.c
git commit -m "mise à jour : GIT_DESCRIBE (vcs_tag) et version ignorée par tag (update_skipped_version)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task B3 : `update_check.c` allégé, tag en argument du script

**Files :**
- Modify : `src/update_check.h`, `src/update_check.c`, `tests/test_update_check.c`

**Interfaces :**
- Produces : `gchar **gt_update_terminal_argv(const gchar *terminal, const gchar *script, const gchar *url, const gchar *prefix, GPid pid, const gchar *tag);` (NULL si un argument est NULL
  ou si `tag` n'est pas une version publiée) ; `gchar *gt_update_find_terminal(void);` inchangé. **Retirés** : `GtUpdateVerdict`, `gt_update_compare`, `gt_update_fetch_remote_async/finish`,
  `gt_update_local_is_newer`, `gt_update_cache_src_dir`.

- [ ] **Step 1 : adapter le test** — dans `tests/test_update_check.c` :
  - supprimer `CasCompare`, `cas_compare[]`, `test_compare`, `test_local_is_newer`, `Depot`/`depot_*`, `git()`, `Attente`, `on_fetch`, `recuperer`, `test_fetch_local`, `test_fetch_erreurs`,
    `test_fetch_annule`, les `#define A40/B40/C40_*` et leurs `g_test_add_func` ; garder `supprimer_arbre` (utilisé par `test_terminal_argv_sans_shell`) ;
  - commentaire d'en-tête : « Tests de update_check.c (cœur) : commande du script en liste d'arguments (M34), tag compris, et recherche du terminal. » ;
  - `test_terminal_argv` : appel `gt_update_terminal_argv(terminaux[t], "/usr/share/gtkterm/gtkterm-update.sh", URL_PIEGE, PREFIX_PIEGE, 4242, "v2.1.0")`, `n >= 9`, et les indices :

```c
		g_assert_cmpstr(argv[0], ==, terminaux[t]);
		g_assert_cmpstr(argv[n - 8], ==, "sh");
		g_assert_cmpstr(argv[n - 7], ==, "-c");
		g_assert_cmpstr(argv[n - 5], ==, "/usr/share/gtkterm/gtkterm-update.sh");
		g_assert_cmpstr(argv[n - 4], ==, URL_PIEGE);
		g_assert_cmpstr(argv[n - 3], ==, PREFIX_PIEGE);
		g_assert_cmpstr(argv[n - 2], ==, "4242");
		g_assert_cmpstr(argv[n - 1], ==, "v2.1.0");
		/* Aucune valeur recopiée ailleurs : pas de commande construite par chaîne (M34) */
		for(guint i = 0; i < n - 4; i++)
		{
			g_assert_null(strstr(argv[i], "essai"));
			g_assert_null(strstr(argv[i], "mon prefix"));
		}
```

  - `test_terminal_argv_drapeaux` : ajouter `"v1.0.0"` en dernier argument de chaque appel ; les indices `argv[n - 8]` deviennent `argv[n - 9]` ; ajouter
    `g_assert_null(gt_update_terminal_argv("xterm", "/s.sh", "u", "/p", 1, NULL));` et `g_assert_null(gt_update_terminal_argv("xterm", "/s.sh", "u", "/p", 1, "v1.0;rm -rf x"));` ;
  - `test_terminal_argv_sans_shell` : appel avec `"v2.1.0"`, `attendu = g_strdup_printf("%s\n%s\n4242\nv2.1.0\n", URL_PIEGE, PREFIX_PIEGE)`, `argv + n - 8`.
  - `main()` : seulement `terminal-argv/valeurs-piegees`, `terminal-argv/drapeaux`, `terminal-argv/sans-shell`, `find-terminal`.

- [ ] **Step 2 : vérifier l'échec** — Run : `ninja -C build 2>&1 | tail -3` ; Expected : erreur (trop d'arguments pour `gt_update_terminal_argv`).

- [ ] **Step 3 : implémenter** — `src/update_check.h` :

```c
/* Lancement du script de mise à jour (sous-projets 6a puis « mise à jour par versions taguées ») : commande en liste d'arguments (M34), dans
 * un émulateur de terminal détecté. Versions et notes : update_versions.h, update_fetch.h. GLib seulement (cœur). */

#ifndef UPDATE_CHECK_H_
#define UPDATE_CHECK_H_

#include <glib.h>

G_BEGIN_DECLS

/* argv (g_strfreev) : <terminal> <options> <drapeau> sh -c <script fixe> <script> <url> <prefix> <pid> <tag> ; aucune valeur n'est
 * interprétée par un shell. terminal : nom ou chemin d'un émulateur pris en charge ; NULL si inconnu, si un argument est NULL ou si tag n'est
 * pas une version publiée vX.Y.Z. */
gchar **gt_update_terminal_argv(const gchar *terminal, const gchar *script, const gchar *url, const gchar *prefix, GPid pid, const gchar *tag);

/* Premier émulateur pris en charge trouvé dans le PATH (ordre : xterm, x-terminal-emulator, gnome-terminal, xfce4-terminal, konsole,
 * lxterminal, qterminal, urxvt, kitty), son nom (g_free) ; NULL si aucun. */
gchar *gt_update_find_terminal(void);

G_END_DECLS

#endif /* UPDATE_CHECK_H_ */
```

  `src/update_check.c` : retirer `is_hex`, `is_revision`, `same_revision`, `gt_update_compare`, toute la partie « git ls-remote asynchrone » (`first_line`, `parse_head`, `on_ls_remote_done`,
  `gt_update_fetch_remote_async/finish`), `gt_update_local_is_newer`, `gt_update_cache_src_dir` ; en-tête du fichier : « Lancement du script de mise à jour : voir update_check.h. » ; inclure
  `"update_versions.h"` ; `#define GT_UPDATE_INNER_SCRIPT "sh \"$0\" \"$1\" \"$2\" \"$3\" \"$4\"; echo; echo '>>> Appuyez sur Entrée pour fermer...'; read _"` (commentaire : valeurs en $0…$4) ;
  `gt_update_terminal_argv` reçoit `const gchar *tag`, refuse `!gt_version_is_release_tag(tag)` avec les autres NULL, et ajoute `g_ptr_array_add(argv, g_strdup(tag));` après le PID.
  Retirer les `#include <gio/gio.h>` et `<glib/gi18n.h>` devenus inutiles.

- [ ] **Step 4 : construire et tester** — Run : `ninja -C build 2>&1 | grep -E "error" ; meson test -C build test_update_check` ; Expected : `test_update_check` OK. La construction de
  `src/ui/update_window.c` **échoue** (fonctions retirées) : c'est attendu, elle est réécrite en Task B4 ; ne pas committer avant la fin de B4 (B3 et B4 forment un seul commit).

### Task B4 : fenêtre « Updates » GTK 4

**Files :**
- Modify (réécriture) : `src/ui/update_window.c`, `src/ui/update_window.h`
- Modify : `tests/gui/bin/git`, `tests/gui/gtktest.py` (`fake_git`, nouvelle aide `local_describe`), `tests/gui/test_update_window.py` (réécriture), `po/fr.po` (+ `po/gtkterm.pot`)

**Interfaces :**
- Consumes : Tasks A1, A2, B2 (`settings.update_skipped_version`, `config_save_skipped_version`, `GIT_DESCRIBE`), B3 (`gt_update_terminal_argv` à 6 arguments).
- Produces : `gt_update_window_show(GtkWindow *parent)`, `gt_update_startup_check(GtkWindow *main_window)`, `gt_update_shutdown(void)` (mêmes noms et appels qu'aujourd'hui dans `app.c`) ;
  `gt_update_offer` **retirée**. Widgets nommés : `update-window`, `update-installed`, `update-url`, `update-startup`, `update-check`, `update-version`, `update-notes`, `update-result`,
  `update-skip`, `update-install`, `update-close` ; classes CSS des notes : `heading` (titres), `warning` (avertissements).

- [ ] **Step 1 : faux git** — réécrire `tests/gui/bin/git` :

```sh
#!/bin/sh
# Faux git de la suite gui : aucune connexion réseau, aucun dépôt réel touché.
# Journalise chaque appel dans $EM_FAKEGIT_LOG : une ligne « FAKEGIT » suivie de chaque argument précédé d'une tabulation.
# Versions servies depuis $EM_FAKEGIT_DIR : « tags » (un nom de tag par ligne), « news » (texte de NEWS), « delay » (secondes avant de répondre à
# clone et fetch). Sans « tags », clone et fetch échouent comme une URL injoignable (code 128).
# clone : crée le dossier cible (dernier argument) avec un fichier HEAD ; remote, fetch : succès ; for-each-ref : contenu de « tags » ;
# show : contenu de « news » (code 128 sans ce fichier) ; toute autre commande échoue (code 1).
{
	printf 'FAKEGIT'
	for a in "$@"; do printf '\t%s' "$a"; done
	printf '\n'
} >> "${EM_FAKEGIT_LOG:-/dev/null}"
d="${EM_FAKEGIT_DIR:-/nonexistent}"
cmd=""
last=""
for a in "$@"; do
	last="$a"
	if [ -z "$cmd" ]; then
		case "$a" in
			clone|remote|fetch|for-each-ref|show) cmd="$a" ;;
		esac
	fi
done
case "$cmd" in
	clone|fetch)
		if [ -r "$d/delay" ]; then sleep "$(cat "$d/delay")"; fi
		if [ ! -r "$d/tags" ]; then
			echo "fatal: repository not found (faux git)" >&2
			exit 128
		fi
		if [ "$cmd" = clone ]; then mkdir -p "$last" && : > "$last/HEAD"; fi
		exit 0 ;;
	remote) exit 0 ;;
	for-each-ref) cat "$d/tags" 2>/dev/null; exit 0 ;;
	show)
		if [ -r "$d/news" ]; then cat "$d/news"; exit 0; fi
		echo "fatal: path 'NEWS' does not exist (faux git)" >&2
		exit 128 ;;
esac
exit 1
```

- [ ] **Step 2 : aides Python** — dans `tests/gui/gtktest.py`, remplacer `fake_git` par :

```python
    def fake_git(self, tags=None, news=None, delay=0):
        """Réponses du faux git pour les appels suivants : tags servis (liste ; None : clone et fetch échouent, code 128), texte de NEWS
        (None : absent), délai en secondes avant de répondre à clone et fetch."""
        for nom, val in (('tags', '\n'.join(tags) + '\n' if tags is not None else None), ('news', news), ('delay', str(delay) if delay else None)):
            p = os.path.join(self.fakegit_dir, nom)
            if val is None:
                if os.path.exists(p):
                    os.remove(p)
            else:
                with open(p, 'w', encoding='utf-8') as f:
                    f.write(val)
```

  et ajouter, après `local_revision()` :

```python
def local_describe():
    """GIT_DESCRIBE de la construction testée (git_describe.h) ; « unknown » si introuvable."""
    try:
        with open(os.environ.get('GTKTERM_GIT_DESCRIBE_H', ''), encoding='utf-8') as f:
            m = re.search(r'#define GIT_DESCRIBE "([^"]*)"', f.read())
        return m.group(1) if m else 'unknown'
    except OSError:
        return 'unknown'
```

  `grep -rn "fake_git(" tests/gui/` : seul `test_update_window.py` l'appelle (vérifier ; adapter tout autre appel au nouveau format).

- [ ] **Step 3 : réécrire le scénario `tests/gui/test_update_window.py`**

```python
# Fenêtre « Updates » (mise à jour par versions taguées) : versions servies par le faux git, ouverture au démarrage seulement pour une version
# plus récente non ignorée, notes de NEWS (avertissements en tête, retour en arrière), bouton principal selon le sens, confirmation quand il y a des
# avertissements, « Skip this version » enregistré, script lancé avec le tag, échec de la source, fermeture pendant une actualisation.
from gtktest import *

DESCRIBE = local_describe()
URL = "file:///srv/dépôt l'essai/gtkterm.git"
PREFIX = '/opt/gtkterm essai'
NOUVELLE, ANCIENNE = 'v99.0.0', 'v0.0.1'
TAGS = [ANCIENNE, 'essai', NOUVELLE]
NEWS = ('# Nouveautés\n\n## v99.0.0 — 2099-01-01\n- Nouveauté lointaine\n! Avertissement lointain\n  sur deux lignes\n\n'
        '## v0.0.1 — 2000-01-01\n- Toute première\n')

e = Env('update_window')
check(DESCRIBE != 'unknown', 'GIT_DESCRIBE construit connu', DESCRIBE)
e.write_config(e.general(update_check_startup='true', update_prefix=PREFIX) + e.profile())


def rc():
    return e.read_config()


def git_calls(cmd):
    return [a for a in e.fake_calls('git') if cmd in a]


def resultat():
    return e.find1(name='update-result').get('label') or ''


def libelle(name):
    w = e.find(name=name)
    return (w[0].get('label') or '') if w else None


def notes(css=None):
    """Libellés visibles de la fenêtre « Updates » (dont ceux du panneau de notes), filtrés par classe CSS."""
    nat = fenetre_native()
    return [w.get('label') or '' for w in e.dump() if w.get('type') == 'GtkLabel' and w.get('mapped') and w.get('native') == nat and w.get('label')
            and (css is None or css in (w.get('css') or []))]


def fenetre_native():
    f = e.find(name='update-window')
    return f[0]['native'] if f else None


def fenetre_ouverte():
    return bool(e.find(name='update-window'))


def fenetre_fermee():
    return wait_until(lambda: not fenetre_ouverte(), 5)


def choisir(texte):
    e.set_text('update-version', texte)


# 1. Démarrage : version plus récente → la fenêtre s'ouvre seule, plus récente choisie, avertissements en tête
e.fake_git(tags=TAGS, news=NEWS)
e.start()
check(wait_until(fenetre_ouverte, 10), 'fenetre ouverte au demarrage')
clones = git_calls('clone')
check(bool(clones) and clones[0][-3:-1] == ['--', 'https://invalid.example/none.git'] and '--filter=tree:0' in clones[0] and '--bare' in clones[0], 'clone nu filtre de l URL', clones)
check(wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 5), 'bouton : Update to', libelle('update-install'))
avert = notes('warning')
check(any('Avertissement lointain sur deux lignes' in t for t in avert), 'avertissement de NEWS affiche (prolongement joint)', avert)
check(any(t.startswith('v99.0.0 — 2099-01-01') for t in notes('heading')), 'titre de section', notes('heading'))
check(e.find(name='update-skip') != [], 'Skip visible pour une version plus recente')
check(NOUVELLE in resultat(), 'resultat : nouvelle version', resultat())

# 2. « Skip this version » : tag enregistré, fenêtre fermée
e.activate('update-skip')
check(fenetre_fermee(), 'fenetre fermee par Skip')
check(wait_until(lambda: 'update_skipped_version=%s' % NOUVELLE in rc(), 5), 'version ignoree enregistree', rc())

# 3. Redémarrage : version ignorée → aucune fenêtre au démarrage
e.stop()
n = len(git_calls('fetch'))
e.start()
check(wait_until(lambda: len(git_calls('fetch')) > n, 10), 'actualisation au demarrage (cache present : fetch)')
time.sleep(1.5)
check(not fenetre_ouverte(), 'version ignoree : pas de fenetre au demarrage')

# 4. Ouverture à la main : actualisation automatique ; retour en arrière vers la plus ancienne
avant = rc()
e.action('updates')
check(wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 10), 'fenetre ouverte a la main, liste chargee', libelle('update-install'))
choisir(ANCIENNE)
check(wait_until(lambda: libelle('update-install') == 'Roll back to ' + ANCIENNE, 5), 'bouton : Roll back to', libelle('update-install'))
check(any('You are leaving these versions:' in t for t in notes()), 'en-tete du retour en arriere', notes())
check(not e.find(name='update-skip'), 'Skip masque pour un retour en arriere')
check(rc() == avant, 'ouvrir la fenetre n ecrit rien')

# 5. Lancement : retour sans avertissement (aucune section entre v0.0.1 et la version installée) → script lancé directement, avec le tag
if not check(os.access(os.path.join(FAKEBIN, 'xterm'), os.X_OK), 'faux xterm en place : aucun vrai terminal ne sera lance'):
    e.finish()
    sys.exit(conclude())
e.set_text('update-url', URL)
e.activate('update-check')
check(wait_until(lambda: libelle('update-install') is not None and NOUVELLE in resultat(), 10), 'actualisation sur l URL affichee', resultat())
check('update_url=%s' % URL in rc(), 'URL affichee enregistree')
check(git_calls('set-url')[-1][-2:] == ['--', URL], 'remote set-url -- <url> en un seul argument', git_calls('set-url')[-1:])
choisir(ANCIENNE)
wait_until(lambda: libelle('update-install') == 'Roll back to ' + ANCIENNE, 5)
e.activate('update-install')
check(bool(wait_until(lambda: e.fake_calls('xterm'), 5)), 'terminal lance sans confirmation')
args = (e.fake_calls('xterm') or [[]])[-1]
check(len(args) >= 8 and args[-8:-6] == ['sh', '-c'], 'commande sh -c en tete', args)
check(args[-5].endswith('/gtkterm-update.sh') and args[-4] == URL and args[-3] == PREFIX and args[-2].isdigit() and args[-1] == ANCIENNE, 'arguments du script intacts', args)
check(wait_until(lambda: any('The update is running in a terminal window.' in t for t in e.alerts()), 5), 'message de lancement', e.alerts())
e.close_alert()

# 6. Montée avec avertissements : confirmation ; Annuler ne lance rien, Installer lance avec le tag
choisir(NOUVELLE + ' — latest')
wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 5)
n = len(e.fake_calls('xterm'))
e.activate('update-install')
check(wait_until(lambda: any('Install %s?' % NOUVELLE in t for t in e.alerts()), 5), 'confirmation demandee', e.alerts())
e.close_alert('Cancel')
time.sleep(0.5)
check(len(e.fake_calls('xterm')) == n, 'Annuler : rien n est lance')
e.activate('update-install')
check(wait_until(lambda: any('Install %s?' % NOUVELLE in t for t in e.alerts()), 5), 'confirmation redemandee')
e.close_alert('Install')
check(wait_until(lambda: len(e.fake_calls('xterm')) > n, 5), 'Installer : terminal lance')
check(e.fake_calls('xterm')[-1][-1] == NOUVELLE, 'tag de la version choisie', e.fake_calls('xterm')[-1][-1:])
wait_until(lambda: any('The update is running in a terminal window.' in t for t in e.alerts()), 5)
e.close_alert()

# 7. Source injoignable : message dans la fenêtre, bouton réactivé
e.fake_git(tags=None)
e.activate('update-check')
check(wait_until(lambda: 'Could not check for updates' in resultat(), 10), 'echec de la source affiche', resultat())

# 8. Case « au démarrage » : enregistrée à chaque changement
e.activate('update-startup')
check(wait_until(lambda: 'update_check_startup=false' in rc(), 5), 'case decochee enregistree', rc())
e.activate('update-startup')
check(wait_until(lambda: 'update_check_startup=true' in rc(), 5), 'case recochee enregistree', rc())

# 9. Fermeture pendant une actualisation : annulée, sans effet ni erreur (stderr vérifié par finish)
e.fake_git(tags=TAGS, news=NEWS, delay=3)
n = len(git_calls('fetch'))
e.activate('update-check')
check(wait_until(lambda: len(git_calls('fetch')) > n, 5), 'actualisation lancee')
e.activate('update-close')
check(fenetre_fermee(), 'fenetre fermee pendant l actualisation')
time.sleep(4)
check(not fenetre_ouverte() and not e.alerts(), 'actualisation annulee : ni fenetre recreee ni boite', e.alerts())

# 10. Fermeture de GTKTerm (app.quit) fenêtre ouverte : code 0, stderr propre (vérifiés par finish)
e.fake_git(tags=TAGS, news=NEWS)
e.action('updates')
check(wait_until(fenetre_ouverte, 5), 'fenetre ouverte avant la fermeture')
e.finish()
sys.exit(conclude())
```

  Le libellé exact d'une entrée de la liste (`v99.0.0 — latest`) dépend de `DESCRIBE` uniquement pour la mention « installed » : ni `v99.0.0` ni `v0.0.1` ne sont la version installée.

- [ ] **Step 4 : lancer le scénario, vérifier qu'il échoue** — Run : `meson test -C build --suite gui test_update_window -v 2>&1 | tail -20` (construction en échec depuis B3, ou fenêtre
  ancienne) ; Expected : échec.

- [ ] **Step 5 : `src/ui/update_window.h`**

```c
/* Fenêtre « Updates » (action app.updates) : versions publiées (tags vX.Y.Z), notes de NEWS, installation d'une version au choix (retour en
 * arrière compris) ; vérification au démarrage qui ouvre cette fenêtre quand une version plus récente existe et n'est pas ignorée. */

#ifndef GT_UI_UPDATE_WINDOW_H_
#define GT_UI_UPDATE_WINDOW_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GT_TYPE_UPDATE_WINDOW (gt_update_window_get_type())
G_DECLARE_FINAL_TYPE(GtUpdateWindow, gt_update_window, GT, UPDATE_WINDOW, GtkWindow)

/* Une seule instance, transitoire de parent ; créée puis actualisée (settings.update_url), ou présentée de nouveau si elle est ouverte. */
void gt_update_window_show(GtkWindow *parent);
/* À appeler depuis activate : diffère (g_idle_add) puis actualise ; silencieuse sauf version plus récente et non ignorée (la fenêtre s'ouvre). */
void gt_update_startup_check(GtkWindow *main_window);
/* Annule l'actualisation de démarrage ; toute confirmation encore ouverte est ignorée (app_stop_services, à la fermeture). */
void gt_update_shutdown(void);

G_END_DECLS

#endif /* GT_UI_UPDATE_WINDOW_H_ */
```

- [ ] **Step 6 : réécrire `src/ui/update_window.c`**

```c
/* Fenêtre « Updates » et vérification au démarrage (mise à jour par versions taguées) : voir update_window.h. Logique dans update_versions.c,
 * update_fetch.c et update_check.c (terminal) ; ici, seulement l'interface. */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include "update_window.h"
#include "app.h"
#include "app_config.h"
#include "dialogs.h"
#include "git_describe.h"
#include "git_revision.h"
#include "update_check.h"
#include "update_fetch.h"
#include "update_versions.h"

struct _GtUpdateWindow
{
	GtkWindow parent_instance;
	GtkWidget *url;	/* update-url */
	GtkWidget *startup;	/* update-startup */
	GtkWidget *check;	/* update-check */
	GtkWidget *versions;	/* update-version : GtkDropDown sur un GtkStringList de libellés, même ordre que releases->tags */
	GtkWidget *notes;	/* update-notes : GtkBox vertical de GtkLabel */
	GtkWidget *result;	/* update-result */
	GtkWidget *skip;	/* update-skip */
	GtkWidget *install;	/* update-install */
	GtReleases *releases;	/* dernière actualisation réussie ; NULL avant */
	GPtrArray *sections;	/* gt_news_parse(releases->news) */
	GCancellable *cancellable;	/* actualisation en cours ; annulée à la fermeture ou par une nouvelle actualisation */
};

G_DEFINE_FINAL_TYPE(GtUpdateWindow, gt_update_window, GTK_TYPE_WINDOW)

static GtUpdateWindow *instance;	/* pointeur faible : la fenêtre ouverte */
static GtkWindow *startup_window;	/* pointeur faible : fenêtre principale, parent de l'ouverture au démarrage */
static GCancellable *startup_cancellable;
static gboolean shutting_down;	/* posé par gt_update_shutdown() : plus d'ouverture au démarrage, confirmation tardive ignorée */

static void
installed_version(GtVersion *v)
{
	gt_version_installed(GIT_DESCRIBE, VERSION, v);
}

/* ---- lancement du script ------------------------------------------- */

static void
run_update_script(GtkWindow *parent, const gchar *tag)
{
	gchar *terminal = gt_update_find_terminal();
	gchar **argv = NULL;
	GError *error = NULL;

	/* Arguments en liste (M34) : g_spawn_async, aucun shell n'interprète l'URL, le préfixe ni le tag (refusé s'il n'est pas vX.Y.Z) */
	if(terminal != NULL)
		argv = gt_update_terminal_argv(terminal, PACKAGE_DATA_DIR "/gtkterm-update.sh", settings.update_url, settings.update_prefix, getpid(), tag);
	g_free(terminal);
	if(argv == NULL)
	{
		gt_ui_message(parent, GT_APP_MSG_ERROR, _("No terminal emulator found. Please install xterm or similar."));
		return;
	}
	if(!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error))
	{
		gchar *text = g_strdup_printf(_("Failed to launch update terminal:\n%s"), error->message);

		gt_ui_message(parent, GT_APP_MSG_ERROR, text);
		g_free(text);
		g_clear_error(&error);
	}
	else
		gt_ui_message(parent, GT_APP_MSG_INFO, _("The update is running in a terminal window.\nFollow the instructions there and restart GTKTerm when done."));
	g_strfreev(argv);
}

/* ---- notes ---------------------------------------------------------- */

static void
notes_clear(GtUpdateWindow *self)
{
	GtkWidget *child;

	while((child = gtk_widget_get_first_child(self->notes)) != NULL)
		gtk_box_remove(GTK_BOX(self->notes), child);
}

/* Texte de NEWS tel quel : jamais de balisage Pango */
static void
notes_add(GtUpdateWindow *self, const gchar *text, const gchar *css_class, gint indent)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_selectable(GTK_LABEL(l), TRUE);
	gtk_widget_set_margin_start(l, indent);
	if(css_class != NULL)
		gtk_widget_add_css_class(l, css_class);
	gtk_box_append(GTK_BOX(self->notes), l);
}

static const gchar *
selected_tag(GtUpdateWindow *self)
{
	guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(self->versions));

	if(self->releases == NULL || i == GTK_INVALID_LIST_POSITION || i >= self->releases->tags->len)
		return NULL;
	return g_ptr_array_index(self->releases->tags, i);
}

/* Sections retenues pour tag (pointeurs empruntés) ; NULL sans sélection */
static GPtrArray *
selected_sections(GtUpdateWindow *self, const gchar *tag, GtNewsDirection *dir)
{
	GtVersion installed, target;

	if(tag == NULL || self->releases == NULL || !gt_version_parse(tag, &target))
		return NULL;
	installed_version(&installed);
	return gt_news_select(self->sections, self->releases->tags, &installed, &target, dir);
}

static void
refresh_selection(GtUpdateWindow *self)
{
	const gchar *tag = selected_tag(self);
	GtNewsDirection dir = GT_NEWS_SAME;
	GPtrArray *sel = selected_sections(self, tag, &dir);
	gchar *label;

	notes_clear(self);
	if(sel == NULL)
	{
		gtk_widget_set_sensitive(self->install, FALSE);
		gtk_widget_set_visible(self->skip, FALSE);
		return;
	}
	if(dir == GT_NEWS_SAME)
		notes_add(self, _("This version is installed."), NULL, 0);
	if(gt_news_has_warning(sel))
	{
		notes_add(self, _("Warnings"), "warning", 0);
		for(guint i = 0; i < sel->len; i++)
		{
			GtNewsSection *s = g_ptr_array_index(sel, i);

			for(guint j = 0; j < s->entries->len; j++)
			{
				GtNewsEntry *e = g_ptr_array_index(s->entries, j);

				if(e->kind == GT_NEWS_WARNING)
				{
					gchar *t = g_strdup_printf("%s : %s", s->tag, e->text);

					notes_add(self, t, "warning", 12);
					g_free(t);
				}
			}
		}
	}
	if(dir == GT_NEWS_ROLLBACK && sel->len > 0)
		notes_add(self, _("You are leaving these versions:"), "heading", 0);
	for(guint i = 0; i < sel->len; i++)
	{
		GtNewsSection *s = g_ptr_array_index(sel, i);

		notes_add(self, s->title, "heading", 0);
		for(guint j = 0; j < s->entries->len; j++)
		{
			GtNewsEntry *e = g_ptr_array_index(s->entries, j);
			gchar *t = e->kind == GT_NEWS_ITEM ? g_strconcat("• ", e->text, NULL) : e->kind == GT_NEWS_WARNING ? g_strconcat("⚠ ", e->text, NULL) : g_strdup(e->text);

			notes_add(self, t, e->kind == GT_NEWS_WARNING ? "warning" : NULL, 12);
			g_free(t);
		}
	}
	if(dir == GT_NEWS_UPGRADE)
		label = g_strdup_printf(_("Update to %s"), tag);
	else if(dir == GT_NEWS_ROLLBACK)
		label = g_strdup_printf(_("Roll back to %s"), tag);
	else
		label = g_strdup_printf(_("Reinstall %s"), tag);
	gtk_button_set_label(GTK_BUTTON(self->install), label);
	g_free(label);
	gtk_widget_set_sensitive(self->install, TRUE);
	gtk_widget_set_visible(self->skip, dir == GT_NEWS_UPGRADE);
	g_ptr_array_unref(sel);
}

static void
set_result(GtUpdateWindow *self, const gchar *text)
{
	gtk_label_set_text(GTK_LABEL(self->result), text);
}

static void
set_result_for(GtUpdateWindow *self, GtReleases *r)
{
	GtVersion installed, newest;

	installed_version(&installed);
	if(r->tags->len == 0)
		set_result(self, _("No published version found."));
	else if(gt_version_parse(g_ptr_array_index(r->tags, 0), &newest) && gt_version_compare(&newest, &installed) > 0)
	{
		gchar *t = g_strdup_printf(_("A new version is available (%s)."), (const gchar *)g_ptr_array_index(r->tags, 0));

		set_result(self, t);
		g_free(t);
	}
	else
		set_result(self, _("GTKTerm is up to date."));
}

/* Prend possession de r */
static void
set_releases(GtUpdateWindow *self, GtReleases *r)
{
	GtkStringList *labels = gtk_string_list_new(NULL);
	GtVersion installed;

	gt_releases_free(self->releases);
	g_clear_pointer(&self->sections, g_ptr_array_unref);
	self->releases = r;
	self->sections = gt_news_parse(r->news);
	installed_version(&installed);
	for(guint i = 0; i < r->tags->len; i++)
	{
		const gchar *tag = g_ptr_array_index(r->tags, i);
		GtVersion v;
		gboolean latest = i == 0, inst;
		gchar *text;

		gt_version_parse(tag, &v);
		inst = gt_version_compare(&v, &installed) == 0;
		if(latest && inst)
			text = g_strdup_printf(_("%s — latest, installed"), tag);
		else if(latest)
			text = g_strdup_printf(_("%s — latest"), tag);
		else if(inst)
			text = g_strdup_printf(_("%s — installed"), tag);
		else
			text = g_strdup(tag);
		gtk_string_list_append(labels, text);
		g_free(text);
	}
	gtk_drop_down_set_model(GTK_DROP_DOWN(self->versions), G_LIST_MODEL(labels));
	g_object_unref(labels);
	gtk_widget_set_sensitive(self->versions, r->tags->len > 0);
	if(r->tags->len > 0)
		gtk_drop_down_set_selected(GTK_DROP_DOWN(self->versions), 0);
	refresh_selection(self);
}

/* ---- actualisation -------------------------------------------------- */

typedef struct
{
	GtUpdateWindow *self;	/* référence forte : les widgets ne sont touchés que si l'actualisation n'est pas annulée */
	GCancellable *cancellable;	/* celui de cette actualisation */
} CheckData;

static void
cancel_check(GtUpdateWindow *self)
{
	if(self->cancellable != NULL)
	{
		g_cancellable_cancel(self->cancellable);
		g_clear_object(&self->cancellable);
	}
}

static void
on_check_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	CheckData *d = user_data;
	GtUpdateWindow *self = d->self;
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre fermée (close-request/dispose ont annulé) ou actualisation remplacée : rien à afficher */
	if(!g_cancellable_is_cancelled(d->cancellable))
	{
		gtk_widget_set_sensitive(self->check, TRUE);
		g_clear_object(&self->cancellable);
		if(r == NULL)
		{
			gchar *text = g_strdup_printf(_("Could not check for updates: %s"), error->message);

			set_result(self, text);
			g_free(text);
		}
		else
		{
			set_result_for(self, r);
			set_releases(self, r);
			r = NULL;
		}
	}
	gt_releases_free(r);
	g_clear_error(&error);
	g_object_unref(d->cancellable);
	g_object_unref(d->self);
	g_free(d);
}

/* « Check now », Entrée dans le champ, ouverture : actualise l'URL AFFICHÉE, après l'avoir enregistrée */
static void
start_check(GtUpdateWindow *self)
{
	gchar *url = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->url))));
	gchar *cache;
	CheckData *d;

	if(*url == '\0')
	{
		set_result(self, _("Please enter the URL of the source repository."));
		g_free(url);
		return;
	}
	config_save_update_url(url);	/* n'écrit que si elle a changé ; le script lancé ensuite reçoit settings.update_url = cette URL */
	cancel_check(self);
	self->cancellable = g_cancellable_new();
	gtk_widget_set_sensitive(self->check, FALSE);
	set_result(self, _("Checking…"));
	d = g_new0(CheckData, 1);
	d->self = g_object_ref(self);
	d->cancellable = g_object_ref(self->cancellable);
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(url, cache, self->cancellable, on_check_done, d);
	g_free(cache);
	g_free(url);
}

/* ---- boutons -------------------------------------------------------- */

typedef struct
{
	GtUpdateWindow *self;	/* pointeur faible */
	gchar *tag;
} InstallData;

static void
on_install_confirmed(gboolean accepted, gpointer user_data)
{
	InstallData *d = user_data;

	if(accepted && !shutting_down && d->self != NULL)
		run_update_script(GTK_WINDOW(d->self), d->tag);
	if(d->self != NULL)
		g_object_remove_weak_pointer(G_OBJECT(d->self), (gpointer *)&d->self);
	g_free(d->tag);
	g_free(d);
}

static void
on_install_clicked(GtUpdateWindow *self)
{
	const gchar *tag = selected_tag(self);
	GPtrArray *sel = selected_sections(self, tag, NULL);
	gboolean warn = gt_news_has_warning(sel);
	InstallData *d;
	gchar *question;

	if(sel != NULL)
		g_ptr_array_unref(sel);
	if(tag == NULL)
		return;
	if(!warn)
	{
		run_update_script(GTK_WINDOW(self), tag);
		return;
	}
	/* Avertissements : confirmation (non bloquante) avant de lancer le script */
	d = g_new0(InstallData, 1);
	d->self = self;
	g_object_add_weak_pointer(G_OBJECT(self), (gpointer *)&d->self);
	d->tag = g_strdup(tag);
	question = g_strdup_printf(_("Install %s?"), tag);
	gt_ui_confirm(GTK_WINDOW(self), question, _("See the warnings above."), _("Install"), on_install_confirmed, d);
	g_free(question);
}

static void
on_skip_clicked(GtUpdateWindow *self)
{
	const gchar *tag = selected_tag(self);

	if(tag == NULL)
		return;
	/* « Skip this version » survit au redémarrage */
	config_save_skipped_version(tag);
	gtk_window_close(GTK_WINDOW(self));
}

static void
on_startup_toggled(GtkCheckButton *button, gpointer user_data)
{
	config_save_update_check_startup(gtk_check_button_get_active(button));
}

static void
on_version_selected(GObject *dropdown, GParamSpec *pspec, gpointer user_data)
{
	refresh_selection(GT_UPDATE_WINDOW(user_data));
}

/* ---- classe --------------------------------------------------------- */

static gboolean
gt_update_window_close_request(GtkWindow *window)
{
	cancel_check(GT_UPDATE_WINDOW(window));
	return FALSE;	/* fermeture acceptée (hide-on-close FALSE : la fenêtre est détruite) */
}

static void
gt_update_window_dispose(GObject *object)
{
	GtUpdateWindow *self = GT_UPDATE_WINDOW(object);

	cancel_check(self);
	gt_releases_free(self->releases);
	self->releases = NULL;
	g_clear_pointer(&self->sections, g_ptr_array_unref);
	/* Le pointeur faible ne tombe qu'à la finalisation, retardée par la référence de CheckData jusqu'au rappel annulé : une fenêtre détruite
	 * ne doit plus être présentée par gt_update_window_show */
	if(instance == self)
	{
		g_object_remove_weak_pointer(object, (gpointer *)&instance);
		instance = NULL;
	}
	G_OBJECT_CLASS(gt_update_window_parent_class)->dispose(object);
}

static void
gt_update_window_class_init(GtUpdateWindowClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = gt_update_window_dispose;
	GTK_WINDOW_CLASS(klass)->close_request = gt_update_window_close_request;
}

static GtkWidget *
row_label(const gchar *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	return l;
}

static void
gt_update_window_init(GtUpdateWindow *self)
{
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *installed, *row, *scroll, *buttons, *spacer, *close;
	gchar *installed_text;

	gtk_window_set_title(GTK_WINDOW(self), _("Updates"));
	gtk_window_set_default_size(GTK_WINDOW(self), 640, 520);
	gtk_widget_set_name(GTK_WIDGET(self), "update-window");
	gt_ui_close_on_escape(GTK_WINDOW(self));	/* dialogs.c : window.close → close_request */

	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_widget_set_margin_top(grid, 12);
	gtk_widget_set_margin_bottom(grid, 12);
	gtk_widget_set_margin_start(grid, 12);
	gtk_widget_set_margin_end(grid, 12);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Installed version:")), 0, 0, 1, 1);
	installed_text = g_strdup_printf("%s (%.7s)", GIT_DESCRIBE, GIT_REVISION);
	installed = row_label(installed_text);
	g_free(installed_text);
	gtk_widget_set_name(installed, "update-installed");
	gtk_label_set_selectable(GTK_LABEL(installed), TRUE);
	gtk_grid_attach(GTK_GRID(grid), installed, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Update source:")), 0, 1, 1, 1);
	self->url = gtk_entry_new();
	gtk_widget_set_name(self->url, "update-url");
	gtk_widget_set_hexpand(self->url, TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(self->url), 50);
	gtk_editable_set_text(GTK_EDITABLE(self->url), settings.update_url != NULL ? settings.update_url : "");
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->url), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Update source"), -1);
	gtk_grid_attach(GTK_GRID(grid), self->url, 1, 1, 1, 1);

	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	self->startup = gtk_check_button_new_with_label(_("Check for updates at startup"));
	gtk_widget_set_name(self->startup, "update-startup");
	gtk_widget_set_hexpand(self->startup, TRUE);
	/* État posé avant le branchement du signal : ouvrir la fenêtre n'écrit rien */
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->startup), settings.update_check_startup);
	self->check = gtk_button_new_with_label(_("Check now"));
	gtk_widget_set_name(self->check, "update-check");
	gtk_box_append(GTK_BOX(row), self->startup);
	gtk_box_append(GTK_BOX(row), self->check);
	gtk_grid_attach(GTK_GRID(grid), row, 0, 2, 2, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Version:")), 0, 3, 1, 1);
	self->versions = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL);
	gtk_widget_set_name(self->versions, "update-version");
	gtk_widget_set_sensitive(self->versions, FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->versions), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Version"), -1);
	gtk_grid_attach(GTK_GRID(grid), self->versions, 1, 3, 1, 1);

	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 220);
	gtk_widget_set_vexpand(scroll, TRUE);
	self->notes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_name(self->notes, "update-notes");
	gtk_widget_set_margin_start(self->notes, 6);
	gtk_widget_set_margin_end(self->notes, 6);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->notes);
	gtk_grid_attach(GTK_GRID(grid), scroll, 0, 4, 2, 1);

	self->result = row_label("");
	gtk_widget_set_name(self->result, "update-result");
	gtk_label_set_wrap(GTK_LABEL(self->result), TRUE);
	gtk_grid_attach(GTK_GRID(grid), self->result, 0, 5, 2, 1);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	self->skip = gtk_button_new_with_label(_("Skip this version"));
	gtk_widget_set_name(self->skip, "update-skip");
	gtk_widget_set_visible(self->skip, FALSE);
	spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(spacer, TRUE);
	self->install = gtk_button_new_with_label(_("Install"));
	gtk_widget_set_name(self->install, "update-install");
	gtk_widget_add_css_class(self->install, "suggested-action");
	gtk_widget_set_sensitive(self->install, FALSE);
	close = gtk_button_new_with_label(_("Close"));
	gtk_widget_set_name(close, "update-close");
	gtk_box_append(GTK_BOX(buttons), self->skip);
	gtk_box_append(GTK_BOX(buttons), spacer);
	gtk_box_append(GTK_BOX(buttons), self->install);
	gtk_box_append(GTK_BOX(buttons), close);
	gtk_grid_attach(GTK_GRID(grid), buttons, 0, 6, 2, 1);

	gtk_window_set_child(GTK_WINDOW(self), grid);

	g_signal_connect_swapped(self->check, "clicked", G_CALLBACK(start_check), self);
	g_signal_connect_swapped(self->url, "activate", G_CALLBACK(start_check), self);
	g_signal_connect(self->startup, "toggled", G_CALLBACK(on_startup_toggled), NULL);
	g_signal_connect(self->versions, "notify::selected", G_CALLBACK(on_version_selected), self);
	g_signal_connect_swapped(self->skip, "clicked", G_CALLBACK(on_skip_clicked), self);
	g_signal_connect_swapped(self->install, "clicked", G_CALLBACK(on_install_clicked), self);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_close), self);
}

static GtUpdateWindow *
window_new(GtkWindow *parent)
{
	instance = g_object_new(GT_TYPE_UPDATE_WINDOW, "transient-for", parent, "destroy-with-parent", TRUE, NULL);
	g_object_add_weak_pointer(G_OBJECT(instance), (gpointer *)&instance);
	return instance;
}

void
gt_update_window_show(GtkWindow *parent)
{
	if(instance == NULL)
		start_check(window_new(parent));
	gtk_window_present(GTK_WINDOW(instance));
}

/* ---- vérification au démarrage ------------------------------------- */

static void
on_startup_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GCancellable *c = user_data;
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre déjà ouverte à la main : elle actualise elle-même */
	if(r != NULL && !g_cancellable_is_cancelled(c) && !shutting_down && startup_window != NULL && instance == NULL)
	{
		GtVersion installed;
		const gchar *newest = r->tags->len > 0 ? g_ptr_array_index(r->tags, 0) : NULL;

		installed_version(&installed);
		/* Silencieuse sauf version plus récente et non ignorée */
		if(gt_release_verdict(&installed, newest, settings.update_skipped_version) == GT_RELEASE_AVAILABLE)
		{
			GtUpdateWindow *w = window_new(startup_window);

			set_result_for(w, r);
			set_releases(w, r);
			r = NULL;
			gtk_window_present(GTK_WINDOW(w));
		}
	}
	if(error != NULL)
		g_debug("update check: %s", error->message);
	g_clear_error(&error);
	gt_releases_free(r);
	if(startup_cancellable == c)
		g_clear_object(&startup_cancellable);
	g_object_unref(c);
}

static gboolean
startup_check_idle(gpointer user_data)
{
	gchar *cache;

	if(startup_window == NULL || !settings.update_check_startup || shutting_down)
		return G_SOURCE_REMOVE;
	startup_cancellable = g_cancellable_new();
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(settings.update_url, cache, startup_cancellable, on_startup_fetched, g_object_ref(startup_cancellable));
	g_free(cache);
	return G_SOURCE_REMOVE;
}

void
gt_update_startup_check(GtkWindow *main_window)
{
	g_return_if_fail(main_window != NULL && startup_window == NULL);
	startup_window = main_window;
	g_object_add_weak_pointer(G_OBJECT(main_window), (gpointer *)&startup_window);
	g_idle_add(startup_check_idle, NULL);
}

void
gt_update_shutdown(void)
{
	if(startup_cancellable != NULL)
		g_cancellable_cancel(startup_cancellable);
	shutting_down = TRUE;	/* confirmations ouvertes : fermées avec les fenêtres, leur réponse tardive est ignorée */
	if(startup_window != NULL)
	{
		g_object_remove_weak_pointer(G_OBJECT(startup_window), (gpointer *)&startup_window);
		startup_window = NULL;
	}
}
```

  Points à vérifier pendant l'écriture : `VERSION` est défini par `config.h` (`conf.set_quoted('VERSION', …)` du `meson.build` racine : oui) ; `gt_ui_confirm` appelle toujours son rappel
  (même fenêtre détruite : `accepted` FALSE) — lire `src/ui/dialogs.c` pour le confirmer, sinon le pointeur faible de `InstallData` suffit à éviter tout accès, mais la structure fuirait :
  dans ce cas, libérer via `g_object_set_data_full` sur la fenêtre de confirmation. `grep -rn gt_update_offer src/` doit être vide.

- [ ] **Step 7 : traductions** — `ninja -C build gtkterm-pot gtkterm-update-po`, puis dans `po/fr.po` traduire les nouveaux msgid avec la table de la Task A4 Step 7, plus :
  `Version` → `Version`. Retirer de `po/fr.po` les entrées devenues obsolètes (`#~`) seulement si `gtkterm-update-po` les a marquées ainsi (ne rien retirer à la main d'autre).

- [ ] **Step 8 : construire et tester**
  Run : `ninja -C build 2>&1 | grep -E "warning|error"; meson test -C build --suite unit && meson test -C build --suite gui test_update_window test_about test_menus_actions test_quit_etat`
  Expected : construction sans avertissement nouveau ; tous OK. Puis la suite graphique complète : `meson test -C build --suite gui` (jamais deux suites gui à la fois) ; Expected : OK
  (SKIP 77 toléré pour un plantage interne de `gtk4-broadwayd`, voir `tests/README.md`).

- [ ] **Step 9 : commit (B3 + B4)**

```bash
git add src/update_check.h src/update_check.c tests/test_update_check.c src/ui/update_window.h src/ui/update_window.c tests/gui/bin/git tests/gui/gtktest.py tests/gui/test_update_window.py po/
git commit -m "mise à jour : fenêtre « Updates » GTK 4 par versions taguées (liste, nouveautés et avertissements, retour en arrière, confirmation), tag passé au script

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task B5 : version 2.0.0, `NEWS`, documentation

**Files :**
- Modify : `meson.build:4` (`version: '2.0.0'`), `NEWS`, `CLAUDE.md` (paragraphe « Self-update » et mentions de `update_skipped_revision`), `tests/README.md` (faux git de la suite gui,
  nouveaux tests), `README*` s'il décrit la mise à jour (`grep -ril "mise à jour\|update" README*`)

- [ ] **Step 1 : `meson.build`** — `version: '1.3.2',` → `version: '2.0.0',`.

- [ ] **Step 2 : vérifier les avertissements avant de les écrire** — contre le code de `v1.3.2` (`git show v1.3.2:<fichier>`) :
  - `.gtktermrc` : `v1.3.2` lit-elle un fichier converti (`[profile:NAME]`, `[general]`) ? (`git show v1.3.2:src/term_config.c | grep -n "cfgParse\|kf_sections"`) ;
  - fichiers de macros : `v1.3.2` lit-elle `[gtkterm-macros] version=2` / `[macro:N]` ? (`git show v1.3.2:src/macros.c | grep -n "\[macros\]\|gtkterm-macros\|g_key_file"`) ;
  - lanceur : nom du `.desktop` de chaque version.
  N'écrire que ce qui est vérifié ; adapter les phrases ci-dessous au résultat.

- [ ] **Step 3 : section `v2.0.0` en tête de `NEWS`** (au-dessus de `## v1.3.2`) :

```
## v2.0.0 — 2026-10-20
- Interface GTK 4.
- Onglets : un port par onglet, chacun avec sa liaison, ses options, son profil, ses macros et son script Lua.
- Projets : un fichier .gtkterm enregistre tous les onglets (port, réglages, macros, script) et se rouvre au démarrage.
- Macros : valeurs par défaut des arguments, éditeur réorganisé, protection contre l'écrasement d'un fichier modifié ailleurs.
- Menus réorganisés ; les commandes de GTKTerm passent par Ctrl+Maj, Ctrl+lettre va au port.
! GTKTerm 2 demande GTK 4 (Ubuntu 24.04 ou plus récent) : la mise à jour installe les paquets nécessaires.
! Au premier lancement, ~/.config/.gtktermrc est converti au nouveau format (copie gardée : .gtktermrc.avant-migration).
  v1.3.2 ne lit pas le fichier converti : pour revenir à v1.3.2 avec vos réglages, remettez la copie à sa place.
! Les fichiers de macros (.ini) sont réécrits au nouveau format au premier enregistrement (copie : <fichier>.avant-migration) ;
  v1.3.2 ne les relit pas : en cas de retour, remettez la copie.
! Le lanceur devient eu.mulagabriel.gtkterm.desktop : un raccourci épinglé dans le dock est à épingler de nouveau.
```

  (La date est celle du jour où `v2.0.0` sera posé, Task B6 : la corriger alors.)

- [ ] **Step 4 : `CLAUDE.md`** — réécrire le paragraphe **Self-update** :

```
**Self-update (`update_versions.c` + `update_fetch.c` + `update_check.c` + `src/ui/update_window.c` + `data/gtkterm-update.sh`; spec `docs/superpowers/specs/2026-10-12-maj-par-tags-design.md`):**
updates install **published versions** = git tags `vX.Y.Z` (numeric order), never a branch head. The build knows `GIT_REVISION` and `GIT_DESCRIBE` (`git describe --tags --match 'v[0-9]*'`,
both by `vcs_tag()`, `safe.directory=*`; installed version = `GIT_DESCRIBE`, else `VERSION`, else 0.0.0; `vX.Y.Z-N-g…` is newer than `vX.Y.Z`, older than the next one). Install as root with
`meson install --no-rebuild`, never `sudo ninja install`. `gt_releases_fetch_async()` keeps a bare cache `$XDG_CACHE_HOME/gtkterm/versions.git` (`clone --bare --filter=tree:0`, then
`remote set-url` + `fetch --prune --force +refs/tags/*:refs/tags/*`), lists tags and reads `NEWS` of the newest tag (`git -c protocol.ext.allow=never`, `GIT_TERMINAL_PROMPT=0`, `LC_ALL=C`,
cancellable; URL empty or starting with `-` refused). `NEWS` (French, root of the repo): `## vX.Y.Z …` section, `- ` item, `! ` warning, two leading spaces continue; a section without a published
tag is never shown. The « Updates » window (Help › Check for updates…, refreshes on open) lists versions, shows the notes between installed and chosen version (warnings first; rollback: « You are
leaving these versions: »), « Update to / Roll back to / Reinstall vX » (confirmation when warnings), « Skip this version » (newer only, `[general] update_skipped_version` = tag); the start-up check
is silent unless a newer, non-skipped version exists, and then opens that window. The script `gtkterm-update.sh <url> <prefix> <pid> <tag>` checks out the tag in `$XDG_CACHE_HOME/gtkterm/src`,
re-executes the script **of that version** (it picks its own deps: GTK 3 for v1.3.x, GTK 4 for v2.x), installs deps, rebuilds from scratch (`-Dtests=false`), `pkexec`-installs and removes, in the
same authorization, files of the registry `installed-files.txt` missing from the new install log (under the prefix, no `..`, files/links only, not owned by a package), then restarts the app by
PID. `GTKTERM_UPDATE_SUDO` / `GTKTERM_UPDATE_DEPS=0` are for tests only. Release: `tools/publier-version.sh vX.Y.Z` (clean tree, `meson.build` version, `NEWS` section; annotated tag, no push).
```

  et remplacer ailleurs dans `CLAUDE.md` `update_skipped_revision` par `update_skipped_version` (`grep -n skipped CLAUDE.md`). Dans le paragraphe « GUI test harness », le faux `git` sert
  désormais `tags`/`news` (pas de `sha`).

- [ ] **Step 5 : `tests/README.md`** — mentionner `test_update_versions`, `test_update_fetch`, `update_script` (sh, git, meson, ninja requis ; préfixe jetable, `GTKTERM_UPDATE_SUDO=env`),
  `publier_version`, et le nouveau faux `git` de la suite gui (`Env.fake_git(tags=…, news=…, delay=…)`, `local_describe()`).

- [ ] **Step 6 : tests complets** — Run : `ninja -C build && meson test -C build && meson test -C build --suite gui` ; Expected : tout OK.

- [ ] **Step 7 : commit**

```bash
git add meson.build NEWS CLAUDE.md tests/README.md
git commit -m "version 2.0.0 : NEWS (nouveautés et avertissements de la refonte) et documentation de la mise à jour par versions taguées

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task B6 : revue, écrasement, fusion dans `master`, tag `v2.0.0`, vérification de bout en bout

- [ ] **Step 1 : revue de la branche** — agent `memory-safety-reviewer` sur `git diff <commit de fusion>..HEAD -- '*.c'` et sur `src/update_versions.c`, `src/update_fetch.c` ; corriger ce qui
  est confirmé (commit « correctifs de revue »), relancer `meson test -C build` et la suite gui.

- [ ] **Step 2 : écraser les commits APRÈS la fusion** (la fusion elle-même reste intacte) :

```bash
cd ~/git/gtkterm
FUSION=$(git log --merges --format=%H -1 --grep="ligne GTK 3 v1.3.2")
git reset --soft "$FUSION"
git reset -q
git add src/ po/ meson.build
git commit -m "mise à jour par versions taguées (refonte) : fenêtre GTK 4, GIT_DESCRIBE, version ignorée par tag, script appelé avec le tag ; version 2.0.0

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git add -A
git commit -m "outillage et doc : tests (modules partagés, script, fenêtre), faux git de la suite gui, NEWS v2.0.0, CLAUDE.md, tests/README.md

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short   # vide
```

  (Les specs et le plan sont déjà arrivés par la fusion, dans le commit « docs » de la ligne GTK 3 : pas de commit de docs ici.)

- [ ] **Step 3 : fusionner** — `git checkout master && git merge --no-ff feature/maj-par-tags -m "merge: mise à jour par versions taguées, refonte et v2.0.0 (feature/maj-par-tags)"` (avec la
  ligne Co-Authored-By) ; `ninja -C build && meson test -C build` ; puis corriger la date de `## v2.0.0` dans `NEWS` si besoin (commit « NEWS : date de v2.0.0 ») et
  `tools/publier-version.sh v2.0.0`. `git branch -d feature/maj-par-tags` (locale seulement). Vérifier : `git merge-base --is-ancestor v1.3.2 master && echo ok`.

- [ ] **Step 4 : bout en bout (utilisateur, installe réellement dans `/usr/local`)** — préparer la source de test : retirer d'abord les tags factices de la Task A6
  (`git -C $SCRATCH/depot-test.git tag -d v1.3.2 v1.3.3`), puis `git -C $SCRATCH/depot-test.git fetch -q ~/git/gtkterm master:master --tags` (les tags `v1.3.2` et `v2.0.0` y sont alors les vrais). Marche à suivre à donner à l'utilisateur :
  1. Installer `v1.3.2` : `sh ~/git/gtkterm-gtk3/data/gtkterm-update.sh file://$SCRATCH/depot-test.git /usr/local "" v1.3.2` (autorisations `pkexec`) ; lancer GTKTerm (GTK 3) ; Aide › Rechercher
     des mises à jour… ; source `file://$SCRATCH/depot-test.git` ; attendu : `v2.0.0 — la plus récente`, avertissements de v2.0.0 en tête, « Mettre à jour vers v2.0.0 ».
  2. Mettre à jour : confirmation (avertissements) → terminal → paquets GTK 4 → construction → installation → GTKTerm redémarre en GTK 4. Vérifier : un seul lanceur
     (`ls /usr/local/share/applications/`), « Version installée : v2.0.0 ».
  3. Revenir à `v1.3.2` depuis la fenêtre GTK 4 : « You are leaving these versions: » + section v2.0.0 ; confirmation ; GTKTerm GTK 3 redémarre ; un seul lanceur (`gtkterm.desktop`).
     Restaurer la configuration selon l'avertissement si besoin.
  4. Remonter en `v2.0.0`. Remettre la source par défaut (`https://github.com/Mula-Gabriel/gtkterm.git`) dans la fenêtre.

- [ ] **Step 5 : publication — seulement sur feu vert explicite de l'utilisateur, en deux temps** :
  1. `cd ~/git/gtkterm-gtk3 && git push origin v1.3.2 master && git push Maison v1.3.2 master` (le tag avant ou avec `master`) : les postes sur `0a013c2` (ancien gestionnaire,
     qui suit la tête de `master`) passent à `v1.3.2`.
  2. Plus tard : **ne pas faire avancer `master` de GitHub ni de Maison** tant qu'un poste peut encore être sur `0a013c2` — l'ancien script y suivrait la refonte (dépendances GTK 3,
     construction GTK 4 en échec à chaque démarrage). Publier la refonte par son tag et une branche dédiée : `cd ~/git/gtkterm && git push origin v2.0.0 master:refs/heads/refonte &&
     git push Maison v2.0.0 master:refs/heads/refonte` (le nouveau gestionnaire ne lit que les tags). Le jour où tous les postes sont au moins en `v1.3.2`, `master` peut avancer
     (avance rapide : `v1.3.2` est ancêtre du `master` de la refonte). Décision de l'utilisateur.

Remarque pour l'exécution : la Partie A se fait **entièrement** dans `~/git/gtkterm-gtk3`, la Partie B dans `~/git/gtkterm` ; le hook PostToolUse du projet reconstruit `~/git/gtkterm/build`
après chaque modification de `src/` dans ce dépôt-là seulement.
