# Nom de projet, éditeur de macros et arguments facultatifs — plan de réalisation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**But :** un projet a un nom (renommable, avec le fichier) ; Configure Macros regroupe les macros par onglet et les entrées par liste, filtre « Tab: »
et « List: », montre le fichier visé avec un bouton Reload, place ses boutons sous le tableau ; un argument de macro peut être facultatif avec un préfixe.

**Architecture :** le cœur porte les règles pures (regroupement d'un `GtMacroSet` à la lecture, vecteurs `arg_optional` / `arg_prefix`, expansion qui
omet préfixe et valeur, clés de fichier, nom de projet et nom de fichier dérivé) ; la couche application (`GtProject`) porte le nom et le déplacement du
fichier ; l'interface (`src/ui/`) ajoute la fenêtre « Rename project », et dans l'éditeur les filtres, le rechargement, les colonnes Optional / Prefix.
Les règles d'interface sans affichage (regroupement des lignes, déplacement borné, découpage du préfixe) vont dans `macro_row.c`, testé sans GTK.

**Tech Stack :** C, GLib/GIO, GTK 4 plafonné à 4.14 (`-Werror=deprecated-declarations`), Meson/Ninja, GLib GTest, scénarios GUI Python (broadway).

**Spec :** `docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md` — à lire **en entier** avant chaque tâche, §7 bis compris (précisions
fixées à la rédaction de ce plan). Elle fait foi en cas de doute.

## Global Constraints

- Travail **en direct sur `master`** ; pousser sur le dépôt distant `Maison` (pas `origin`) à la fin, après accord.
- Couches : aucun `<gtk/gtk.h>` hors `src/ui/` (`grep -rn 'gtk/gtk.h' src/*.c src/*.h` doit rester vide).
- GTK ≤ 4.14, VTE ≤ 0.76, GtkSourceView ≤ 5.12 ; pas de `GtkDialog`, `gtk_dialog_run`, ni boucle imbriquée ; aucune variable statique pour un état par onglet.
- Style : celui du fichier modifié (`macro_set.c`, `macro_file.c`, `project_file.c`, `project.c`, `macro_arg.c`, `src/ui/*` : tabulations, Allman, pas
  d'espace avant `(` ; `macros_format.c`, `macros_list.c` : GNU 2 espaces ; `macros.c` : GNU 2 espaces). Lignes jusqu'à 200 colonnes.
- Commentaires, messages de commit en français ; commits terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Le langage des `.ewu`
  n'est pas concerné ici.
- Toute chaîne visible : `_()` avec msgid anglais, traduction dans `po/fr.po` (tâche 12) ; fichier nouveau avec chaînes → `po/POTFILES.in`.
- Widgets atteints par les tests GUI : nom stable (`gtk_widget_set_name`) et nom accessible.
- Construire : `ninja -C build` ; tests : `meson test -C build` ; GUI : `meson test -C build --suite gui` (jamais deux suites GUI à la fois, jamais
  l'écran réel) ; un scénario précis : `meson test -C build --suite gui test_xxx`. SKIP 77 avec assertion interne de gtk4-broadwayd : défaut GTK connu.
- Ne jamais toucher `~/.config/.gtktermrc` ni un vrai port série ; aucun `pkexec`.
- Format : `.ini` reste `version=2`, projet `version=1`.

## Review Focus

1. **Projet ancien mélangé** (macros d'onglets entrelacés, projet enregistré avant cette version) : il doit s'ouvrir **non modifié** et un onglet lié ne doit
   pas être « UPDATED » pour un simple mélange — test dans la tâche 1 (`/macro_file/regroupe-lecture`) et la tâche 6 (`/project/ancien-melange-non-modifie`).
2. **Préfixe avec échappements et `%`** (` -t=\r`, `100%%`) : octets envoyés exacts, `%%` converti au découpage et rétabli à la réinsertion — tests
   tâches 3 et 8.
3. **Renommer vers un fichier existant** ou dans un dossier en lecture seule : rien ne change (ni nom, ni chemin, ni `last_project`) — tâche 6
   (`/project/deplacer-refus`) et tâche 7 (scénario GUI).
4. **Filtre actif puis la clé disparaît** (onglet renommé dans la cellule, dernière macro du groupe supprimée) : le filtre revient à « All », la sélection
   reste cohérente, Delete n'efface jamais une ligne cachée — tâche 9 (scénario GUI).
5. **Reload pendant une question en attente / onglet libre sans fichier** : bouton insensible ou caché, aucun rechargement silencieux qui jette les lignes —
   tâche 10.

---

### Tâche 1 : cœur — regroupement des macros par onglet à la lecture

**Fichiers :**
- Modifier : `src/macro_set.h`, `src/macro_set.c` (nouvelle fonction), `src/macro_file.c` (`gt_macro_set_from_key_file`, `load_legacy`)
- Tests : `tests/test_macro_set.c`, `tests/test_macro_file.c`

**Interfaces :**
- Produit : `gboolean gt_macro_set_group_by_tab(GtMacroSet *set);` — tri stable (groupes dans l'ordre de première apparition de leur `tab`, `""` compris,
  ordre relatif gardé dans un groupe, identifiants et pointeurs gardés) ; TRUE si l'ordre a changé ; NULL accepté (FALSE).

- [ ] **Étape 1 : test du regroupement (échoue)** — dans `tests/test_macro_set.c`, avant `main` :

```c
/* Libellés du jeu dans l'ordre, séparés par des virgules (g_free) */
static gchar *ordre_jeu(const GtMacroSet *set)
{
	GString *s = g_string_new(NULL);

	for(guint i = 0; i < gt_macro_set_count(set); i++)
		g_string_append_printf(s, i ? ",%s" : "%s", gt_macro_set_get(set, i)->label);
	return g_string_free(s, FALSE);
}

static void test_regroupe_onglets(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = gt_macro_set_append(set, "a", "", "A", "T1");
	guint b = gt_macro_set_append(set, "b", "", "B", "");
	guint c = gt_macro_set_append(set, "c", "", "C", "T1");
	gt_macro_set_append(set, "d", "", "D", "T2");
	gt_macro_set_append(set, "e", "", "E", "");
	gchar *o;

	g_assert_true(gt_macro_set_group_by_tab(set));
	o = ordre_jeu(set);
	g_assert_cmpstr(o, ==, "a,c,b,e,d");
	g_free(o);
	g_assert_cmpuint(gt_macro_set_get(set, 0)->id, ==, a);	/* identifiants gardés */
	g_assert_cmpuint(gt_macro_set_get(set, 1)->id, ==, c);
	g_assert_cmpuint(gt_macro_set_get(set, 2)->id, ==, b);
	g_assert_false(gt_macro_set_group_by_tab(set));	/* déjà regroupé */
	g_assert_false(gt_macro_set_group_by_tab(NULL));
	gt_macro_set_free(set);
}
```
Enregistrer : `g_test_add_func("/macro_set/regroupe-onglets", test_regroupe_onglets);`

- [ ] **Étape 2 : lancer, constater l'échec** — `ninja -C build` : erreur « implicit declaration of gt_macro_set_group_by_tab ».

- [ ] **Étape 3 : implémenter** — `macro_set.h`, après `gt_macro_set_clone` :

```c
/* Regroupe les macros par onglet (spec 2026-10-15-nom-projet §2.1) : groupes dans l'ordre de première apparition de leur tab ("" compris),
 * ordre relatif gardé ; identifiants et pointeurs inchangés. TRUE si l'ordre a changé ; NULL : FALSE. */
gboolean            gt_macro_set_group_by_tab (GtMacroSet *set);
```
`macro_set.c`, après `gt_macro_set_clone` :

```c
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
```

- [ ] **Étape 4 : test de lecture (échoue)** — dans `tests/test_macro_file.c` :

```c
/* Un fichier aux onglets entrelacés est lu regroupé ; le fichier n'est pas réécrit */
static void
test_regroupe_lecture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-mf-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	const gchar *texte = "[gtkterm-macros]\nversion=2\n\n"
	                     "[macro:1]\nlabel=a\naction=A\ntab=T1\n\n"
	                     "[macro:2]\nlabel=b\naction=B\ntab=\n\n"
	                     "[macro:3]\nlabel=c\naction=C\ntab=T1\n";
	GtMacroSet *set;
	gchar *relu;

	g_assert_true(g_file_set_contents(path, texte, -1, NULL));
	set = charger(path);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->label, ==, "a");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->label, ==, "c");
	g_assert_cmpstr(gt_macro_set_get(set, 2)->label, ==, "b");
	relu = lire(path);
	g_assert_cmpstr(relu, ==, texte);	/* jamais réécrit par la lecture */
	g_free(relu);
	gt_macro_set_free(set);
	g_remove(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}
```
Enregistrer `"/macro_file/regroupe-lecture"`. Ajouter le même contrôle pour l'ancien format : `[macros]` avec
`macro_0=a::None::A::T1::::0::1000`, `macro_1=b::None::B::::::0::1000`, `macro_2=c::None::C::T1::::0::1000` → ordre a, c, b
(`"/macro_file/regroupe-ancien-format"`).

- [ ] **Étape 5 : implémenter** — `macro_file.c` : à la fin de `gt_macro_set_from_key_file`, juste avant `g_strfreev(groups); return set;` :

```c
	/* Regroupement par onglet à la lecture (spec 2026-10-15-nom-projet §2.2) : .ini, modèle, base en cache, jeu intégré d'un projet */
	gt_macro_set_group_by_tab(set);
```
et dans `load_legacy`, juste avant son `return` du jeu : `gt_macro_set_group_by_tab(set);` (nom de la variable locale de `load_legacy`).

- [ ] **Étape 6 : lancer toute la suite unitaire** — `meson test -C build`. Attendu : PASS. Si un test existant (`test_macro_file`, `test_macros_template`,
  `test_project_file`, `test_macros_send`) lisait un fichier aux onglets entrelacés et vérifiait l'ordre du fichier, corriger **l'attendu** (le nouvel ordre
  regroupé est la règle) et le signaler dans le message de commit.

- [ ] **Étape 7 : commit**

```bash
git add src/macro_set.h src/macro_set.c src/macro_file.c tests/test_macro_set.c tests/test_macro_file.c
git commit -m "macros : jeu regroupé par onglet à la lecture (tri stable, identifiants gardés)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Tâche 2 : cœur — modèle des arguments facultatifs (`arg_optional`, `arg_prefix`)

**Fichiers :**
- Modifier : `src/macro_set.h`, `src/macro_set.c`
- Tests : `tests/test_macro_set.c`

**Interfaces :**
- Produit (macro_set.h) :
  - `GtMacro` gagne, après `arg_charset` : `gchar **arg_optional;` (`"1"` / `""`) et `gchar **arg_prefix;` (texte brut, `%` littéral, échappements `\r`…
    traités à l'envoi) — NULL ou exactement `macro_count_format_args()` éléments, jamais entièrement vides.
  - `GtMacroEdit` gagne en **fin** de structure : `const gchar *const *arg_optional; const gchar *const *arg_prefix;` (initialiseurs positionnels existants
    valides).
  - `gboolean gt_macro_set_set_optional(GtMacroSet *set, guint id, const gchar *const *optional, const gchar *const *prefix, gint n_args);` — chaque vecteur
    NULL ou de `n_args` éléments, `n_args` = nombre d'arguments de la macro, sinon FALSE et rien ne change ; une valeur d'`optional` autre que `"1"` devient
    `""` ; vecteur entièrement vide → NULL.
  - `gboolean gt_macro_arg_is_optional(const GtMacro *macro, gint k);` et `const gchar *gt_macro_arg_prefix(const GtMacro *macro, gint k);` (`""` sans
    préfixe ou hors bornes).
- Règle de longueur (spec §5.1) : quand le nombre d'arguments change, `arg_optional` et `arg_prefix` sont **tronqués ou complétés par `""`** (jamais
  effacés en bloc) ; `same_structure` les compare (NULL ≡ entièrement vide) ; `gt_macro_check_args` saute un argument facultatif vide.

- [ ] **Étape 1 : tests (échouent)** — `tests/test_macro_set.c` :

```c
static void test_facultatif_modele(void)
{
	GtMacroSet *set = gt_macro_set_new(), *copie, *clone;
	guint id = gt_macro_set_append(set, "co", "", "connect %s -password=%s", "");
	const gchar *opt[] = { "", "oui", NULL }, *pre[] = { "", " -password=", NULL }, *trop[] = { "1", NULL };
	const GtMacro *m;
	GtMacroEdit row = { 0 };

	g_assert_false(gt_macro_set_set_optional(set, id, trop, NULL, 1));	/* mauvaise longueur */
	g_assert_true(gt_macro_set_set_optional(set, id, opt, pre, 2));
	m = gt_macro_set_find(set, id);
	g_assert_null(m->arg_optional);	/* « oui » n'est pas « 1 » : vecteur entièrement vide → NULL */
	opt[1] = "1";
	g_assert_true(gt_macro_set_set_optional(set, id, opt, pre, 2));
	m = gt_macro_set_find(set, id);
	g_assert_true(gt_macro_arg_is_optional(m, 1));
	g_assert_false(gt_macro_arg_is_optional(m, 0));
	g_assert_false(gt_macro_arg_is_optional(m, 7));
	g_assert_cmpstr(gt_macro_arg_prefix(m, 1), ==, " -password=");
	g_assert_cmpstr(gt_macro_arg_prefix(m, 0), ==, "");
	/* Copie et clone gardent les vecteurs */
	copie = gt_macro_set_copy(set);
	clone = gt_macro_set_clone(set);
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(copie, 0), 1), ==, " -password=");
	g_assert_true(gt_macro_arg_is_optional(gt_macro_set_get(clone, 0), 1));
	g_assert_true(gt_macro_set_same_structure(set, copie));
	/* Action raccourcie à un argument : vecteurs tronqués, pas effacés ; rallongée : complétés */
	row.id = id;
	row.label = "co";
	row.shortcut = "";
	row.action = "connect%s";
	row.tab = "";
	row.arg_optional = (const gchar *const[]){ "1", "1", NULL };
	row.arg_prefix = (const gchar *const[]){ " -u=", " -p=", NULL };
	gt_macro_set_apply_edits(copie, &row, 1, NULL);
	m = gt_macro_set_get(copie, 0);
	g_assert_cmpuint(g_strv_length(m->arg_prefix), ==, 1);
	g_assert_cmpstr(m->arg_prefix[0], ==, " -u=");
	row.action = "c%s%s%s";
	gt_macro_set_apply_edits(copie, &row, 1, NULL);
	m = gt_macro_set_get(copie, 0);
	g_assert_cmpuint(g_strv_length(m->arg_prefix), ==, 3);
	g_assert_cmpstr(m->arg_prefix[2], ==, "");
	g_assert_cmpstr(m->arg_optional[2], ==, "");
	/* Structure : changer Optional ou Prefix la change */
	pre[1] = " -pw=";
	g_assert_true(gt_macro_set_set_optional(clone, gt_macro_set_get(clone, 0)->id, opt, pre, 2));
	g_assert_false(gt_macro_set_same_structure(set, clone));
	gt_macro_set_free(copie);
	gt_macro_set_free(clone);
	gt_macro_set_free(set);
}

static void test_facultatif_verification(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "v", "", "V%d", "");
	const gchar *opt[] = { "1", NULL }, *min[] = { "5", NULL }, *vide[] = { "", NULL }, *trois[] = { "3", NULL };

	gt_macro_set_set_limits(set, id, min, NULL, NULL, 1);
	gt_macro_set_set_optional(set, id, opt, NULL, 1);
	g_assert_true(gt_macro_check_args(set, gt_macro_set_find(set, id), vide, 1, NULL, NULL));	/* omis : ni type ni limites */
	g_assert_false(gt_macro_check_args(set, gt_macro_set_find(set, id), trois, 1, NULL, NULL));	/* rempli : limites */
	gt_macro_set_free(set);
}
```
Enregistrer `"/macro_set/facultatif-modele"` et `"/macro_set/facultatif-verification"`.

- [ ] **Étape 2 : lancer** — `ninja -C build` échoue (champs et fonctions absents).

- [ ] **Étape 3 : implémenter** dans `macro_set.c` :
  - `macro_free` : `g_strfreev(m->arg_optional); g_strfreev(m->arg_prefix);`
  - fonction locale :

```c
/* Ajuste un vecteur à n éléments (tronqué, ou complété par "") ; NULL reste NULL ; entièrement vide → NULL */
static void fit_length(gchar ***v, gint n)
{
	guint len;

	if(*v == NULL)
		return;
	len = g_strv_length(*v);
	if(len != (guint)n)
	{
		gchar **fitted = g_new0(gchar *, n + 1);

		for(gint k = 0; k < n; k++)
			fitted[k] = g_strdup((guint)k < len ? (*v)[k] : "");
		g_strfreev(*v);
		*v = fitted;
	}
	if(n == 0 || vector_all_empty(*v))
		g_clear_pointer(v, g_strfreev);
}
```
  - `check_args` : condition d'entrée étendue (`&& m->arg_optional == NULL && m->arg_prefix == NULL`), puis après le traitement des limites :
    `fit_length(&m->arg_optional, n); fit_length(&m->arg_prefix, n);` et normalisation : toute valeur d'`arg_optional` ≠ `"1"` remplacée par `""`
    (puis `fit_length` à nouveau pour l'éventuel « tout vide »).
  - `gt_macro_set_set_optional` :

```c
gboolean
gt_macro_set_set_optional(GtMacroSet *set, guint id, const gchar *const *optional, const gchar *const *prefix, gint n_args)
{
	GtMacro *m = find_mutable(set, id);
	gboolean ok = TRUE;
	gchar **o, **p;

	if(m == NULL || n_args != macro_count_format_args(set->lists, m->action))
		return FALSE;
	o = copy_vector(optional, n_args, &ok);
	p = ok ? copy_vector(prefix, n_args, &ok) : NULL;
	if(!ok)
	{
		g_strfreev(o);
		g_strfreev(p);
		return FALSE;
	}
	for(gint k = 0; o != NULL && k < n_args; k++)
		if(strcmp(o[k], "1") != 0)
		{
			g_free(o[k]);
			o[k] = g_strdup("");
		}
	g_strfreev(m->arg_optional);
	g_strfreev(m->arg_prefix);
	m->arg_optional = o;
	m->arg_prefix = p;
	fit_length(&m->arg_optional, n_args);
	fit_length(&m->arg_prefix, n_args);
	return TRUE;
}

