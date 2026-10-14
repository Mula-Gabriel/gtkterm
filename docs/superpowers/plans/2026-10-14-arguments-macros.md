# Arguments de macros — plan de réalisation

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**But :** limites optionnelles des arguments de macros (min/max des nombres, longueur et jeu de caractères des `%s`), envoi refusé hors limites,
champ vide = valeur par défaut dans le panneau, table des arguments et aperçu de la ligne envoyée dans « Configure Macros ».

**Spec :** `docs/superpowers/specs/2026-10-14-arguments-macros-design.md` (à lire en entier avant chaque tâche ; les numéros §2.N y renvoient).

**Contexte :** C, GLib, GTK 4 plafonné à 4.14 (`-Werror=deprecated-declarations`), Meson/Ninja. Lire `CLAUDE.md` (couches, style, macros).
Style : celui du fichier modifié (`macros_format.c` : GNU 2 espaces ; `macros.c` : GNU ; `macro_set.c`, `macro_file.c`, `src/ui/*` : tabulations,
accolades Allman, pas d'espace avant `(` ; `script_engine.c` : 4 espaces). Commentaires et commits en français, commits terminés par
`Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Construire : `ninja -C build` ; tests : `meson test -C build`
(suite unitaire), `meson test -C build --suite gui` (une seule à la fois, jamais l'écran réel). Ne jamais toucher `~/.config/.gtktermrc` ni un vrai
port série. Chaînes visibles : `_()` avec msgid anglais (dans le cœur : `#include <glib/gi18n.h>`, comme `macro_file.c`).

---

### Tâche 1 : module cœur `macro_arg.c` (jeux de caractères, limites, vérification, description)

**Fichiers :** créer `src/macro_arg.h`, `src/macro_arg.c`, `tests/test_macro_arg.c` ; modifier `src/meson.build` (`core_sources`, après
`macros_format.h`), `tests/meson.build` (`'test_macro_arg' : [],` après `test_macros_format`), `po/POTFILES.in` (ajouter `src/macro_arg.c`).

API (types d'argument = chaînes de `macro_arg_info_t.type` : `"d"`, `"ld"`, `"hhu"`, `"x"`, `".."`, `"f"`, `"Lf"`, `"s"`, `"c"`, `"l"` pour une liste) :

```c
typedef enum { GT_MACRO_CHARSET_UTF8, GT_MACRO_CHARSET_PRINTABLE, GT_MACRO_CHARSET_ASCII, GT_MACRO_CHARSET_LATIN1, GT_MACRO_CHARSET_CLASS } GtMacroCharset;
typedef enum { GT_MACRO_ARG_OK, GT_MACRO_ARG_BAD_VALUE, GT_MACRO_ARG_BELOW_MIN, GT_MACRO_ARG_ABOVE_MAX,
               GT_MACRO_ARG_TOO_SHORT, GT_MACRO_ARG_TOO_LONG, GT_MACRO_ARG_BAD_CHAR } GtMacroArgVerdict;

/* NULL ou "" → UTF8 ; "printable", "ascii", "latin1" ; "class:<classe>" (classe valide, *class_out = copie de la classe) ; sinon FALSE */
gboolean          gt_macro_charset_parse(const gchar *text, GtMacroCharset *kind, gchar **class_out);
gboolean          gt_macro_class_valid(const gchar *cls);                 /* §2.3 : non vide, plages a ≤ b, « - » au bord, « \ » protège, ≤ 256 caractères */
gboolean          gt_macro_class_contains(const gchar *cls, gunichar c);  /* cls supposée valide */
gboolean          gt_macro_arg_type_has_limits(const gchar *type);        /* entiers, décimaux, s ; pas c ni l */
gboolean          gt_macro_arg_limit_valid(const gchar *type, const gchar *limit);   /* "" ou NULL : TRUE ; §2.6 */
gboolean          gt_macro_arg_limits_valid(const gchar *type, const gchar *min, const gchar *max, const gchar *charset); /* chacun + min ≤ max ; charset seulement pour s */
/* Octets envoyés pour la valeur d'un %s : latin1 → ISO-8859-1 (NULL si un caractère > U+00FF) ; sinon copie. *len_out = longueur. */
gchar            *gt_macro_arg_encode(const gchar *charset, const gchar *value, gsize *len_out);
/* §2.5 : validité du type (macro_type_value_valid), puis limites ; reason (nullable) : texte traduit, NULL si OK ; g_free */
GtMacroArgVerdict gt_macro_arg_check(const gchar *type, const gchar *value, const gchar *min, const gchar *max, const gchar *charset, gchar **reason);
const gchar      *gt_macro_arg_type_name(const gchar *type);              /* traduit : integer, unsigned, hexadecimal, octal, decimal, text, character, list */
/* Bulle du panneau : « integer, 0 – 100 (default 50) », « decimal, ≤ 5.0 », « text, 1 – 8 bytes, printable ASCII », « list Modes (default b) » */
gchar            *gt_macro_arg_describe(const gchar *type, const gchar *list_name, const gchar *min, const gchar *max, const gchar *charset, const gchar *def);
```

Règles précises :
- Nombres : entiers `d`/`i` comparés en `gint64` (lecture `g_ascii_strtoll`, base 16 avec `0x`, sinon 10, comme `int_arg_base` de `macros_format.c` :
  la dupliquer en statique), `u`/`o`/`x`/`X` en `guint64` (`g_ascii_strtoull`), décimaux en `double` (`g_ascii_strtod`). Une limite doit passer
  `macro_type_value_valid(type, limit)` et être non vide après lecture ; décimal : `nan`/`inf` refusés comme limite. Valeur vide = 0. Valeur
  `nan`/`inf` : `BAD_VALUE` si une limite existe.
- `%s` : limites = entiers décimaux 0..65535 (pas de signe, pas d'hexa) ; longueur = octets de `gt_macro_arg_encode` ; `latin1` non encodable →
  `BAD_CHAR` avec le premier caractère fautif ; `printable` 0x20–0x7E, `ascii` 0x00–0x7F, `class:` par `gt_macro_class_contains` ; valeur non UTF-8
  valide → `BAD_VALUE`. Ordre : type, jeu de caractères, longueur.
- Textes de raison (msgid) : `"not a valid %s"` (nom du type), `"below minimum %s"`, `"above maximum %s"`, `"%u bytes, at least %s"`,
  `"%u bytes, at most %s"`, `"character \"%s\" not allowed"`. Description : nom du type ; limites `", %s – %s"`, `", ≥ %s"`, `", ≤ %s"` (+ `" bytes"`
  pour s) ; jeu (s seulement, sauf UTF-8) : `", printable ASCII"`, `", ASCII"`, `", 8 bits (Latin-1)"`, `", characters %s"` ; liste : `"list %s"` ;
  défaut non vide : `" (default %s)"`.

- [ ] Écrire `tests/test_macro_arg.c` (GTest, chemins `/macro_arg/...`) couvrant : parse des jeux (valides, `class:` vide ou invalide, inconnu) ;
  classes (`A-Z0-9_-`, `-a`, `a-`, `\-`, `\\`, `z-a` invalide, `\` final invalide, plage Unicode `à-ÿ`, 257 caractères invalide) ; limites
  (`300` refusé pour `hhu`, `-1` refusé pour `u` ? — non : `macro_type_value_valid("u","-1")` est vrai aujourd'hui, garder ce comportement et le
  tester tel quel ; `0x10` valide pour `x` et `d` ; `1.5` refusé pour `d` ; `nan` refusé pour `f` ; `-1`/`65536`/`0x5` refusés pour `s` ; min > max
  refusé ; jeu sur `d` refusé ; limites sur `c`/`l` refusées sauf vides) ; vérification (dans/hors bornes entiers signés/non signés/hexa/décimaux,
  vide = 0, `nan` avec limite, `%s` longueur UTF-8 « é » = 2 octets mais 1 en latin1, `printable` refuse `\t`, classe, raisons exactes en anglais
  — les tests tournent sans traduction) ; encodage latin1 (`"é"` → 0xE9, `"€"` → NULL) ; descriptions exactes des exemples ci-dessus.
- [ ] `meson test -C build test_macro_arg` échoue (module absent), puis écrire le module, puis tout passe ; `meson test -C build` complet passe.
- [ ] Commit : `macros : module macro_arg (jeux de caractères, limites, vérification des arguments)`.

### Tâche 2 : limites dans `GtMacro`, le fichier et le projet

**Fichiers :** `src/macro_set.h/.c`, `src/macro_file.c`, `tests/test_macro_set.c`, `tests/test_macro_file.c`, `tests/test_project_file.c`.

- `GtMacro` gagne `gchar **arg_min, **arg_max, **arg_charset;` (NULL, ou exactement n_args éléments ; jamais un vecteur entièrement vide : il
  devient NULL). `GtMacroEdit` gagne `const gchar *const *arg_min, *arg_max, *arg_charset;` (NULL : aucune).
- `gint gt_macro_set_set_limits(GtMacroSet *set, guint id, const gchar *const *min, const gchar *const *max, const gchar *const *charset, gint n_args);`
  chaque vecteur NULL ou de longueur n_args (sinon -1, rien ne change ; macro inconnue : -1) ; chaque entrée validée pour le type de l'argument de
  même rang (`gt_macro_arg_limit_valid`, puis `gt_macro_arg_limits_valid` pour le couple et le jeu ; une paire min > max : les deux remis à `""`) ;
  rend le nombre d'entrées remises à `""`.
- `check_args` : nombre d'arguments changé → `args` et les trois vecteurs abandonnés ; sinon chaque limite revalidée pour le type courant (§2.6,
  sans avertissement). `macro_free`, `gt_macro_set_copy` (copie les limites), `gt_macro_set_apply_edits` (prend les limites de la ligne comme les
  `args`, puis `check_args`) suivent.
- `macro_file.c` : lecture des listes `arg_min`, `arg_max`, `arg_charset` par `get_list_tolerant` après `args`, puis `gt_macro_set_set_limits` ;
  si le retour est > 0 ou -1 avec au moins un vecteur présent : `g_warning` traduit `"%s: [%s] invalid argument limits replaced by none"` (origin,
  groupe). Écriture de chaque vecteur non NULL par `g_key_file_set_string_list` juste après `args`.
- [ ] Tests d'abord : `test_macro_set` (`/macro_set/limites` : pose, refus de longueur, entrée invalide comptée, min > max, copie, apply_edits,
  `%s %d` → `%d %s` remet les limites invalides à `""`, nombre d'arguments changé → tout abandonné, vecteur tout vide → NULL) ;
  `test_macro_file` (aller-retour d'un fichier avec `arg_min=0;`, `arg_max=100;`, `arg_charset=class:A-Z\\;` — attention au `;` séparateur de
  GKeyFile : tester une classe contenant `;` — ; fichier sans ces clés : vecteurs NULL ; limite invalide → `""` et avertissement attendu avec
  `g_test_expect_message`) ; `test_project_file` (aller-retour d'un projet dont une macro a des limites).
- [ ] Implémenter, `meson test -C build` passe (ASan inclus si `build-asan` existe : `meson test -C build-asan`).
- [ ] Commit : `macros : limites des arguments dans le jeu, le fichier .ini et le projet`.

### Tâche 3 : expansion tracée, segments, encodage latin1 et vérification des arguments

**Fichiers :** `src/macros_format.c/.h`, `src/macro_set.h/.c`, `src/macro_preview.h/.c` (nouveau, cœur), `src/meson.build`, `tests/meson.build`,
`tests/test_macro_set.c`, `tests/test_macros_format.c`, `tests/test_macro_preview.c` (nouveau), `po/POTFILES.in` si chaînes.

- `macros_format.c` : fonctions **internes partagées** déclarées dans `macros_format.h` sous un commentaire « usage du cœur » :
  `gchar *format_action_traced(const GtMacroLists *lists, const gchar *action, const gchar **args, gint n_args, const gchar *const *charsets, GArray *arg_ranges);`
  (`arg_ranges` nullable : `GtMacroRange { guint start, len; }` par argument mis en forme, dans le texte rendu, **après** le doublement des
  antislashs ; `charsets` nullable, parallèle aux arguments : un `%s` en `latin1` voit sa valeur convertie par `gt_macro_arg_encode` avant
  `apply_spec`, valeur gardée telle quelle si la conversion échoue) et
  `GByteArray *parse_macro_string_traced(const gchar *string, GArray *origins);` (`origins` nullable : pour chaque octet produit, `GtMacroRange` de
  ses caractères d'origine). `format_action_with_args` et `parse_macro_string` deviennent des appels avec NULL : **aucun octet ne change**.
- `macro_set.h` : `typedef enum { GT_MACRO_SEG_TEXT, GT_MACRO_SEG_ESCAPE, GT_MACRO_SEG_ARG } GtMacroSegKind;`
  `typedef struct { GtMacroSegKind kind; gint arg; guint start, len; } GtMacroSegment;` (arg : rang 0.., -1 sinon ; start/len en octets) ;
  `GBytes *gt_macro_expand_segments(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args, GArray **segments_out);`
  (mêmes règles d'arguments que `gt_macro_expand`, charsets = `macro->arg_charset` ; classement §2.9 : origine de plus d'un caractère → ESCAPE,
  sinon octet dont l'origine tombe dans la plage d'un argument → ARG, sinon TEXT ; fusion des voisins de même sorte et même argument).
  `gt_macro_expand` = `gt_macro_expand_segments(..., NULL)`.
  `gboolean gt_macro_check_args(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args, gint *index_out, gchar **reason_out);`
  vérifie les `min(n_args, nombre d'arguments)` premiers arguments non-liste par `gt_macro_arg_check` avec les limites de la macro ; args NULL → TRUE.
- `macro_preview.c` (cœur) : `typedef enum { GT_PREVIEW_TEXT, GT_PREVIEW_ARG, GT_PREVIEW_SPECIAL } GtPreviewKind;`
  `typedef struct { GtPreviewKind kind; gint arg; gchar *text; } GtPreviewRun;`
  `GPtrArray *gt_macro_preview_runs(const guint8 *data, gsize len, const GArray *segments);` (éléments `GtPreviewRun *`, libérés par le tableau) :
  octets 0x20–0x7E et séquences UTF-8 valides tels quels ; contrôles nommés `NUL BEL BS TAB LF VT FF CR ESC DEL`, autres octets `\xHH` (hexa
  majuscule) ; un segment ESCAPE → runs SPECIAL ; dans TEXT, un nom de contrôle ou `\xHH` → run SPECIAL ; dans ARG, tout reste ARG (texte avec les
  noms). Runs voisins de même sorte fusionnés.
  `gchar *gt_macro_preview_hex(const guint8 *data, gsize len, gsize max);` → `"50 57 4D"` ; au-delà de max octets, `" …"` final.
- [ ] Tests d'abord : `test_macro_set` `/macro_set/segments` (égalité des octets `gt_macro_expand_segments` = `parse_macro_string(format_action_with_args())`
  pour une table reprenant les actions de `test_macros_format` + `\x%02X` (65 → `A`, un segment ESCAPE), `\x4%d` (1), `\%c` (`r` → CR),
  `[a\r] %d`, `%%`, liste, `%5s`) ; segments attendus pour `PWM %d %.1f %#Mode\r\n` ; latin1 `%s` « é » → 0xE9 ; `/macro_set/verification`
  (gt_macro_check_args : index et raison). `test_macro_preview` : runs et hexa (`\r\n` → `CR` `LF` SPECIAL ; « é » UTF-8 tel quel ; 0xE9 seul →
  `\xE9` ; troncature).
- [ ] Implémenter ; toute la suite passe (`test_macros_format` inchangé).
- [ ] Commit : `macros : expansion découpée en segments, encodage latin1, vérification et aperçu des octets`.

### Tâche 4 : application — champ vide = défaut, refus d'envoi, Lua

**Fichiers :** `src/macros.h/.c`, `src/script_engine.c`, `tests/stubs/stub_macros.c`, `tests/test_macros_send.c`, `tests/test_script_engine.c`,
`po/fr.po` plus tard (tâche 7).

- `gt_macros_get_args` : §2.7 (vecteur résolu en cache `GHashTable *resolved` id → strv ; entrée retirée par `set_usage_arg` de cet id,
  `reset_usage`, `replace_set`, `apply_edits`, et remplacée à chaque appel). `const gchar *const *gt_macros_get_usage(GtMacros *m, guint id);`
  (brut, NULL si absent ou de mauvaise longueur). `gint gt_macros_check_args(GtMacros *m, guint id, const gchar *const *args, gint n_args, gchar **reason);`
  (-1 : valides ou macro inconnue ; sinon rang 0.. du premier invalide ; n_args < 0 → arguments résolus).
- `send_macro` : avant l'expansion, vérification ; refus → rien n'est écrit, retour FALSE ; si `announce` et dernier message de refus pour cet id
  plus vieux que 3 s (`GHashTable *refused_at` id → gint64 monotone, purgée comme `resolved`) : `gt_app_status_flash(m->owner, 3000,
  _("Macro \"%s\" not sent: argument %d: %s"), nom, index + 1, raison)`.
- `script_engine.c` : `MainMacroData` gagne `gboolean invalid` ; `main_send_macro_cb` appelle `gt_macros_check_args` (args du script, ou -1
  sans argument) et ne l'envoie pas si invalide ; `l_send_macro` rend `false, "invalid argument"`. Arguments : nombre Lua dont
  `lua_tointegerx` réussit → `"%lld"` ; sinon `lua_tostring`.
- `stub_macros.c` : `gt_macros_check_args` sur `gt_macro_check_args` du jeu du bouchon (n_args < 0 : `macro->args`).
- [ ] Tests d'abord : `test_macros_send` (champ d'usage vide → défaut ; aucun défaut ni usage → `V%d` envoyé tel quel ; usage de mauvaise
  longueur ignoré ; refus hors limites : rien d'écrit, FALSE, un message `status_flash` capturé par la table `GtAppUi` du test, pas de second
  message dans les 3 s ; `send_quiet` refusé sans message ; `gt_macros_get_usage`) ; adapter `test_usage_nombre_change` si son attendu change
  (il doit rester NULL : vérifier). `test_script_engine` : `send_macro` avec argument hors limites → `false, "invalid argument"` ; `5.0` accepté
  pour `%d`.
- [ ] Implémenter ; suite complète verte.
- [ ] Commit : `macros : champ vide = valeur par défaut, envoi refusé hors limites (panneau, raccourci, polling, Lua)`.

### Tâche 5 : interface — lignes, champ d'argument, panneau, vidage de test

**Fichiers :** `src/ui/macro_row.h/.c`, `tests/test_macro_row.c`, `src/ui/macro_arg_widget.h/.c`, `src/ui/macro_panel.c`, `src/ui/test_hooks.c`.

- `GtMacroRow` : propriétés `arg-min`, `arg-max`, `arg-charset` (G_TYPE_STRV, même contrat que `args`), accesseurs `gt_macro_row_get_arg_min/max/charset`,
  `gt_macro_row_set_limits(row, min, max, charset)` (notify seulement si changé) ; `gt_macro_rows_from_set` les remplit ; `gt_macro_rows_to_edits`
  les transmet. Tests dans `test_macro_row`.
- `macro_arg_widget` : `gt_macro_arg_widget_new(lists, info, initial, placeholder)` (placeholder NULL : indication de type actuelle) ;
  `gt_macro_arg_widget_set_limits(w, min, max, charset, def)` garde ces valeurs (données de l'objet) ; `gt_macro_arg_widget_validate(w)` : valeur
  vérifiée = texte, ou `def` si le texte est vide et que l'objet est en mode « vide = défaut » (`gt_macro_arg_widget_set_empty_is_default(w, TRUE)`) ;
  classe `error` et bulle = `gt_macro_arg_describe(...)` + `"\n"` + raison si invalide. `gt_macro_arg_widget_check(w, &reason)` rend la validité.
  Pour une liste, la bulle décrit aussi l'argument.
- Panneau (§2.7, §2.12) : champ initial = `gt_macros_get_usage()[k]` (vide si absent), indication = défaut s'il n'est pas vide ; mode
  « vide = défaut » ; largeur `CLAMP(max(len(texte), len(indication)), 4, 24)` ; `commit_arg` compare à la valeur d'usage brute ; limites posées
  depuis la macro. « Reset arguments » : champs vidés (reconstruction actuelle suffit si elle relit les valeurs d'usage).
- `test_hooks.c` : le vidage ajoute `placeholder` (GtkEntry) et `tooltip` (`gtk_widget_get_tooltip_text`) quand ils ne sont pas vides.
- [ ] Tests unitaires `test_macro_row` d'abord ; construire sans avertissement ; suite unitaire verte. Adapter `tests/gui/test_macros_arguments.py`
  (§2.7 : après Reset arguments et après OK de l'éditeur, champ `label == ''` et `placeholder` = défaut) et `tests/gui/test_macro_argument_long.py`
  (argument long = défaut : `placeholder == long_arg`, envoi entier inchangé) ; lancer ces deux scénarios
  (`meson test -C build --suite gui test_macros_arguments test_macro_argument_long`).
- [ ] Commit : `panneau de macros : champ vide = défaut en grisé, limites vérifiées, bulle descriptive`.

### Tâche 6 : éditeur — table des arguments et aperçu

**Fichiers :** `src/ui/macro_editor.c`, CSS éventuelle dans le même fichier ou `src/ui/app.c` (suivre l'existant).

- Remplacer la rangée `macro-defaults` par une `GtkGrid` de même nom (§2.11) : en-têtes `#`, `Format`, `Type`, `Default`, `Min`, `Max`, `Charset` ;
  par argument : rang (1..), spécificateur tel qu'écrit dans l'action (à extraire : ajouter au cœur, dans `macros_format.c`, le champ
  `gchar *spec` de `macro_arg_info_t` rempli par `macro_get_arg_infos` — `%.1f`, `%#Mode` — et le libérer dans `macro_arg_infos_free`), nom de
  type (`gt_macro_arg_type_name`, « text (bytes) » pour s), défaut (`macro-default-<k>`, champ actuel), min/max (`macro-min-<k>`, `macro-max-<k>`,
  entrées 8 caractères, absentes pour c/l), jeu (`macro-charset-<k>`, `GtkDropDown` à 5 entrées, s seulement) et classe (`macro-class-<k>`,
  visible seulement pour « Custom class »). Noms accessibles = noms de widget.
- Commit des champs (`defaults_commit` étendu) : par vecteur (défauts / min / max / jeux) ; un vecteur n'est retenu que si tous ses champs sont
  valides ; défaut vérifié par `gt_macro_arg_check` avec les limites retenues de la ligne ; champ invalide en classe `error` ; tout vide → NULL.
  Écriture par `gt_macro_row_set_args` et `gt_macro_row_set_limits`.
- Aperçu (§2.10) : boîte `macro-preview-box` sous la table, visible pour une seule ligne sélectionnée dont l'action est envoyable
  (`gt_macro_is_sendable` sur une macro temporaire, ou `gt_macro_action_is_separator`/action vide) ; étiquette « Line sent (default values): »,
  `GtkLabel` `macro-preview` (monospace, sélectionnable, retour à la ligne par caractère, balisage Pango construit avec
  `g_markup_escape_text` : ARG `#1c71d8` en gras léger, ARG invalide `#e01b24`, SPECIAL `#9141ac`), `GtkLabel` `macro-preview-bytes`
  (`gt_macro_preview_hex(…, 256)` + `" · %u bytes"` traduit, `ngettext`). Calcul : jeu temporaire d'une macro (action, args, limites de la ligne,
  listes de la page Lists) → `gt_macro_expand_segments` → `gt_macro_preview_runs` ; validité de chaque argument par `gt_macro_check_args`.
  Rafraîchi sur `notify::action`, `notify::args`, `notify::arg-min/max/charset` de la ligne affichée, sélection, retour sur la page Macros.
- [ ] Construire ; suite unitaire verte ; `test_macros_arguments`, `test_macros_edition`, `test_macro_editor_colonnes` verts sous la suite GUI.
- [ ] Commit : `Configure Macros : table des arguments (défaut, limites, jeu de caractères) et aperçu de la ligne envoyée`.

### Tâche 7 : scénario GUI, documentation, traductions

**Fichiers :** `tests/gui/test_macros_limites.py` (nouveau), `tests/gui/meson.build` (liste `gui_scenarios`), `README.md`, `CLAUDE.md`,
`po/*` (`ninja -C build gtkterm-pot gtkterm-update-po`, puis `po/fr.po` traduit à 100 %, aucun fuzzy : `msgfmt -c --statistics po/fr.po`).

- Scénario (`write_macros_v2` étendu dans `gtktest.py` : clés optionnelles `arg_min`, `arg_max`, `arg_charset`) : macro `P` = `P%d;` défaut 50,
  limites 0–100 ; macro `N` = `N%s;` longueur 1–4 `printable` ; macro `L` = `L%s;` `latin1` défaut `é`. Vérifier : bulle de `P` contient
  « integer, 0 – 100 (default 50) » ; champ vide et indication `50` ; clic → `P50;` ; `140` → classe error, clic → rien sur la pty et message
  « not sent » dans la barre d'état ; `N` avec `abcde` refusé, `ab` envoyé ; `L` envoie `b'L\xe9;'`. Éditeur : sélection de `P`, table visible,
  `macro-min-0` = `0`, `macro-max-0` = `100`, `macro-preview-bytes` commence par `50 35 30 3B` et finit par `4 bytes` ; `macro-max-0` → `10` :
  défaut 50 rouge dans l'aperçu ; mettre `macro-default-0` à `7`, OK → `.ini` contient `arg_max=10;` et `args=7;`. Choix `Custom class` sur
  `N` : `macro-class-0` visible.
- Docs : README (section macros : limites, jeux, aperçu, champ vide = défaut) ; CLAUDE.md (module `macro_arg.c`, `macro_preview.c`, clés
  `arg_min`/`arg_max`/`arg_charset`, règle du champ vide, refus d'envoi, nombre de scénarios GUI + 1, clés `placeholder`/`tooltip` du vidage).
- [ ] Suite unitaire et suite GUI complètes (une seule GUI à la fois) ; noter les SKIP broadway connus (`test_port_dialog`, `test_polling`,
  `test_macros_arguments` peuvent sauter par assertion interne de gtk4-broadwayd : 77, pas un échec).
- [ ] Commits : `tests GUI : limites des arguments de macros` puis `docs, traductions : arguments de macros`.
