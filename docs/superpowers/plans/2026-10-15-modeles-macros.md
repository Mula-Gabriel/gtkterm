# Modèles de macros liés — plan de réalisation

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**But :** un onglet de projet peut lier un fichier de macros comme modèle ; ses valeurs par défaut et son polling propres au projet surchargent le
modèle ; l'OK de « Configure Macros » demande « garder dans le projet » ou « enregistrer dans le modèle » ; réimport du modèle à l'ouverture ; Détacher.

**Spec :** `docs/superpowers/specs/2026-10-15-modeles-macros-design.md` (à lire **en entier** avant chaque tâche ; les §2.N y renvoient). Elle fait foi
en cas de doute : ce plan ne fait que découper.

**Contexte :** C, GLib, GTK 4 plafonné à 4.14 (`-Werror=deprecated-declarations`), Meson/Ninja. Lire `CLAUDE.md` (couches, style, macros, projets).
Style : celui du fichier modifié (`macros.c` : GNU 2 espaces ; `macro_set.c`, `macro_file.c`, `project_file.c`, `project.c`, `session.c`, `src/ui/*` :
tabulations, accolades Allman, pas d'espace avant `(` ; nouveau module cœur : style dominant, tabulations). Commentaires et commits en français, commits
terminés par `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Construire : `ninja -C build` ; tests : `meson test -C build`
(suite unitaire) ; GUI : `meson test -C build --suite gui` (une seule suite GUI à la fois, jamais l'écran réel ; un SKIP 77 dû à une assertion interne de
gtk4-broadwayd est connu pour `test_port_dialog`, `test_polling`, `test_macros_arguments`). Ne jamais toucher `~/.config/.gtktermrc` ni un vrai port
série. Chaînes visibles : `_()` avec msgid anglais (cœur : `#include <glib/gi18n.h>`). Fil principal seulement.

---

### Tâche 1 : cœur — structure, clone, module `macro_override.c`

**Fichiers :** `src/macro_set.h/.c`, créer `src/macro_override.h/.c`, `tests/test_macro_override.c` ; `tests/test_macro_set.c` ; `src/meson.build`
(`core_sources`, après `macro_preview`), `tests/meson.build` (`'test_macro_override' : [],` après `test_macro_set`), `po/POTFILES.in`.

API :
```c
/* macro_set.h */
gboolean    gt_macro_set_same_structure(const GtMacroSet *a, const GtMacroSet *b);	/* §2.2 ; NULL ≡ vecteur vide pour les limites */
GtMacroSet *gt_macro_set_clone(const GtMacroSet *set);	/* copie profonde gardant les identifiants : simulations seulement, jamais installé */

/* macro_override.h */
typedef struct
{
	gchar     *label;		/* jamais NULL */
	guint      occurrence;	/* rang parmi les macros de même libellé (0 : première) */
	GPtrArray *args;		/* gchar * ou NULL (rang non surchargé), possédés ; longueur = dernier rang surchargé + 1 */
	gboolean   has_polling;
	gboolean   polling;
	guint      polling_period_ms;	/* telle que lue : peut être hors bornes (abandonnée par apply) */
} GtMacroOverride;
typedef struct _GtMacroOverrides GtMacroOverrides;

GtMacroOverrides      *gt_macro_overrides_new(void);
void                   gt_macro_overrides_free(GtMacroOverrides *o);	/* NULL accepté */
guint                  gt_macro_overrides_count(const GtMacroOverrides *o);
const GtMacroOverride *gt_macro_overrides_get(const GtMacroOverrides *o, guint i);
GtMacroOverrides      *gt_macro_overrides_copy(const GtMacroOverrides *o);
/* Ajoute ou remplace (même libellé et occurrence : l'entrée existante est remplacée en place, valeurs fusionnées rang par rang) */
void                   gt_macro_overrides_set_arg(GtMacroOverrides *o, const gchar *label, guint occurrence, guint rank, const gchar *value);
void                   gt_macro_overrides_set_polling(GtMacroOverrides *o, const gchar *label, guint occurrence, gboolean polling, guint period_ms);
GtMacroOverrides      *gt_macro_overrides_diff(const GtMacroSet *base, const GtMacroSet *effective);	/* §2.3 ; NULL si structures différentes */
GtMacroSet            *gt_macro_overrides_apply(const GtMacroOverrides *o, const GtMacroSet *base, guint *dropped);	/* §2.3 ; o NULL : copie */
GtMacroSet            *gt_macro_overrides_merge_template(const GtMacroSet *base, const GtMacroSet *before, const GtMacroSet *after);	/* §2.8-2 */
/* Groupes « <prefix>override:K » (§2.4) ; écriture : groupes existants de ce préfixe retirés d'abord. Lecture tolérante, normalisée (§2.3) ;
 * origin : chemin cité dans les avertissements ; jamais NULL. */
void                   gt_macro_overrides_to_key_file(const GtMacroOverrides *o, GKeyFile *kf, const gchar *prefix);
GtMacroOverrides      *gt_macro_overrides_from_key_file(GKeyFile *kf, const gchar *prefix, const gchar *origin);
```
Règles : égalité des valeurs par défaut NULL ≡ `""` ; type d'un rang = `macro_parse_format_args` / `macro_arg_info_t` (voir `macros_format.h` et
l'usage dans `macro_set.c`) ; vérification d'une valeur = `gt_macro_arg_check(type, value, min, max, charset, NULL) == GT_MACRO_ARG_OK` avec les limites de
même rang (listes : non vérifiées). Avertissement de lecture : `g_warning(_("%s: [%s] %s: invalid value ignored"), origin, group, key)`.
Pour `apply`, appliquer les valeurs avec `gt_macro_set_set_arg` (qui crée le vecteur au besoin) et le polling avec `gt_macro_set_set_polling`.

- [ ] Tests d'abord (`/macro_set/same_structure`, `/macro_set/clone_keeps_ids`, `/macro_override/...`) : same_structure (libellé, action, limites
  NULL vs vide, listes différentes, ordre) ; diff (args NULL vs `""`, chaîne vide surchargée, polling seul, macro sans argument pollée, libellés en
  double → occurrences 0 et 1, libellé vide, structures différentes → NULL) ; apply (aller-retour diff/apply donne des valeurs égales, abandons : macro
  disparue (compte tous ses éléments), rang dépassé, limite durcie, période hors bornes, polling sur séparateur ; liste non vérifiée) ; merge_template
  (inchangé → base, changé → after, base refusée par nouvelles limites → after, réordonnancement, type changé, macro nouvelle et supprimée, polling de la
  base) ; key file (aller-retour, `occurrence` absente = 0, clés invalides ignorées, `polling` sans période ignoré, doublon : le dernier gagne, groupes d'un
  autre préfixe ignorés, préfixe `tab:0:` ne lit pas `tab:1:`).