gboolean
gt_macro_arg_is_optional(const GtMacro *macro, gint k)
{
	return macro != NULL && macro->arg_optional != NULL && k >= 0 && (guint)k < g_strv_length(macro->arg_optional)
	       && strcmp(macro->arg_optional[k], "1") == 0;
}

const gchar *
gt_macro_arg_prefix(const GtMacro *macro, gint k)
{
	if(macro == NULL || macro->arg_prefix == NULL || k < 0 || (guint)k >= g_strv_length(macro->arg_prefix))
		return "";
	return macro->arg_prefix[k];
}
```
  - `gt_macro_set_copy` : après les limites,
    `if(m->arg_optional != NULL || m->arg_prefix != NULL) gt_macro_set_set_optional(copy, id, (const gchar *const *)m->arg_optional, (const gchar *const *)m->arg_prefix, (gint)g_strv_length(m->arg_optional != NULL ? m->arg_optional : m->arg_prefix));`
  - `gt_macro_set_apply_edits` : comme les limites, `g_clear_pointer` puis `g_strdupv` des `rows[i].arg_optional` / `rows[i].arg_prefix` non vides ;
    `check_args` ajuste ensuite les longueurs.
  - `gt_macro_set_same_structure` : ajouter `|| !limits_equal(ma->arg_optional, mb->arg_optional) || !limits_equal(ma->arg_prefix, mb->arg_prefix)` ;
    mettre à jour son commentaire dans `macro_set.h` (« … limites, Optional et Prefix … »).
  - `gt_macro_set_clone` : `c->arg_optional = g_strdupv(m->arg_optional); c->arg_prefix = g_strdupv(m->arg_prefix);`
  - `gt_macro_check_args` : dans la boucle, `if(gt_macro_arg_is_optional(macro, k) && (use[k] == NULL || use[k][0] == '\0')) continue;` avant le test de liste.
  - Commentaires de `GtMacro` et `GtMacroEdit` dans `macro_set.h` (règle de longueur différente des limites).

- [ ] **Étape 4 : lancer** — `meson test -C build`. Attendu : PASS (`macro_override.c` compare via `same_structure` : rien d'autre à changer).

- [ ] **Étape 5 : commit** — `git commit -m "macros : arguments facultatifs et préfixes dans le jeu (longueur suivie rang par rang, structure)"` (avec la ligne
  Co-Authored-By).

---

### Tâche 3 : cœur — expansion : argument facultatif vide omis, préfixe envoyé, segment « omis »

**Fichiers :**
- Modifier : `src/macros_format.h`, `src/macros_format.c` (style GNU 2 espaces), `src/macro_set.h`, `src/macro_set.c`, `src/macro_preview.h`, `src/macro_preview.c`
- Tests : `tests/test_macros_format.c` (4 appels de `format_action_traced` à compléter), `tests/test_macro_set.c`, `tests/test_macro_preview.c`

**Interfaces :**
- `format_action_traced(lists, action, args, n_args, charsets, optional, prefixes, arg_ranges)` — deux paramètres insérés avant `arg_ranges` (nullable,
  parallèles aux spécificateurs) ; `format_action_with_args` passe NULL, NULL.
- `GtMacroSegKind` gagne `GT_MACRO_SEG_OMITTED` (longueur 0, `arg` = rang) ; `GtPreviewKind` gagne `GT_PREVIEW_OMITTED` (texte « ∅ »).

- [ ] **Étape 1 : tests (échouent)** — `tests/test_macro_set.c` :

```c
static void test_facultatif_expansion(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();
	guint id;
	const gchar *opt[] = { "", "1", "1", NULL }, *pre[] = { "", " -p=\\r", " -m=", NULL };
	const gchar *vide[] = { "srv", "", "", NULL }, *plein[] = { "srv", "x%y", "fast", NULL }, *court[] = { "srv", NULL };
	GBytes *b;
	GArray *segs = NULL;

	gt_macro_lists_add(lists, "Mode", "Fast", "fast");
	gt_macro_lists_add(lists, "Mode", "(none)", "");
	gt_macro_set_take_lists(set, lists);
	id = gt_macro_set_append(set, "c", "", "connect %s%s%#Mode;", "");
	g_assert_true(gt_macro_set_set_optional(set, id, opt, pre, 3));
	b = gt_macro_expand(set, gt_macro_set_find(set, id), vide, 3);
	g_assert_cmpmem(g_bytes_get_data(b, NULL), g_bytes_get_size(b), "connect srv;", 12);
	g_bytes_unref(b);
	b = gt_macro_expand(set, gt_macro_set_find(set, id), plein, 3);	/* préfixe : échappement traité ; valeur : % littéral */
	g_assert_cmpmem(g_bytes_get_data(b, NULL), g_bytes_get_size(b), "connect srv -p=\rx%y -m=fast;", 28);
	g_bytes_unref(b);
	b = gt_macro_expand(set, gt_macro_set_find(set, id), court, 1);	/* arguments manquants facultatifs : omis, jamais « %s » littéral */
	g_assert_cmpmem(g_bytes_get_data(b, NULL), g_bytes_get_size(b), "connect srv;", 12);
	g_bytes_unref(b);
	b = gt_macro_expand_segments(set, gt_macro_set_find(set, id), vide, 3, &segs);
	{
		guint omis = 0;

		for(guint k = 0; k < segs->len; k++)
		{
			const GtMacroSegment *s = &g_array_index(segs, GtMacroSegment, k);

			if(s->kind == GT_MACRO_SEG_OMITTED)
			{
				g_assert_cmpuint(s->len, ==, 0);
				g_assert_cmpuint(s->start, ==, 11);	/* avant « ; » */
				omis++;
			}
		}
		g_assert_cmpuint(omis, ==, 2);
	}
	g_array_unref(segs);
	g_bytes_unref(b);
	gt_macro_set_free(set);
}
```
`tests/test_macro_preview.c` : un segment `{ GT_MACRO_SEG_OMITTED, 1, 2, 0 }` entre deux segments TEXT de `"ab;"` (0–2, 2–3) donne les runs
`TEXT "ab"`, `OMITTED "∅"` (arg 1), `TEXT ";"`.
`tests/test_macros_format.c` : ajouter `NULL, NULL,` avant le dernier argument des 4 appels existants à `format_action_traced`, et un cas
`format_action_traced(NULL, "a%sb", (const gchar *[]){ "" }, 1, NULL, (const gchar *const[]){ "1", NULL }, (const gchar *const[]){ "100%%", NULL }, plages)`
→ texte `"ab"`, une plage de longueur 0 au rang 1 ; avec la valeur `"v"` → `"a100%%vb"` (le préfixe est copié brut ; `%%` n'est pas réduit par
`format_action_traced` — c'est le découpage, tâche 8, qui stocke `%`).

- [ ] **Étape 2 : lancer** — échec de compilation (nouveaux paramètres, nouvelles constantes).

- [ ] **Étape 3 : implémenter `format_action_traced`** — signature et commentaire dans `macros_format.h`, puis dans `macros_format.c` (style GNU) :

```c
/* Argument facultatif (spec 2026-10-15-nom-projet §5.2) : vide ou non fourni, rien n'est rendu (plage vide notée) ; sinon le préfixe, brut,
   précède la valeur (ses échappements seront traités par parse_macro_string). TRUE si l'argument est omis. */
static gboolean
optional_omitted (const gchar *const *optional, gint arg_idx, const gchar **args, gint n_args)
{
  if (!optional || g_strcmp0 (optional[arg_idx], "1") != 0)
    return FALSE;
  return arg_idx >= n_args || !args[arg_idx] || !args[arg_idx][0];
}

static void
append_prefix (GString *result, const gchar *const *optional, const gchar *const *prefixes, gint arg_idx)
{
  if (optional && g_strcmp0 (optional[arg_idx], "1") == 0 && prefixes && prefixes[arg_idx])
    g_string_append (result, prefixes[arg_idx]);
}
```
  Dans la boucle, pour la branche liste **et** la branche spécificateur, avant le test `arg_idx < n_args` :

```c
            if (optional_omitted (optional, arg_idx, args, n_args))
              {
                note_range (arg_ranges, result->len, result->len);
                arg_idx++;
                i = list_end;          /* spec_end dans la branche spécificateur, après g_free (fmt_type) */
                g_free (list_name);    /* branche liste seulement */
                continue;
              }
```
  et dans la branche `arg_idx < n_args` : `gsize start = result->len; append_prefix (result, optional, prefixes, arg_idx);` **avant** la valeur (la plage
  couvre préfixe + valeur). Les vecteurs ont exactement un élément par spécificateur : lecture `optional[arg_idx]` sûre.

- [ ] **Étape 4 : implémenter les segments** — `macro_set.h` : `GT_MACRO_SEG_OMITTED` ajouté en fin d'énumération, commentaire « longueur 0 : argument
  facultatif omis, placé avant l'octet qui le suit ». `macro_set.c` : `gt_macro_expand_segments` passe
  `(const gchar *const *)macro->arg_optional, (const gchar *const *)macro->arg_prefix` ; `build_segments(origins, arg_ranges)` insère les plages vides :

```c
static void
emit_omitted(GArray *segs, const GArray *arg_ranges, gboolean *done, guint upto_char, guint byte_pos)
{
	for(guint k = 0; k < arg_ranges->len; k++)
	{
		const GtMacroRange *r = &g_array_index(arg_ranges, GtMacroRange, k);

		if(!done[k] && r->len == 0 && r->start <= upto_char)
		{
			GtMacroSegment seg = { GT_MACRO_SEG_OMITTED, (gint)k, byte_pos, 0 };

			g_array_append_val(segs, seg);
			done[k] = TRUE;
		}
	}
}
```
  appelée au début de chaque octet `b` avec `upto_char = origine(b).start` et `byte_pos = b`, puis une dernière fois après la boucle avec
  `upto_char = G_MAXUINT`, `byte_pos = origins->len` ; `done` = `g_new0(gboolean, arg_ranges->len)`. La fusion des voisins ignore les segments OMITTED
  (un segment OMITTED n'est jamais fusionné, et le segment suivant ne fusionne pas avec lui).

- [ ] **Étape 5 : aperçu** — `macro_preview.h` : `GT_PREVIEW_OMITTED` ; `gt_macro_preview_runs` : un segment OMITTED ajoute un run
  `{ GT_PREVIEW_OMITTED, seg->arg, g_strdup("∅") }` (jamais fusionné) ; commentaire de `gt_macro_preview_runs` complété.

- [ ] **Étape 6 : lancer** — `meson test -C build` : PASS. Vérifier que `src/ui/macro_editor.c` compile encore (`preview_color` : le `default` couvre la
  nouvelle sorte ; elle sera stylée tâche 11).

- [ ] **Étape 7 : commit** — `"macros : expansion des arguments facultatifs (préfixe, omission, segment omis de l'aperçu)"`.

---

### Tâche 4 : cœur — clés `arg_optional` / `arg_prefix` (`.ini` et projet), description de l'argument

**Fichiers :**
- Modifier : `src/macro_file.c`, `src/macro_arg.h`, `src/macro_arg.c`
- Tests : `tests/test_macro_file.c`, `tests/test_project_file.c`, `tests/test_macro_arg.c`

**Interfaces :**
- Lecture tolérante dans `gt_macro_set_from_key_file` ; écriture dans `gt_macro_set_to_key_file` (servent au `.ini` et aux groupes `[tab:N:macro:M]`).
- `gchar *gt_macro_arg_describe_optional(const gchar *prefix);` → `_("optional")` ou `_("optional, prefix \"%s\"")` (g_free).

- [ ] **Étape 1 : tests (échouent)** — `tests/test_macro_file.c` :

```c
static void
test_facultatif_aller_retour(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-mf-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;
	guint id = gt_macro_set_append(set, "co", "", "connect %s%s", "");
	const gchar *opt[] = { "", "1", NULL }, *pre[] = { "", " -password=", NULL };
	GError *error = NULL;
	gchar *texte;

	g_assert_true(gt_macro_set_set_optional(set, id, opt, pre, 2));
	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "arg_optional=;1;"));
	g_assert_nonnull(strstr(texte, "arg_prefix=;\\s-password=;"));	/* espace de tête échappé par GKeyFile */
	g_free(texte);
	relu = charger(path);
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(relu, 0), 1), ==, " -password=");
	g_assert_true(gt_macro_arg_is_optional(gt_macro_set_get(relu, 0), 1));
	g_assert_true(gt_macro_set_same_structure(set, relu));
	gt_macro_set_free(relu);
	/* Lecture tolérante : longueur fausse ajustée, valeur inconnue → "" (avertissements attendus) */
	g_assert_true(g_file_set_contents(path, "[gtkterm-macros]\nversion=2\n\n[macro:1]\nlabel=x\naction=X%s%s\n"
	                                  "arg_optional=1;oui;1;\narg_prefix=a;\n", -1, NULL));
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*arg_optional*");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*arg_prefix*");
	relu = charger(path);
	g_test_assert_expected_messages();
	g_assert_true(gt_macro_arg_is_optional(gt_macro_set_get(relu, 0), 0));
	g_assert_false(gt_macro_arg_is_optional(gt_macro_set_get(relu, 0), 1));
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(relu, 0), 0), ==, "a");
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(relu, 0), 1), ==, "");
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	g_remove(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}
```
(Si le texte réel de GKeyFile diffère pour l'échappement, vérifier par relecture plutôt que par `strstr` sur `\\s` ; l'essentiel est l'aller-retour.)
`tests/test_project_file.c` : un onglet dont le jeu a un argument facultatif avec préfixe ` -p=` → `gt_project_data_save` puis `gt_project_data_load` :
préfixe et Optional relus (`"/project_file/arguments-facultatifs"`). `tests/test_macro_arg.c` : `gt_macro_arg_describe_optional(NULL)` = `"optional"`,
`gt_macro_arg_describe_optional(" -p=")` = `"optional, prefix \" -p=\""`.

- [ ] **Étape 2 : lancer** — échec.

- [ ] **Étape 3 : implémenter la lecture** — dans `gt_macro_set_from_key_file`, après le bloc des limites :

```c
		/* Arguments facultatifs (spec 2026-10-15-nom-projet §5.3) : longueur ajustée au nombre d'arguments, avec avertissement */
		{
			gsize n_opt = 0, n_pre = 0;
			gchar **opt = get_list_tolerant(kf, groups[g], "arg_optional", origin, &n_opt);
			gchar **pre = get_list_tolerant(kf, groups[g], "arg_prefix", origin, &n_pre);
			gint n = macro_count_format_args(gt_macro_set_get_lists(set), action);

			if(opt != NULL || pre != NULL)
			{
				gchar **v[2] = { opt, pre };
				const gchar *keys[2] = { "arg_optional", "arg_prefix" };

				for(gint i = 0; i < 2; i++)
				{
					gboolean bad = v[i] != NULL && g_strv_length(v[i]) != (guint)n;

					for(guint k = 0; i == 0 && v[i] != NULL && v[i][k] != NULL; k++)
						bad |= v[i][k][0] != '\0' && strcmp(v[i][k], "1") != 0;
					if(bad)
					{
						gchar *msg = g_strdup_printf(_("%s: [%s] %s: invalid value adjusted"), origin, groups[g], keys[i]);

						g_warning("%s", msg);
						g_free(msg);
					}
					v[i] = fit_vector(v[i], n);	/* tronqué ou complété par "" (prend v[i]) */
				}
				gt_macro_set_set_optional(set, id, (const gchar *const *)v[0], (const gchar *const *)v[1], n);
				g_strfreev(v[0]);
				g_strfreev(v[1]);
			}
			else
			{
				g_strfreev(opt);
				g_strfreev(pre);
			}
		}
```
  avec `fit_vector()` locale (même logique que `fit_length` de la tâche 2, version qui rend le vecteur ajusté ; `#include "macros_format.h"` si absent).
- [ ] **Étape 4 : implémenter l'écriture** — dans `gt_macro_set_to_key_file`, après `arg_charset` :
  `if(m->arg_optional != NULL) g_key_file_set_string_list(kf, group, "arg_optional", …); if(m->arg_prefix != NULL) g_key_file_set_string_list(kf, group, "arg_prefix", …);`
- [ ] **Étape 5 : `gt_macro_arg_describe_optional`** dans `macro_arg.c` (+ déclaration commentée dans `macro_arg.h`) :

```c
gchar *
gt_macro_arg_describe_optional(const gchar *prefix)
{
	if(prefix == NULL || prefix[0] == '\0')
		return g_strdup(_("optional"));
	return g_strdup_printf(_("optional, prefix \"%s\""), prefix);
}
```
- [ ] **Étape 6 : lancer** — `meson test -C build` : PASS.
- [ ] **Étape 7 : commit** — `"macros : clés arg_optional et arg_prefix (lecture tolérante), description d'un argument facultatif"`.

---

### Tâche 5 : interface — panneau : liste « (none) », champ « (omitted) », bulle

**Fichiers :**
- Modifier : `src/ui/macro_arg_widget.h`, `src/ui/macro_arg_widget.c`, `src/ui/macro_panel.c`

**Interfaces :**
- Produit : `void gt_macro_arg_widget_set_optional(GtkWidget *w, gboolean optional, const gchar *prefix);` — données `arg-optional`, `arg-prefix` ; une
  entrée facultative vide (après la règle « vide = défaut ») est valide ; la bulle ajoute `gt_macro_arg_describe_optional(prefix)` (« , » + texte).
- `gt_macro_arg_widget_new(lists, info, initial, placeholder)` reste ; nouvelle variante pour la liste facultative :
  `GtkWidget *gt_macro_arg_widget_new_full(const GtMacroLists *lists, const macro_arg_info_t *info, const gchar *initial, const gchar *placeholder, gboolean optional);`
  — `optional` et type `l` : première entrée « (none) » (valeur `""`), choisie si `initial` vaut `""` ou NULL ; `gt_macro_arg_widget_new` appelle
  `_full(…, FALSE)`.