- [ ] Échec constaté, implémentation, `meson test -C build` passe en entier.
- [ ] Commit : `macros : valeurs propres d'un modèle (module macro_override), structure et clone d'un jeu`.

### Tâche 2 : format projet

**Fichiers :** `src/project_file.h/.c`, `tests/test_project_file.c`.

- `GtProjectTab` gagne `gchar *macros_template;` (NULL : non lié) et `GtMacroOverrides *overrides;` (jamais NULL ; `gt_project_tab_new` crée vide, `free`
  libère).
- Écriture (`write_key_file`, avec et sans état de page) : onglet lié → clé `macros_template` dans `[tab:N]`, base dans `[tab:N:macro:M]`/`list`
  (instantané : **sans** ces groupes de la base, §2.4), puis `gt_macro_overrides_to_key_file(t->overrides, kf, "tab:N:")` (instantané compris). Non lié :
  inchangé.
- Lecture (`read_tab`) : `macros_template` (vide ou non absolu → NULL + `g_warning`), `overrides` = `gt_macro_overrides_from_key_file(kf, prefix, path)`
  — lu seulement si l'onglet est lié (sinon groupes ignorés, vide).
- [ ] Tests : aller-retour d'un onglet lié (chemin, base, valeurs propres), onglet non lié : texte identique à avant (comparer à une référence fixe
  existante du test si elle existe), instantané d'un onglet lié sans `[tab:0:macro:*]` mais avec `override`, deux bases différentes même instantané,
  `macros_template` relatif ignoré.
- [ ] Commit : `projets : modèle de macros lié et valeurs propres dans le fichier .gtkterm`.

### Tâche 3 : `GtMacros` lié