- [ ] **Étape 1 : implémenter `_full`** — dans la branche liste, `n_extra = optional ? 1 : 0` ; `displays[0] = g_strdup(_("(none)"))`,
  `values[0] = g_strdup("")` quand `optional`, entrées de la liste décalées d'un rang ; recherche de `initial` sur toutes les valeurs (la première valeur
  égale gagne, donc `""` → « (none) »). Branche entrée : si `optional` et `placeholder` NULL ou vide → `placeholder = _("(omitted)")`.
- [ ] **Étape 2 : `set_optional` et vérification** — `gt_macro_arg_widget_check` : après la résolution « vide = défaut », si
  `g_object_get_data(o, "arg-optional") != NULL && value[0] == '\0'` → TRUE. `gt_macro_arg_widget_validate` : texte de bulle complété par
  `", "` + `gt_macro_arg_describe_optional(prefix)` quand facultatif.
- [ ] **Étape 3 : panneau** — `make_arg_widget` de `macro_panel.c` : `gboolean opt = gt_macro_arg_is_optional(m, k);` ;
  `w = gt_macro_arg_widget_new_full(lists, info, initial, def[0] != '\0' ? def : NULL, opt);` puis
  `gt_macro_arg_widget_set_optional(w, opt, gt_macro_arg_prefix(m, k));` avant `set_limits`. Pour la liste déroulante, `initial` d'une liste
  facultative sans usage ni défaut vaut `""` → « (none) ». `commit_arg` : une liste facultative sur « (none) » écrit la valeur d'usage `""` (règle
  actuelle, inchangée : `""` = défaut).
- [ ] **Étape 4 : construire** — `ninja -C build` (aucun avertissement de dépréciation) ; `meson test -C build` : PASS.
- [ ] **Étape 5 : scénario GUI (échoue puis passe)** — créer `tests/gui/test_macros_facultatifs.py` (partie panneau ; l'éditeur viendra tâche 11) :

```python
# Arguments facultatifs (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §5) : panneau — un %s facultatif vide
# n'envoie ni préfixe ni valeur ; rempli, il envoie le préfixe puis la valeur ; une liste facultative a « (none) ».
from gtktest import *

e = Env('macros_facultatifs')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='Co', action='connect %s%s;', args=['srv', ''], arg_optional=['', '1'], arg_prefix=['', ' -password=']),
                      dict(label='Mo', action='mode%#Mode;', arg_optional=['1'], arg_prefix=[' -m='])],
                lists=[('Mode', [('Fast', 'fast'), ('Slow', 'slow')])])
macro_config(e, ini)
e.start()
co = macro_id(e, 'Co')
e.drain()
e.activate('macro-%d' % co)
check(wait_until(lambda: b'connect srv;' in e.read_pty(0.3), 3), 'facultatif vide : ni prefixe ni valeur')
check(e.find1(name='macro-%d-arg-1' % co).get('placeholder') == '(omitted)', 'texte indicatif (omitted)')
e.set_text('macro-%d-arg-1' % co, 'abc')
e.activate('macro-%d-arg-1' % co)        # Entrée : valeur d'usage prise
e.drain()
e.activate('macro-%d' % co)
check(wait_until(lambda: b'connect srv -password=abc;' in e.read_pty(0.3), 3), 'rempli : prefixe puis valeur')
mo = macro_id(e, 'Mo')
e.drain()
e.activate('macro-%d' % mo)
check(wait_until(lambda: b'mode;' in e.read_pty(0.3), 3), 'liste facultative sur (none) : omise')
e.set_text('macro-%d-arg-0' % mo, 'Slow')
e.drain()
e.activate('macro-%d' % mo)
check(wait_until(lambda: b'mode -m=slow;' in e.read_pty(0.3), 3), 'liste choisie : prefixe et valeur')
e.finish()
```
  `tests/gui/gtktest.py` : `write_macros_v2` accepte aussi `arg_optional` et `arg_prefix` (ajouter les deux clés au tuple `('args', 'arg_min', …)`).
  `tests/gui/meson.build` : `'test_macros_facultatifs',` dans la section des macros (après `'test_macros_limites'`). Le premier essai (avant les
  étapes 1–3) échoue sur « texte indicatif (omitted) » ; après, `meson test -C build --suite gui test_macros_facultatifs` : OK. Adapter le moyen de
  valider le champ (`activate` sur l'entrée = signal `activate`) à ce que fait `test_macros_arguments.py` si besoin.
- [ ] **Étape 6 : commit** — `"panneau de macros : arguments facultatifs (liste « (none) », champ « (omitted) », bulle)"` (fichiers UI, gtktest.py,
  scénario, meson.build).

---

### Tâche 6 : nom du projet — cœur et couche application

**Fichiers :**
- Modifier : `src/project_file.h`, `src/project_file.c`, `src/project.h`, `src/project.c`
- Tests : `tests/test_project_file.c`, `tests/test_project.c`

**Interfaces :**
- `GtProjectData` gagne `gchar *name;` (NULL : aucun) ; libéré par `gt_project_data_free` ; clé `name` de `[gtkterm-project]`, écrite seulement si non
  NULL, dans le fichier **et** l'instantané ; lue par `read_string` puis `g_strstrip`, vide → NULL.
- `gchar *gt_project_name_validate(const gchar *name, gchar **error_out);` (project_file.c) : rogné, 1–128 caractères, sans caractère de contrôle ;
  rend le nom rogné ou NULL + raison traduite (`_("The project name cannot be empty")`, `_("The project name is too long (128 characters at most)")`,
  `_("The project name cannot contain control characters")`).
- `gchar *gt_project_file_name_for(const gchar *name);` → nom de base `<nom>.gtkterm`, `/`, `\` et caractères de contrôle remplacés par `_`.
- `GtProject` : propriété `name` (s, NULL permise, notifiée si elle change) ; `const gchar *gt_project_get_stored_name(GtProject *p);` ;
  `void gt_project_set_name(GtProject *p, const gchar *name);` (rogné, vide → NULL, `queue_check`) ; `gt_project_get_name()` : nom stocké, sinon nom du
  fichier, sinon « Untitled project » ; `gt_project_capture()` copie le nom ; `gt_project_begin(…, saved)` prend `saved->name` (NULL sans `saved`) ;
  `gt_project_end()` le remet à NULL ;
  `gboolean gt_project_move_file(GtProject *p, const gchar *new_path, GError **error);` — projet ouvert avec fichier ; `new_path` = chemin actuel :
  TRUE sans rien faire ; cible existante : `G_FILE_ERROR_EXIST` (`_("A file named %s already exists.")`, nom de base) ; `g_rename` échoué : erreur
  `g_file_error_from_errno` avec `_("Cannot rename the project file: %s")` ; succès : `set_path`, tampon repris (`gt_file_stamp_take`), `stamp_known` TRUE.

- [ ] **Étape 1 : tests (échouent)** — `tests/test_project_file.c` :

```c
static void
test_nom(void)
{
	GtProjectData *d = gt_project_data_new(), *relu = NULL;
	gchar *dir = g_dir_make_tmp("gtkterm-pf-XXXXXX", NULL), *path = g_build_filename(dir, "p.gtkterm", NULL);
	gchar *snap, *err = NULL, *v;
	GError *error = NULL;

	g_ptr_array_add(d->tabs, gt_project_tab_new());
	snap = gt_project_data_snapshot(d);
	g_assert_null(strstr(snap, "name="));	/* absent sans nom */
	g_free(snap);
	d->name = g_strdup("Banc moteur");
	snap = gt_project_data_snapshot(d);
	g_assert_nonnull(strstr(snap, "name=Banc moteur"));	/* l'instantané le contient : renommer rend modifié */
	g_free(snap);
	g_assert_true(gt_project_data_save(d, path, NULL, &error));
	g_assert_true(gt_project_data_load(path, &relu, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(relu->name, ==, "Banc moteur");
	gt_project_data_free(relu);
	g_assert_true(g_file_set_contents(path, "[gtkterm-project]\nversion=1\nname=  \n\n[tab:0]\ntransport=serial\n", -1, NULL));
	g_assert_true(gt_project_data_load(path, &relu, NULL, &error));
	g_assert_null(relu->name);	/* vide après rognage : pas de nom */
	gt_project_data_free(relu);
	v = gt_project_name_validate("  A  ", &err);
	g_assert_cmpstr(v, ==, "A");
	g_free(v);
	g_assert_null(gt_project_name_validate("   ", &err));
	g_assert_nonnull(err);
	g_clear_pointer(&err, g_free);
	g_assert_null(gt_project_name_validate("a\tb", &err));
	g_clear_pointer(&err, g_free);
	v = gt_project_file_name_for("Banc a/b\\c");
	g_assert_cmpstr(v, ==, "Banc a_b_c.gtkterm");
	g_free(v);
	gt_project_data_free(d);
	g_remove(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}
```
  (Le transport exact attendu par `read_tab` : reprendre la forme utilisée par `test_modele_relatif` du même fichier si `transport=serial` ne suffit pas.)
  `tests/test_project.c` (avec `Fix`) :
  - `"/project/nom"` : `gt_project_begin(f.p, f.path, NULL, NULL)` puis `gt_project_save` ; `gt_project_get_name` = `"banc"` ; compteur sur
    `notify::name` ; `gt_project_set_name(f.p, " Banc ")` → stocké `"Banc"`, nom affiché `"Banc"`, une notification ; `gt_project_check_now` →
    modifié ; `gt_project_set_name(f.p, "")` → stocké NULL, nom affiché `"banc"` ; `gt_project_end` → stocké NULL.
  - `"/project/deplacer"` : projet enregistré sur `f.path` ; `gt_project_move_file(f.p, <dir>/neuf.gtkterm, &error)` → TRUE, `f.path` n'existe plus,
    le nouveau oui, `gt_project_get_path` = nouveau, puis `gt_project_save(f.p, NULL, FALSE, &error)` = `GT_PROJECT_SAVED` (pas de conflit : tampon
    repris) ; remettre le fichier à `f.path` en fin de test pour `fix_clear`.
  - `"/project/deplacer-refus"` : cible existante → FALSE, `G_FILE_ERROR_EXIST`, chemin inchangé, les deux fichiers intacts.
  - `"/project/ancien-melange-non-modifie"` (Review Focus 1) : écrire à la main un `.gtkterm` dont `[tab:0:macro:1..3]` ont les onglets T1, "", T1 ;
    `gt_project_data_load` → jeu regroupé ; installer ce jeu dans la session (`gt_macros_replace_set(gt_session_get_macros(f.a), gt_macro_set_copy(t->macros))`
    pour la seule session après avoir retiré `f.b` du gestionnaire, ou adapter avec deux onglets) puis `gt_project_begin(f.p, path, &stamp, d)` →
    `gt_project_is_modified` FALSE.

- [ ] **Étape 2 : lancer** — échec.
- [ ] **Étape 3 : implémenter** `project_file.c` : `write_key_file` : `if(d->name != NULL) g_key_file_set_string(kf, HEADER_GROUP, "name", d->name);`
  (instantané compris) ; `gt_project_data_load`, après `active` :
  `s = read_string(kf, HEADER_GROUP, "name", path); if(s != NULL && *g_strstrip(s) != '\0') d->name = s; else g_free(s);` ;
  `gt_project_data_free` : `g_free(d->name)`. `gt_project_name_validate` (`g_utf8_strlen` ≤ 128, refus si un caractère `g_unichar_iscntrl`) et
  `gt_project_file_name_for` (parcours UTF-8, `/`, `\` et `g_unichar_iscntrl` → `_`, puis `g_strconcat(…, ".gtkterm", NULL)`). Commentaires dans
  `project_file.h` (format : `name` facultatif dans l'en-tête).
- [ ] **Étape 4 : implémenter** `project.c` : champ `gchar *name;`, `PROP_NAME` (`g_param_spec_string("name", …, G_PARAM_READABLE)`), `set_name_internal()`
  (compare `g_strcmp0`, notifie), `gt_project_set_name` (rognage via `g_strstrip` d'une copie), `gt_project_get_stored_name`, `gt_project_get_name`
  (commence par `if(p->name != NULL) return g_strdup(p->name);` après le test `!p->open`), `gt_project_capture` : `d->name = g_strdup(p->name);`,
  `gt_project_begin` : `set_name_internal(p, saved != NULL ? saved->name : NULL);` **avant** la prise de l'instantané de capture,
  `gt_project_end` : `set_name_internal(p, NULL)`, finalisation : `g_free(p->name)`, `gt_project_move_file` comme décrit. Mettre à jour les commentaires
  de `project.h` (propriété `name`, nom affiché).
- [ ] **Étape 5 : lancer** — `meson test -C build` : PASS.
- [ ] **Étape 6 : commit** — `"projet : nom stocké dans le fichier, déplacement du fichier du projet"`.

---

### Tâche 7 : interface — « Rename project… »

**Fichiers :**
- Créer : `src/ui/project_rename.h`, `src/ui/project_rename.c`
- Modifier : `src/ui/app.c` (action, menu, titre sur `notify::name`, nom proposé par Save as), `src/ui/menus.ui`, `src/ui/meson.build` (source),
  `src/ui/test_hooks.c` (`stored_name` dans `test-project`), `po/POTFILES.in` (`src/ui/project_rename.c`)
- Test GUI : créer `tests/gui/test_projet_nom.py`, l'ajouter à `tests/gui/meson.build` (section projets, après `'test_projet_operations'`)

**Interfaces :**
- `void gt_project_rename_show(GtkWindow *parent, GtProject *project);` — une seule instance (pointeur faible statique : fenêtre modale unique,
  autorisé) ; fermée sans rien appliquer sur `notify::open` du projet passé à FALSE.
- Action `app.project-rename` (entrée dans `app_entries`, activée comme `project-close` dans `project_actions_update`, ignorée si
  `gt_app.pending_op != OP_NONE`).

- [ ] **Étape 1 : scénario GUI (échoue)** — `tests/gui/test_projet_nom.py` :

```python
# Nom de projet (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §1) : Rename project… sans fichier (nom dans le
# titre, projet modifié, Save as propose <nom>.gtkterm), avec déplacement du fichier, refus si la cible existe.
import os
from gtktest import *

e = Env('projet_nom')
e.write_config(e.general() + e.profile())
e.start()
check(not e.describe('project-rename')[0], 'sans projet : Rename project inactif')
e.action('project-new')
check(wait_until(lambda: e.project().get('open'), 5), 'projet ouvert')
e.action('project-rename')
e.find1(name='rename-project')
check(not e.find(name='project-rename-file'), 'sans fichier : pas de case de renommage du fichier')
e.set_text('project-name', 'Banc moteur')
e.activate('rename-ok')
check(wait_until(lambda: e.project().get('stored_name') == 'Banc moteur', 3), 'nom stocke : %r' % e.project())
check(wait_until(lambda: e.title().startswith('Banc moteur •'), 3), 'titre : %r' % e.title())
banc = e.project_path('Banc moteur.gtkterm')
save_project_as(e, banc)        # le sélecteur propose « Banc moteur.gtkterm » ; save_project_as tape le chemin complet
check(wait_until(lambda: e.project().get('path') == banc and not e.project().get('modified'), 5), 'enregistre : %r' % e.project())
check('name=Banc moteur' in open(banc).read(), 'cle name ecrite')
# Renommer avec le fichier
e.action('project-rename')
e.find1(name='project-rename-file')
e.set_text('project-name', 'Banc 2')
e.activate('rename-ok')
neuf = e.project_path('Banc 2.gtkterm')
check(wait_until(lambda: e.project().get('path') == neuf, 3), 'chemin deplace : %r' % e.project())
check(os.path.exists(neuf) and not os.path.exists(banc), 'fichier deplace')
check(e.project().get('modified') is True, 'nom change : projet modifie (fichier non reecrit)')
check('last_project=' + neuf in e.read_config(), 'last_project mis a jour')
# Cible existante : refus dans la fenêtre, rien ne change
open(e.project_path('Pris.gtkterm'), 'w').write('x')
e.action('project-rename')
e.set_text('project-name', 'Pris')
e.activate('rename-ok')
check(wait_until(lambda: 'already exists' in (e.find1(name='rename-error').get('label') or ''), 3), 'refus affiche')
check(e.project().get('path') == neuf and e.project().get('stored_name') == 'Banc 2', 'rien ne change : %r' % e.project())
e.activate('rename-cancel')
e.finish(quit=False)
```
  (Le texte exact de `last_project` dans `.gtktermrc` : reprendre la vérification de `test_projet_demarrage.py`. Fermer sans question : finir par
  `e.action('project-save')` avant `e.finish()` si la sortie demande Save / Don't save.)
- [ ] **Étape 2 : lancer** — `meson test -C build --suite gui test_projet_nom` : FAIL (action inconnue).
- [ ] **Étape 3 : `project_rename.c`** — copier la structure de `profile_dialog_open` (`src/ui/profile_dialogs.c:278`) : fenêtre `rename-project`, titre
  `_("Rename project")`, modale, transiente de `gt_app_get_main_window()` ; libellé `_("Project _name:")` + `GtkEntry` `project-name` (nom accessible
  « Project name »), texte = `gt_project_get_name(p)`, tout sélectionné, `activates-default` ; `GtkCheckButton` `project-rename-file`
  `_("Also rename the file")`, visible seulement si `gt_project_get_path(p) != NULL`, actif par défaut ; libellé `rename-error` (classe `error`, caché) ;
  boutons `rename-cancel` / `rename-ok` (`suggested-action`, défaut). À chaque `changed` : `gt_project_name_validate` → `error` sur l'entrée et OK
  insensible si invalide. OK :

```c
static void on_rename_ok(GtkButton *button, RenameDialog *rd)
{
	gchar *reason = NULL, *name = gt_project_name_validate(gtk_editable_get_text(rd->entry), &reason);
	const gchar *path = gt_project_get_path(rd->project);
	GError *error = NULL;

	if(name == NULL || !gt_project_is_open(rd->project))
	{
		show_error(rd, reason);
		g_free(reason);
		return;
	}
	if(path != NULL && gtk_widget_get_visible(rd->rename_file) && gtk_check_button_get_active(GTK_CHECK_BUTTON(rd->rename_file)))
	{
		gchar *dir = g_path_get_dirname(path), *base = gt_project_file_name_for(name), *target = g_build_filename(dir, base, NULL);
		gboolean ok = gt_project_move_file(rd->project, target, &error);

		if(ok && g_strcmp0(target, path) != 0)
			app_config_set_last_project(target);	/* relire → modifier → enregistrer (app_config) */
		g_free(target);
		g_free(base);
		g_free(dir);
		if(!ok)
		{
			show_error(rd, error->message);
			g_error_free(error);
			g_free(name);
			return;	/* rien ne change, la fenêtre reste ouverte */
		}
	}
	gt_project_set_name(rd->project, name);
	g_free(name);
	gtk_window_destroy(rd->window);
}
```
  Focus rendu au terminal à la fermeture : comme les autres fenêtres modales (`gt_app_focus_terminal()` ou équivalent utilisé par `profile_dialogs.c` ;
  reprendre le même appel). `#include "app_config.h"` pour `app_config_set_last_project` (vérifier son en-tête exact).
- [ ] **Étape 4 : `app.c`** — `action_project_rename` (`if(app_alive() && gt_project_is_open(gt_app.project) && gt_app.pending_op == OP_NONE)
  gt_project_rename_show(gt_app_get_main_window(), gt_app.project);`), entrée `{ .name = "project-rename", .activate = action_project_rename }`,
  `GSimpleAction *project_rename_action` gardé comme `project_close_action` et activé dans `project_actions_update` ; `on_project_notify` est déjà
  branché sur les notifications du projet : vérifier qu'il l'est sur `notify` sans détail ou ajouter `notify::name` ; `project_save` : quand
  `path == NULL` et `gt_project_get_stored_name()` non NULL, `name = gt_project_file_name_for(stored)` au lieu de `"project.gtkterm"`.
  `menus.ui` : élément `Rename project…` (`app.project-rename`) après `Save project as…`.
  `test_hooks.c` : `stored_name` (s, vide sans nom) dans `on_test_project` ; commentaire mis à jour.
- [ ] **Étape 5 : lancer** — `ninja -C build`, `meson test -C build --suite gui test_projet_nom` : OK ; puis `test_projet`, `test_projet_operations`,
  `test_menus_actions` (le menu File a un élément de plus : corriger l'attendu de ce scénario s'il énumère le menu).
- [ ] **Étape 6 : commit** — `"projet : Rename project… (nom dans le titre, renommage du fichier, Save as propose le nom)"`.

---

### Tâche 8 : `macro_row.c` — lignes facultatives, regroupement, déplacement borné, découpage du préfixe

**Fichiers :**
- Modifier : `src/macros_format.h`, `src/macros_format.c` (bornes d'un spécificateur), `src/ui/macro_row.h`, `src/ui/macro_row.c`
- Tests : `tests/test_macros_format.c`, `tests/test_macro_row.c`

**Interfaces :**
- `gboolean macro_arg_spec_bounds(const GtMacroLists *lists, const gchar *action, gint k, gint *start, gint *end, gint *label_start);` (cœur) —
  octets : `start` = `%` du spécificateur k, `end` = son dernier caractère (inclus), `label_start` = `[` de son libellé ou -1 ; FALSE si k n'existe pas.
- `GtMacroRow` : propriétés `arg-optional`, `arg-prefix` (STRV, même contrat que `arg-min`) ; `gt_macro_row_get_arg_optional/prefix()` ;
  `void gt_macro_row_set_optional(GtMacroRow *row, const gchar *const *optional, const gchar *const *prefix);` ; `gt_macro_rows_from_set` et
  `gt_macro_rows_to_edits` les portent.
- `typedef const gchar *(*GtRowKeyFunc)(gpointer item);` ; `const gchar *gt_macro_row_tab_key(gpointer item);` (tab d'une `GtMacroRow`) ;
  `const gchar *gt_macro_list_row_key(gpointer item);` (list d'une `GtMacroListRow`).
- `gboolean gt_macro_rows_group(GListStore *store, GtRowKeyFunc key);` — même règle que la tâche 1, par `g_list_store_splice` (une seule fois, tout le
  modèle), TRUE si changé.
- `gboolean gt_macro_rows_move_grouped(GListStore *store, guint *positions, guint n, gint delta, GtRowKeyFunc key);` — comme `gt_macro_rows_move`, mais
  refusé (FALSE, rien ne bouge) si la suite des groupes changerait.
- `gboolean gt_macro_action_split_prefix(const GtMacroLists *lists, const gchar *action, gint k, gchar **new_action, gchar **prefix);` et
  `gchar *gt_macro_action_join_prefix(const GtMacroLists *lists, const gchar *action, gint k, const gchar *prefix);` (spec §5.4 et §7 bis).

- [ ] **Étape 1 : tests (échouent)** — `tests/test_macros_format.c` : `macro_arg_spec_bounds(NULL, "a [Pw]%s b%d", 0, &s, &e, &l)` → s=6, e=7, l=2 ;
  k=1 → s=10, e=11, l=-1 ; k=2 → FALSE ; `"%%%d"` k=0 → s=2.
  `tests/test_macro_row.c` (réutiliser `ordre()` existant) :

```c
static GtMacroRow *ligne(const gchar *label, const gchar *tab)
{
	return gt_macro_row_new(0, label, "", "X", tab);
}

static void test_regroupe(void)
{
	GListStore *store = g_list_store_new(GT_TYPE_MACRO_ROW);
	const gchar *labels[] = { "a", "b", "c", "d", "e" }, *tabs[] = { "T1", "", "T1", "T2", "" };
	gchar *o;

	for(guint i = 0; i < 5; i++)
	{
		GtMacroRow *r = ligne(labels[i], tabs[i]);

		g_list_store_append(store, r);
		g_object_unref(r);
	}
	g_assert_true(gt_macro_rows_group(store, gt_macro_row_tab_key));
	o = ordre(store);
	g_assert_cmpstr(o, ==, "a,c,b,e,d");
	g_free(o);
	g_assert_false(gt_macro_rows_group(store, gt_macro_row_tab_key));
	g_object_unref(store);
}

static void test_deplacement_borne(void)
{
	GListStore *store = g_list_store_new(GT_TYPE_MACRO_ROW);
	const gchar *labels[] = { "a", "c", "b", "e" }, *tabs[] = { "T1", "T1", "", "" };
	guint pos[1];
	gchar *o;

	for(guint i = 0; i < 4; i++)
	{
		GtMacroRow *r = ligne(labels[i], tabs[i]);

		g_list_store_append(store, r);
		g_object_unref(r);
	}
	pos[0] = 1;	/* c, dernier de T1 : ne descend pas dans "" */
	g_assert_false(gt_macro_rows_move_grouped(store, pos, 1, +1, gt_macro_row_tab_key));
	g_assert_cmpuint(pos[0], ==, 1);
	g_assert_true(gt_macro_rows_move_grouped(store, pos, 1, -1, gt_macro_row_tab_key));	/* dans son groupe : oui */
	g_assert_cmpuint(pos[0], ==, 0);
	o = ordre(store);
	g_assert_cmpstr(o, ==, "c,a,b,e");
	g_free(o);
	pos[0] = 2;	/* b, premier de "" : ne monte pas */
	g_assert_false(gt_macro_rows_move_grouped(store, pos, 1, -1, gt_macro_row_tab_key));
	g_object_unref(store);
}

static void test_decoupe_prefixe(void)
{
	const struct { const gchar *action; gint k; const gchar *apres, *prefixe; } cas[] = {
		{ "connect %s -password=%s", 1, "connect %s%s", " -password=" },
		{ "connect %s", 0, "connect %s", "" },	/* précédé d'un espace : rien */
		{ "AT+X=%d,%d", 1, "AT+X=%d%d", "," },	/* sans espace : jusqu'au spécificateur précédent */
		{ "%s", 0, "%s", "" },	/* en tête */
		{ "go -r=100%%[Rate]%d", 0, "go[Rate]%d", " -r=100%" },	/* libellé gardé avec son spécificateur ; %% → % */
		{ "a\\ b%s", 0, "a\\ b%s", "" },	/* espace échappé : pas de coupe */
		{ "-a=%s", 0, "-a=%s", "" },	/* premier argument sans espace avant : rien */
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		gchar *apres = NULL, *prefixe = NULL, *rejoint;

		g_assert_true(gt_macro_action_split_prefix(NULL, cas[i].action, cas[i].k, &apres, &prefixe));
		g_assert_cmpstr(apres, ==, cas[i].apres);
		g_assert_cmpstr(prefixe, ==, cas[i].prefixe);
		rejoint = gt_macro_action_join_prefix(NULL, apres, cas[i].k, prefixe);
		g_assert_cmpstr(rejoint, ==, cas[i].action);	/* aller-retour exact */
		g_free(rejoint);
		g_free(apres);
		g_free(prefixe);
	}
	g_assert_false(gt_macro_action_split_prefix(NULL, "x%d", 3, NULL, NULL));	/* argument inexistant */
}
```
  Règles (§7 bis), dans l'ordre : point d'ancrage = `[` du libellé s'il existe, sinon le `%` ; borne basse = fin du spécificateur précédent + 1 (0 pour
  k = 0) ; caractère juste avant l'ancrage = espace → rien ; sinon remonter jusqu'au premier espace ≥ borne basse (compris) ; espace trouvé mais précédé
  d'un antislash → rien ; aucun espace : k > 0 → tout depuis la borne basse, k = 0 → rien. Ajouter aussi
  `test_lignes_facultatives` : `gt_macro_rows_from_set` d'un jeu avec Optional/Prefix → propriétés de la ligne ; `gt_macro_rows_to_edits` les rend ;
  `gt_macro_row_set_optional` ne notifie que si changé. Enregistrer les quatre tests.
- [ ] **Étape 2 : lancer** — échec de compilation.
- [ ] **Étape 3 : `macro_arg_spec_bounds`** (macros_format.c, style GNU) : même parcours que `macro_get_arg_infos` (saute `%%`, `try_parse_list_spec`,
  sinon `parse_one_spec`), en comptant les spécificateurs ; au k-ième : `*start = i`, `*end = end`, `*label_start = try_parse_label(action, i, NULL)` ;
  pointeurs de sortie nullables.
- [ ] **Étape 4 : `macro_row.c`** — propriétés `ROW_PROP_ARG_OPTIONAL`, `ROW_PROP_ARG_PREFIX` dans `row_strv_field`, `class_init`, `finalize` ;
  getters, `gt_macro_row_set_optional` (freeze/thaw comme `set_limits`) ; `rows_from_set` : `"arg-optional", m->arg_optional, "arg-prefix", m->arg_prefix` ;
  `rows_to_edits` : `edits[i].arg_optional = …; edits[i].arg_prefix = …;`.
  Regroupement :

```c
gboolean gt_macro_rows_group(GListStore *store, GtRowKeyFunc key)
{
	guint n = g_list_model_get_n_items(G_LIST_MODEL(store));
	gpointer *items = g_new(gpointer, MAX(n, 1)), *sorted = g_new(gpointer, MAX(n, 1));
	GPtrArray *keys = g_ptr_array_new();
	guint m = 0;
	gboolean changed = FALSE;

	for(guint i = 0; i < n; i++)
	{
		items[i] = g_list_model_get_item(G_LIST_MODEL(store), i);
		if(!g_ptr_array_find_with_equal_func(keys, key(items[i]), g_str_equal, NULL))
			g_ptr_array_add(keys, (gpointer)key(items[i]));
	}
	for(guint k = 0; k < keys->len; k++)
		for(guint i = 0; i < n; i++)
			if(strcmp(key(items[i]), g_ptr_array_index(keys, k)) == 0)
				sorted[m++] = items[i];
	for(guint i = 0; i < n; i++)
		changed |= sorted[i] != items[i];
	if(changed)
		g_list_store_splice(store, 0, n, sorted, n);
	for(guint i = 0; i < n; i++)
		g_object_unref(items[i]);
	g_ptr_array_unref(keys);
	g_free(sorted);
	g_free(items);
	return changed;
}
```
  Déplacement borné : construire `gchar **keys` (copies) des n_items clés, simuler les échanges de `gt_macro_rows_move` sur ce tableau, comparer la suite
  des groupes (clés consécutives distinctes, fonction locale `group_sequence()` → `GPtrArray`) avant / après ; égale → `gt_macro_rows_move(store, positions,
  n, delta)` ; sinon FALSE. Positions au bord : `gt_macro_rows_move` refuse déjà.
  Découpage :

```c
/* Préfixe d'un argument facultatif (spec §5.4, §7 bis) */
gboolean gt_macro_action_split_prefix(const GtMacroLists *lists, const gchar *action, gint k, gchar **new_action, gchar **prefix)
{
	gint start, end, label, anchor, lower = 0, prev_end, cut;
	GString *pre, *rest;

	if(!macro_arg_spec_bounds(lists, action, k, &start, &end, &label))
		return FALSE;
	if(new_action == NULL || prefix == NULL)
		return TRUE;
	anchor = label >= 0 ? label : start;
	if(k > 0 && macro_arg_spec_bounds(lists, action, k - 1, NULL, &prev_end, NULL))
		lower = prev_end + 1;
	for(cut = anchor - 1; cut >= lower && action[cut] != ' '; cut--)
		;
	if(anchor > lower && action[anchor - 1] == ' ')
		cut = anchor;	/* précédé d'un espace : rien à prendre */
	else if(cut >= lower && cut > 0 && action[cut - 1] == '\\')
		cut = anchor;	/* espace échappé : pas de coupe */
	else if(cut < lower)
		cut = k > 0 ? lower : anchor;	/* aucun espace : depuis le spécificateur précédent, rien pour le premier argument */
	pre = g_string_new(NULL);
	for(gint i = cut; i < anchor; i++)
	{
		if(action[i] == '%' && i + 1 < anchor && action[i + 1] == '%')
			i++;
		g_string_append_c(pre, action[i]);
	}
	rest = g_string_new_len(action, cut);
	g_string_append(rest, action + anchor);
	*prefix = g_string_free(pre, FALSE);
	*new_action = g_string_free(rest, FALSE);
	return TRUE;
}
```
  Réinsertion : `prefix` avec chaque `%` doublé, inséré à `anchor` du spécificateur k de `action` (bornes recalculées).
- [ ] **Étape 5 : lancer** — `meson test -C build` : PASS (`test_macro_row`, `test_macros_format`).
- [ ] **Étape 6 : commit** — `"Configure Macros (logique) : lignes facultatives, regroupement, déplacement borné au groupe, découpage du préfixe"`.

---

### Tâche 9 : éditeur — filtres, regroupement, mise en page

**Fichiers :**
- Modifier : `src/ui/macro_editor.c`
- Tests GUI : créer `tests/gui/test_macro_editor_filtre.py` ; mettre à jour au besoin `test_macro_editor_ordre.py`, `test_macro_editor_colonnes.py`,
  `test_macros_edition.py` (ordre regroupé, boutons déplacés) ; `tests/gui/meson.build`

**Interfaces :**
- État `MacroEditor` : `GtkFilterListModel *filtered[2]; GtkCustomFilter *filter[2]; GtkWidget *filter_dd[2]; GPtrArray *filter_keys[2];`
  (clé par élément de la liste déroulante, `[0]` = NULL pour « All »), `gchar *filter_key[2]` (NULL : All), `gboolean filter_rebuilding;`,
  `GtkWidget *settings_separator;` (pointeur faible).
- Fonctions internes (toutes dans `macro_editor.c`) : `selected_store_positions(editor, page, &n)` (positions **du store**, croissantes),
  `reselect_items(editor, page, store_pos, n)`, `filter_keys_rebuild(editor, page)`, `regroup(editor, page)`, `settings_visibility_update(editor)`.

- [ ] **Étape 1 : scénario GUI (échoue)** — `tests/gui/test_macro_editor_filtre.py` :

```python
# Configure Macros, filtres et regroupement (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §2.3, §3, §6) :
# lignes regroupées à l'ouverture ; filtre Tab: ; Add sous filtre crée la macro dans l'onglet ; changer la cellule Tab regroupe ; Move borné au groupe ;
# filtre List: et Add Entry ; boutons sous le tableau, séparateur avant la table des arguments.
from gtktest import *

e = Env('macro_editor_filtre')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='a', action='A', tab='T1'), dict(label='b', action='B'), dict(label='c', action='C%d', tab='T1')],
                lists=[('L1', [('x', '1')]), ('L2', [('y', '2')])])
macro_config(e, ini)
e.start()
open_editor(e)
check([r['label'] for r in editor_rows(e)] == ['a', 'c', 'b'], 'regroupe : %r' % [r['label'] for r in editor_rows(e)])
e.set_text('macro-tab-filter', 'T1')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'c'], 3), 'filtre T1')
e.activate('macro-add')
check(wait_until(lambda: len(editor_rows(e)) == 3 and editor_rows(e)[2]['tab'] == 'T1', 3), 'Add sous filtre : onglet T1 : %r' % editor_rows(e))
e.set_text('macro-tab-filter', 'General')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['b'], 3), 'filtre General')
e.set_text('macro-tab-filter', 'All')
# Move borné : « c » (dernier de T1 avant la nouvelle) … la nouvelle ligne, dernière de T1, ne descend pas dans General
editor_select(e, 2)
e.activate('macro-down')
check([r['label'] for r in editor_rows(e)][3] == 'b', 'Move Down refuse au bord du groupe')


def editer_tab(rang, texte):
    """Édition réelle de la cellule Tab de la ligne rang (GtkEditableLabel) : clic, Entrée ouvre l'édition, Ctrl+A, saisie, Entrée valide."""
    e.bw.click(editor_rows(e)[rang]['cells']['tab'])
    e.bw.key('Return')
    e.bw.key('a', mods=('ctrl',))
    e.bw.type(texte)
    e.bw.key('Return')


editer_tab(3, 'T1')                      # « b » passe dans T1 : regroupé à la fin de l'édition
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'c', '', 'b'], 3), 'regroupe apres edition : %r' % editor_rows(e))
# Review Focus 4 : filtre T1, toutes les lignes T1 (4) passent en T3 → le filtre revient à All
e.set_text('macro-tab-filter', 'T1')
for _ in range(4):
    editer_tab(0, 'T3')
check(wait_until(lambda: len(editor_rows(e)) == 4, 3), 'filtre revenu a All : 4 lignes visibles')
e.activate('macro-delete')               # aucune sélection : rien n'est supprimé
check(len(editor_rows(e)) == 4, 'Delete sans selection : rien supprime')
# Mise en page : boutons sous le tableau, séparateur au-dessus de la table des arguments
d = e.dump()
liste, add, sep = (e.find1(name=n) for n in ('macro-list', 'macro-add', 'macro-settings-separator'))
check(add['y'] > liste['y'] and sep['y'] > add['y'], 'boutons sous le tableau, separateur dessous')
# Lists
e.activate('Lists')
e.set_text('lists-filter', 'L2')
check(wait_until(lambda: [r['list'] for r in editor_rows(e, 'list-cell', ('list', 'display', 'value'))] == ['L2'], 3), 'filtre L2')
e.activate('lists-add')
check(wait_until(lambda: [r['list'] for r in editor_rows(e, 'list-cell', ('list', 'display', 'value'))] == ['L2', 'L2'], 3), 'Add Entry dans L2')
e.activate('macro-cancel')
e.finish()
```
  Si Entrée n'ouvre pas l'édition d'un `GtkEditableLabel` sous broadway, utiliser deux clics rapprochés (`e.bw.click(cell, settle=0.05)` deux fois).
  Sous filtre T1, une ligne passée en T3 sort de la vue : on édite toujours le rang 0 ; à la quatrième, plus aucune ligne T1, le filtre revient à All.
- [ ] **Étape 2 : lancer** — `meson test -C build --suite gui test_macro_editor_filtre` : FAIL (`macro-tab-filter` introuvable).
- [ ] **Étape 3 : modèle filtré et sélection** — dans `build_page` : `editor->filter[page] = gtk_custom_filter_new(page == PAGE_MACROS ? macro_matches :
  list_matches, editor, NULL);` ; `editor->filtered[page] = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(page_store(editor, page))),
  GTK_FILTER(g_object_ref(editor->filter[page])));` ; la `GtkMultiSelection` prend `g_object_ref(editor->filtered[page])`.

```c
static gboolean macro_matches(gpointer item, gpointer data)
{
	MacroEditor *editor = data;

	return editor->filter_key[PAGE_MACROS] == NULL || strcmp(gt_macro_row_get_tab(item), editor->filter_key[PAGE_MACROS]) == 0;
}

/* Positions dans le store des lignes sélectionnées (vue filtrée), croissantes ; g_free */
static guint *selected_store_positions(MacroEditor *editor, gint page, guint *n_out)
{
	GtkSelectionModel *sel = page_selection(editor, page);
	guint n = 0, *pos = selected_positions(sel, &n);

	for(guint k = 0; k < n; k++)
	{
		gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), pos[k]);

		g_list_store_find(page_store(editor, page), item, &pos[k]);
		g_object_unref(item);
	}
	qsort(pos, n, sizeof *pos, compare_guint);
	*n_out = n;
	return pos;
}
```
  `reselect_items` : désélectionne tout, puis pour chaque position du store cherche l'objet dans la vue (`g_list_model_get_item` sur `sel` jusqu'à
  identité) et le sélectionne. Remplacer **tous** les `selected_positions(editor->selection…)` / `page_selection` + `g_list_model_get_item(editor->rows, …)`
  de `on_add`, `on_delete`, `move_selected`, `on_unshortcut`, `on_capture`, `defaults_rebuild` par `selected_store_positions` ; `move_selected` appelle
  `gt_macro_rows_move_grouped(store, pos, n, delta, page == PAGE_MACROS ? gt_macro_row_tab_key : gt_macro_list_row_key)` puis `reselect_items`.
  `editor_free` : `g_clear_object` des filtres et modèles filtrés, `g_ptr_array_unref` des clés, `g_free` des `filter_key`.
- [ ] **Étape 4 : listes déroulantes de filtre** — `build_page` ajoute en tête une boîte horizontale : libellé `_("Tab:")` / `_("List:")` +
  `GtkDropDown` `macro-tab-filter` / `lists-filter` (nom accessible `_("Tab filter")` / `_("List filter")`) ; `notify::selected` → si
  `!editor->filter_rebuilding` : `filter_key[page]` = clé choisie (NULL pour 0), `gtk_filter_changed(filter, GTK_FILTER_CHANGE_DIFFERENT)`.
  `filter_keys_rebuild(editor, page)` : clés distinctes du store dans l'ordre (groupe vide affiché `_("General")`, page Macros seulement), nouveau
  `GtkStringList` (`"All"` traduit `_("All")` en tête), `filter_rebuilding = TRUE`, `gtk_drop_down_set_model`, sélection = index de `filter_key[page]`
  s'il existe encore, sinon 0 et `filter_key[page] = NULL` + `gtk_filter_changed`, `filter_rebuilding = FALSE`. Appelée à la construction et sur
  `items-changed` de chaque store (gestionnaire déconnecté dans `on_destroy`, comme `on_defaults_items_changed`).
- [ ] **Étape 5 : regroupement** — `regroup(editor, page)` : `pos = selected_store_positions` → objets gardés (refs) → `gt_macro_rows_group(store, key)`
  → positions retrouvées par `g_list_store_find` → `reselect_items` ; puis `filter_keys_rebuild` et `gtk_filter_changed`. Appelé : à l'ouverture (après
  la construction ; le jeu est déjà regroupé, sans effet sauf jeu ancien en mémoire), après `on_add`, et dans `on_cell_editing` quand `editing` passe à
  FALSE et que `spec->prop` vaut `"tab"` ou `"list"` (réorganiser `on_cell_editing` : il ne retourne plus tôt sur `!editing`).
- [ ] **Étape 6 : Add** — `on_add` : clé du nouveau = `filter_key[page]` si filtre, sinon clé de la dernière ligne sélectionnée (aucune : `""` pour une
  macro, `"NewList"` pour une entrée) ; position : après la dernière ligne sélectionnée si elle est de cette clé, sinon après la dernière ligne de cette
  clé dans le store, sinon à la fin ; macro : `gt_macro_row_new(0, "", "", "", key)` ; entrée : `gt_macro_list_row_new(key, "display", "value")` ; puis
  `regroup` et sélection de la nouvelle ligne (`reselect_items`).
- [ ] **Étape 7 : mise en page** — `build_page`, ordre : boîte filtre ; défilement du tableau ; `row1` ; `row2` ; (page Macros) séparateur
  `macro-settings-separator` (`gtk_widget_set_margin_top(sep, 4)`), boîte des réglages (marge haute 12 px) contenant `build_defaults_box` et
  `build_preview_box`. `settings_visibility_update(editor)` : séparateur visible si `defaults_scroll` ou `preview_box` visible ; appelée à la fin de
  `defaults_rebuild` et de `preview_update`. Taille par défaut `900, 640`.
- [ ] **Étape 8 : lancer** — `ninja -C build` ; `meson test -C build --suite gui test_macro_editor_filtre` : OK ; puis toute la suite GUI
  `meson test -C build --suite gui` : corriger les scénarios existants dont l'attendu dépend de l'ancien ordre ou de la place des boutons (citer chaque
  scénario corrigé dans le message de commit) ; jamais affaiblir un contrôle sans raison écrite.
- [ ] **Étape 9 : commit** — `"Configure Macros : filtres Tab et List, lignes regroupées, boutons sous le tableau et séparateur"`.

---

### Tâche 10 : éditeur — ligne du fichier visé et Reload

**Fichiers :**
- Modifier : `src/ui/macro_editor.c`
- Tests GUI : créer `tests/gui/test_macro_editor_recharger.py` ; mettre à jour `tests/gui/test_macros_modele.py` (`macro-template-info` →
  `macro-source-label`) ; `tests/gui/meson.build`

**Interfaces :**
- Widgets : boîte `macro-source`, libellé `macro-source-label` (nom accessible `_("Macros file")`), bouton `macro-reload` `_("Reload")`.
- État : `GtkWidget *source_label, *reload;` (pointeurs faibles, remplacent `template_info`), `gboolean reloading;`.
- `gt_macro_editor_macros_changed(s)` : si `editor->reloading` → `editor_reload_rows(editor, gt_macros_get_set(m))` ; sinon règle actuelle.

- [ ] **Étape 1 : scénario GUI (échoue)** — `tests/gui/test_macro_editor_recharger.py` :

```python
# Configure Macros, fichier visé et Reload (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §4) :
# onglet libre : « File: <chemin> » ; fichier réécrit sur le disque → Reload sans modification : lignes relues, éditeur ouvert ; avec une ligne
# modifiée : question, Cancel garde les lignes, Reload les jette ; onglet de projet non lié : « Embedded in the project », pas de bouton.
from gtktest import *

e = Env('macro_editor_recharger')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='a', action='A')])
macro_config(e, ini)
e.start()
open_editor(e)
check(e.find1(name='macro-source-label').get('label') == 'File: ' + ini, 'libelle du fichier')
write_macros_v2(ini, [dict(label='a', action='A'), dict(label='z', action='Z')])
e.activate('macro-reload')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'z'], 3), 'relu sans question')
check(e.find(name='macro-editor'), 'editeur reste ouvert')
e.activate('macro-add')                       # ligne modifiée
write_macros_v2(ini, [dict(label='q', action='Q')])
e.activate('macro-reload')
check(wait_until(lambda: any('Discard your changes' in a for a in e.alerts()), 3), 'question posee')
e.close_alert('Cancel')
check(len(editor_rows(e)) == 3, 'Cancel : lignes gardees')
e.activate('macro-reload')
check(wait_until(lambda: e.alerts(), 3), 'question reposee')
e.close_alert('Reload')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['q'], 3), 'Reload : lignes du disque')
e.activate('macro-cancel')
save_project_as(e, e.project_path('p.gtkterm'))
check(wait_until(lambda: e.sessions()[0]['in_project'], 5), 'onglet de projet')
open_editor(e)
check(e.find1(name='macro-source-label').get('label') == 'Embedded in the project', 'integre au projet')
check(not e.find(name='macro-reload'), 'pas de bouton Reload')
e.activate('macro-cancel')
e.finish()
```
  Ajouter (Review Focus 5) un cas onglet lié : réutiliser le début de `test_macros_modele.py` (Link as template), réécrire le modèle sur le disque,
  Reload → `Template: <chemin>` toujours affiché, lignes relues, éditeur ouvert.
- [ ] **Étape 2 : lancer** — FAIL (`macro-source-label` introuvable).
- [ ] **Étape 3 : ligne du fichier** — remplacer `template_info` par la boîte (libellé + bouton) ; `template_info_update` devient `source_update(editor, m)` :

```c
static void source_update(MacroEditor *editor, GtMacros *macros)
{
	GtSession *s = g_weak_ref_get(&editor->session);
	const gchar *tpl = macros != NULL ? gt_macros_get_template(macros) : NULL;
	const gchar *path = macros != NULL ? gt_macros_get_path(macros) : NULL;
	gboolean in_project = s != NULL && gt_session_get_in_project(s);
	gchar *text;

	if(editor->source_label == NULL)
	{
		g_clear_object(&s);
		return;
	}
	if(tpl != NULL)
		text = g_strdup_printf(_("Template: %s"), tpl);
	else if(in_project)
		text = g_strdup(_("Embedded in the project"));
	else if(path != NULL)
		text = g_strdup_printf(_("File: %s"), path);
	else
		text = g_strdup(_("No macros file"));
	gtk_label_set_text(GTK_LABEL(editor->source_label), text);
	if(editor->reload != NULL)
		gtk_widget_set_visible(editor->reload, tpl != NULL || (!in_project && path != NULL));
	g_free(text);
	g_clear_object(&s);
}
```
  Appelée là où `template_info_update` l'était (ouverture, `on_session_edited`, `gt_macro_editor_macros_changed`).
- [ ] **Étape 4 : Reload** — `on_reload` : rien si `editor->asking` ; `kind = gt_macros_classify_edits(macros, edits, n, lists)` (lignes de l'éditeur) ;
  `NONE` → `do_reload(editor)` ; sinon question `gt_ui_choose(window, _("Discard your changes and reload the macros file?"), path, labels {Reload,
  Cancel}, 1, 0, on_reload_choice, q)` où `q` porte `GWeakRef` fenêtre + génération (même modèle que `TemplateQuestion`), `editor->asking = TRUE` ; la
  réponse remet `asking` à FALSE, et n'appelle `do_reload` que si fenêtre vivante, génération inchangée et choix 0.
  `do_reload` : `stop_capture` ; `editor->reloading = TRUE` ; `ok = gt_macros_get_template(m) != NULL ? gt_macros_reload_template(m) : gt_macros_reload(m)` ;
  `editor->reloading = FALSE` ; si le jeu n'a pas été remplacé (rechargement en place d'un modèle à structure inchangée), appeler quand même
  `editor_reload_rows(editor, gt_macros_get_set(m))` ; échec → `set_message(editor, _("Cannot reload the macros file."), TRUE)` (le message détaillé
  passe déjà par le crochet `message`).
  `editor_reload_rows(editor, set)` : `editor->set = set` ; stores remplis à nouveau par `g_list_store_splice` depuis `gt_macro_rows_from_set(set)` et
  `gt_macro_list_rows_from_lists(...)` (objets copiés d'un store temporaire) ; `regroup` des deux pages (filtre gardé si sa clé existe) ;
  `defaults_rebuild` ; `source_update`.
  Bouton `macro-reload` insensible pendant `asking` (mettre à jour à chaque changement d'`asking` : fonction `asking_set(editor, value)` utilisée par
  `ask_template`, `on_template_choice` et la question de Reload).
- [ ] **Étape 5 : lancer** — `meson test -C build --suite gui test_macro_editor_recharger test_macros_modele` : OK.
- [ ] **Étape 6 : commit** — `"Configure Macros : fichier visé (modèle, .ini ou intégré) et bouton Reload, éditeur gardé ouvert"`.

---

### Tâche 11 : éditeur — colonnes Optional et Prefix, aperçu « ∅ »

**Fichiers :**
- Modifier : `src/ui/macro_editor.c`
- Tests GUI : compléter `tests/gui/test_macros_facultatifs.py` (partie éditeur)

**Interfaces :**
- Colonnes `COL_OPTIONAL`, `COL_PREFIX` ajoutées après `COL_CHARSET` (avant `N_COLS`), en-têtes `_("Optional")`, `_("Prefix")` ; widgets
  `macro-optional-<k>` (`GtkCheckButton`), `macro-prefix-<k>` (`GtkEntry`, insensible si la case est décochée).

- [ ] **Étape 1 : scénario (échoue)** — à la fin de `test_macros_facultatifs.py`, avant `e.finish()` :

```python
# Éditeur : cocher Optional découpe le préfixe hors de l'action, décocher le remet ; aperçu ∅
write_macros_v2(ini, [dict(label='Pw', action='login %s -password=%s;')])
e.action('load-macros')
file_dialog(e, 'Load macros file', ini)
open_editor(e)
editor_select(e, 0)
check(wait_until(lambda: bool(e.find(name='macro-optional-1')), 3), 'colonne Optional')
e.activate('macro-optional-1')
check(wait_until(lambda: editor_rows(e)[0]['action'] == 'login %s%s;', 3), 'action decoupee : %r' % editor_rows(e)[0]['action'])
check(e.find1(name='macro-prefix-1').get('label', e.find1(name='macro-prefix-1').get('text')) in (' -password=', None), 'prefixe')
check(wait_until(lambda: '∅' in (e.find1(name='macro-preview').get('label') or ''), 3), 'apercu : argument omis')
e.activate('macro-optional-1')
check(wait_until(lambda: editor_rows(e)[0]['action'] == 'login %s -password=%s;', 3), 'decoche : prefixe remis')
e.activate('macro-optional-1')
e.activate('macro-ok')
# Envoi depuis le panneau avec le champ vide : le préfixe ne part pas
pw = macro_id(e, 'Pw')
e.set_text('macro-%d-arg-0' % pw, 'bob')
e.activate('macro-%d-arg-0' % pw)
e.drain()
e.activate('macro-%d' % pw)
check(wait_until(lambda: b'login bob;' in e.read_pty(0.3), 3), 'facultatif vide apres edition : omis')
```
  (Le dump expose le texte d'une `GtkEntry` sous la clé utilisée par les autres scénarios — vérifier dans `test_macros_limites.py` et ajuster la lecture
  du préfixe.)
- [ ] **Étape 2 : lancer** — FAIL.
- [ ] **Étape 3 : table** — dans `defaults_rebuild`, pour chaque argument : case `gtk_check_button_new()` (`name_field(w, "macro-optional", k)`), active
  si `nth(optional, k)` vaut `"1"` ; entrée préfixe (`limit_entry`-like, largeur 12, texte `nth(prefix, k)`, `name_field(…, "macro-prefix", k)`),
  sensible si cochée. `toggled` → `on_optional_toggled(k)` :

```c
static void on_optional_toggled(GtkCheckButton *check, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(check));
	gint k = GPOINTER_TO_INT(data);
	GtMacroLists *lists;
	gchar *action = NULL, *prefix = NULL;
	gboolean on = gtk_check_button_get_active(check);

	if(editor == NULL || editor->defaults_building || editor->defaults_row == NULL)
		return;
	lists = gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows));
	if(on && gt_macro_action_split_prefix(lists, gt_macro_row_get_action(editor->defaults_row), k, &action, &prefix))
		optional_write(editor, k, TRUE, prefix, action);	/* vecteurs de la ligne + action (la table se refait sur notify::action) */
	else if(!on)
	{
		const gchar *old = nth(gt_macro_row_get_arg_prefix(editor->defaults_row), k);

		action = gt_macro_action_join_prefix(lists, gt_macro_row_get_action(editor->defaults_row), k, old);
		optional_write(editor, k, FALSE, "", action);
	}
	g_free(action);
	g_free(prefix);
	gt_macro_lists_free(lists);
}
```
  `optional_write` : construit les vecteurs de n = `defaults_n` éléments depuis ceux de la ligne (`row_vector` ou `""`), pose le rang k, appelle
  `gt_macro_row_set_optional` **puis** `g_object_set(row, "action", action, NULL)` (l'ordre garde les longueurs cohérentes ; la reconstruction de la table
  sur `notify::action` relit les vecteurs). `changed` de l'entrée préfixe → vecteur `arg_prefix` réécrit (préfixe toujours valide).
  `on_defaults_row_notify` : `arg-optional` / `arg-prefix` → `preview_update`.
  Message de troncature (spec §5.1) : dans `on_defaults_row_notify` sur `action`, avant `defaults_rebuild`, si la ligne a un `arg_prefix` plus long que le
  nouveau nombre d'arguments avec un élément non vide au-delà → `set_message(editor, "Prefix of argument %d removed", TRUE)` (premier rang perdu, 1-based),
  et vecteurs de la ligne ajustés (tronqués) tout de suite.
- [ ] **Étape 4 : aperçu** — `preview_update` : `gt_macro_set_set_optional(set, id, row_vector(optional, n), row_vector(prefix, n), n)` après les limites ;
  `invalid[k]` FALSE pour un argument facultatif vide ; `preview_color` : `GT_PREVIEW_OMITTED` → `"#77767b"` (gris) ; un run OMITTED ne compte pas
  d'octet (les octets viennent de `bytes`, déjà sans lui).
- [ ] **Étape 5 : lancer** — `meson test -C build --suite gui test_macros_facultatifs` : OK ; `meson test -C build` : PASS.
- [ ] **Étape 6 : commit** — `"Configure Macros : colonnes Optional et Prefix (découpage automatique du préfixe), aperçu d'un argument omis"`.

---

### Tâche 12 : traductions, documentation, vérification complète, poussée

**Fichiers :**
- Modifier : `po/POTFILES.in`, `po/fr.po`, `CLAUDE.md`, `NEWS`, `tests/README.md` (liste des scénarios s'il en tient une)

- [ ] **Étape 1 : traductions** — `ninja -C build gtkterm-pot gtkterm-update-po` ; traduire dans `po/fr.po` chaque nouveau msgid, par exemple :
  « Rename project… » → « Renommer le projet… », « Rename project » → « Renommer le projet », « Project _name: » → « _Nom du projet : »,
  « Also rename the file » → « Renommer aussi le fichier », « A file named %s already exists. » → « Un fichier nommé %s existe déjà. »,
  « Cannot rename the project file: %s » → « Impossible de renommer le fichier du projet : %s », « The project name cannot be empty » → « Le nom du projet
  ne peut pas être vide », « Tab: » → « Onglet : », « List: » → « Liste : », « All » → « Tous », « Tab filter » → « Filtre d'onglet », « List filter » →
  « Filtre de liste », « Macros file » → « Fichier de macros », « File: %s » → « Fichier : %s », « Embedded in the project » → « Intégré au projet »,
  « No macros file » → « Aucun fichier de macros », « Reload » → « Recharger » (msgid existant : garder sa traduction), « Discard your changes and reload the macros file? » →
  « Abandonner vos modifications et recharger le fichier de macros ? », « Cannot reload the macros file. » → « Impossible de recharger le fichier de
  macros. », « Optional » → « Facultatif », « Prefix » → « Préfixe », « optional » → « facultatif », « optional, prefix \"%s\" » → « facultatif, préfixe
  « %s » », « (none) » → « (aucun) », « (omitted) » → « (omis) », « Prefix of argument %d removed » → « Préfixe de l'argument %d retiré »,
  « %s: [%s] %s: invalid value adjusted » → « %s : [%s] %s : valeur invalide ajustée ». Aucune entrée `fuzzy` laissée pour ces chaînes.
- [ ] **Étape 2 : CLAUDE.md** — mettre à jour : Projects (clé `name`, `GtProject` `name`, `gt_project_move_file`, `app.project-rename` et menu File, liste
  des actions de projet), Macros subsystem (`gt_macro_set_group_by_tab` à la lecture, `arg_optional`/`arg_prefix` dans `GtMacro`, `.ini`, expansion,
  segment OMITTED, `gt_macro_arg_widget_new_full`/`set_optional`), éditeur (filtres `macro-tab-filter`/`lists-filter`, regroupement, Move borné, ligne
  `macro-source`/`macro-reload` à la place de `macro-template-info`, colonnes Optional/Prefix, séparateur `macro-settings-separator`, 900 × 640),
  `test-project` (`stored_name`), nombre de scénarios GUI (69 + 4 = 73).
- [ ] **Étape 3 : NEWS** — sous une nouvelle section `## v2.2.0 (à venir)` si l'usage du fichier le permet (sinon en tête de la section en cours non
  publiée) : items « Nom de projet… », « Configure Macros : filtres… regroupement… Reload… », « Arguments facultatifs… », et l'avertissement
  `! Un fichier de macros avec des arguments facultatifs, ouvert par une version plus ancienne, envoie l'action sans les préfixes.` Ne pas créer de tag.
- [ ] **Étape 4 : vérification complète** — `ninja -C build && meson test -C build` (tout PASS, les `# TODO` connus inchangés :
  `grep '# TODO' build/meson-logs/testlog.txt`) puis `meson test -C build --suite gui` (tout OK ou SKIP 77 connus) ; `grep -rn 'gtk/gtk.h' src/*.c src/*.h`
  vide. Joindre les chiffres réels dans le résumé.
- [ ] **Étape 5 : revue mémoire** — agent `memory-safety-reviewer` sur le diff depuis le commit de la spec (`git diff d1aa689`), corriger ce qu'il confirme.
- [ ] **Étape 6 : commit** — `"docs, traductions : nom de projet, éditeur de macros, arguments facultatifs"`.
- [ ] **Étape 7 : poussée** — après accord de l'utilisateur : `git push Maison master`.