**Fichiers :** `src/macros.h/.c`, `tests/stubs/stub_macros.c` (ajouter les nouvelles fonctions utilisées par d'autres modules compilés contre le bouchon,
au minimum celles appelées depuis `session.c`/`project.c` si ces modules utilisent le bouchon : vérifier `tests/meson.build`), créer
`tests/test_macros_template.c` (entrée dans `tests/meson.build` à côté de `test_macros_send`, mêmes sources et dépendances).

API (§2.1, 2.5–2.8) :
```c
typedef enum { GT_MACROS_EDIT_NONE, GT_MACROS_EDIT_DEFAULTS, GT_MACROS_EDIT_STRUCTURE } GtMacrosEditKind;
typedef enum { GT_MACROS_TEMPLATE_UNCHANGED, GT_MACROS_TEMPLATE_UPDATED, GT_MACROS_TEMPLATE_MISSING } GtMacrosTemplateStatus;

guint64             gt_macros_get_generation (GtMacros *m);
const gchar        *gt_macros_get_template   (GtMacros *m);	/* NULL : non lié */
const GtMacroSet   *gt_macros_get_template_set(GtMacros *m);	/* base ; NULL : non lié */
GtMacroOverrides   *gt_macros_get_overrides  (GtMacros *m);	/* diff(base, effectif), nouvelle ; NULL : non lié */
gboolean            gt_macros_link_template  (GtMacros *m, const gchar *path);	/* §2.6 ; interactif (message si échec) */
GtMacrosTemplateStatus gt_macros_link_from_project(GtMacros *m, const gchar *path, const GtMacroSet *cached_base, const GtMacroOverrides *overrides,
                                               guint *dropped, gchar **error_text);	/* §2.5 */
void                gt_macros_detach_template(GtMacros *m);	/* §2.6 ; non lié : rien */
void                gt_macros_copy_from      (GtMacros *dst, GtMacros *src);	/* jeu (copie) + lien (base copiée, chemin, identité) */
GtMacrosEditKind    gt_macros_classify_edits (GtMacros *m, const GtMacroEdit *rows, guint n_rows, const GtMacroLists *lists);
void                gt_macros_commit_edits   (GtMacros *m, const GtMacroEdit *rows, guint n_rows, const GtMacroLists *lists);
GtMacrosSaveResult  gt_macros_save_template_edits(GtMacros *m, const GtMacroEdit *rows, guint n_rows, const GtMacroLists *lists, gboolean force);
gboolean            gt_macros_refresh_template(GtMacros *m, gboolean interactive);	/* §2.8 « onglets frères » ; FALSE : lecture échouée */
gboolean            gt_macros_reload_template(GtMacros *m);	/* = refresh interactif */
```
Points : champs `template_path`, `base`, `template_stamp`, `generation` distincts de `path`/`stamp` ; `set_embedded(TRUE)` ne touche pas au lien,
`set_embedded(FALSE)` détache ; `gt_macros_load_file` en mode intégré détache seulement en cas de succès ; `generation` incrémenté par `replace_set`,
`apply_edits`, `gt_macros_edited`, lier, détacher, rafraîchir ; lier/détacher appellent `gt_macros_edited` ; messages et flashs de la spec (§2.6, §2.8,
§2.12 ; flash par `gt_app_status_flash(m->owner, …)`). `gt_macros_apply_edits` garde sa signature (consomme `lists`) ; `commit_edits` copie les listes.
Rafraîchissement en place : recopier `args` (`gt_macro_set_set_args`, ou effacer le vecteur si NULL — ajouter au besoin une petite fonction de
`macro_set.c` pour remettre `args` à NULL) et polling (`gt_macro_set_set_polling`) rang par rang.

- [ ] Tests (fichiers dans un dossier temporaire `g_dir_make_tmp`, table `GtAppUi` de test qui compte flashs et messages comme `test_macros_send.c`) :
  tous les cas de la spec §3 « Intégration ».
- [ ] Commit : `macros : onglet lié à un modèle (lier, détacher, classer, garder, enregistrer dans le modèle, rafraîchir)`.

### Tâche 4 : projet et session

**Fichiers :** `src/project.c`, `src/session.c`, `tests/test_project.c`, `tests/test_session.c` si pertinent.

- `gt_project_capture` : onglet lié → `t->macros` = copie de la base, `t->macros_template` = chemin, `t->overrides` = `gt_macros_get_overrides` ; sinon
  inchangé.
- `session.c` : action d'onglet `detach-macros-template` (activation → `gt_macros_detach_template` + flash de §2.6 fait par macros.c), activée seulement
  si lié ; mise à jour dans le gestionnaire du rappel `edited` de la session (et à l'installation).
- [ ] Tests : capture d'un onglet lié ; « modifié » non déclenché par une base relue identique en valeurs propres ; action activée/désactivée.
- [ ] Commit : `projets : capture des onglets liés, action Détacher du modèle`.

### Tâche 5 : interface — ouverture, nouvel onglet, menus, Load macros file…

**Fichiers :** `src/ui/app.c`, `src/ui/menus.ui`, `src/ui/profile_dialogs.c`, `src/ui/test_hooks.c`.

- `replace_all_tabs` : `d` devient non constant ; onglet lié (in_project) → `gt_macros_link_from_project` au lieu de la copie ; après la page : flash
  ou message (§2.5) ; `dropped == 0` → remplacer `t->overrides` par `gt_macros_get_overrides` avant `gt_project_begin` (§2.5 « Modifié »). Adapter les
  appelants.
- `gt_app_add_session` en projet : `gt_macros_copy_from(new, from)`.
- `tab_action_names` gagne `detach-macros-template` ; `menus.ui` : Macros › « Detach from template » après « Save macros file… ».
- `profile_dialogs.c` : en projet, question §2.6 (`gt_ui_choose`, `GWeakRef` sur la session, génération relevée ; réponse revérifiée).
- `test_hooks.c` : clé `template` dans `test-sessions`.
- [ ] Construction, suite unitaire, scénarios GUI existants des projets et macros (`meson test -C build --suite gui test_projet test_projet_macros
  test_onglets_macros test_menus_actions`) passent.
- [ ] Commit : `interface : modèles de macros (ouverture du projet, nouvel onglet, Load macros file…, Détacher)`.

### Tâche 6 : interface — « Configure Macros »

**Fichiers :** `src/ui/macro_editor.c`, `src/ui/macros_save.c/.h` si utile, `src/ui/app.c` (fonction de rafraîchissement des onglets frères, p. ex.
`gt_app_refresh_template_siblings(GtSession *s)` déclarée dans l'en-tête de `app.c`).

- `apply_edits` : non lié → `gt_macros_commit_edits` puis `gt_ui_macros_save` comme aujourd'hui ; lié → classification et questions §2.7, indicateur
  « question en cours », `GWeakRef` sur la fenêtre, génération ; « Save to template » §2.8 (éditeur ouvert jusqu'à `SAVED`, conflit, Reload) ; après
  `SAVED`, onglets frères rafraîchis.
- `on_ok` / `on_close_request` : ne ferment que si l'application est faite (ou NONE) ; pendant une question, rien.
- Libellé `macro-template-info` « Template: %s » en haut (§2.10), caché si non lié.
- [ ] Construction ; scénarios GUI des macros existants (`test_macros_edition`, `test_macro_editor_*`, `test_macros_conflit`, `test_projet_macros`) passent.
- [ ] Commit : `Configure Macros : question garder dans le projet / enregistrer dans le modèle`.

### Tâche 7 : scénario GUI

**Fichiers :** créer `tests/gui/test_macros_modele.py` (modèle : `test_projet_macros.py`, `test_macros_limites.py`), `tests/gui/meson.build`,
`tests/gui/gtktest.py` si un utilitaire manque.

- [ ] Scénario de la spec §3 « GUI » ; le passer 3 fois de suite ; toute la suite GUI ensuite (aucun nouvel échec).
- [ ] Commit : `tests GUI : modèles de macros liés`.

### Tâche 8 : documentation et traductions

**Fichiers :** `CLAUDE.md` (macros, projets, module cœur, crochet de test, nombre de scénarios), `README` (section macros/projets), `tests/README.md`
(nombre de scénarios), `po/*` (`ninja -C build gtkterm-pot gtkterm-update-po`, puis traduire toutes les nouvelles chaînes dans `po/fr.po`, aucun fuzzy).

- [ ] `meson test -C build` passe ; `msgfmt -c --statistics po/fr.po` sans fuzzy ni non traduit.
- [ ] Commit : `docs, traductions : modèles de macros liés`.
