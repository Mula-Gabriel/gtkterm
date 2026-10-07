# Sous-projet 4 — Macros : modèle sans GTK, identité stable, fichier échappé, polling par liaison

- Date : 2026-10-07
- Branche : `feature/refonte-macros` (fusion dans `master` selon la règle habituelle : écrasement en commits thématiques, fusion, suppression de la branche, publication sur `Maison`)
- Entrées : revue `docs/superpowers/reviews/2026-10-06-revue-code.md` (point 3 de l'architecture « polling des macros en trois exemplaires », M16, M21, M22, note EM-1 du sous-projet 1, remarques reportées : `update_button_label`, `lists_model`, `g_path_get_dirname`, clé `.ini` sans valeur, `tooltip[256]`, code mort et duplications des macros), sous-projets 0 à 3 fusionnés (socle de tests, correctifs C8/C9/C10/M15/M17–M20/M23/M24/M42/M43, `GtkTermLink`, `GtConfigStore`)

## Contexte : la suite du chantier

L'utilisateur prévoit deux évolutions, que ce sous-projet prépare sans les réaliser :
- une notion de **projet** qui applique une configuration, des scripts, et charge le bon fichier de macros tout seul ;
- **plusieurs onglets**, un par port ouvert (avec GTK 4, sous-projet 6).

Conséquences retenues : un jeu de macros devient une **valeur** (`GtMacroSet`, avec ses listes nommées) — un par projet demain, un seul aujourd'hui — et le polling devient un **ordonnanceur rattaché à une liaison** — un par onglet demain, un seul aujourd'hui (la liaison par défaut).

## Objectif

1. Un modèle de macros sans GTK dans `gtkterm_core` : macros à **identifiant stable**, arguments enregistrés, réglages de polling, listes nommées sans variable globale.
2. Une seule expansion (format puis échappements) et un seul envoi d'interface, communs au panneau, au raccourci, au polling et au Lua.
3. Un fichier `.ini` dont les valeurs sont échappées, lu et écrit par un module du cœur avec `GError`, et la migration automatique de l'ancien format avec copie de sauvegarde.
4. Un ordonnanceur de polling indépendant des boutons : une minuterie par macro en cours, état unique.
5. Corriger les défauts de la revue listés au §8.

## Hors périmètre

- La réécriture du panneau et de la fenêtre d'édition (sous-projet 6, GTK 4) : leur apparence ne change pas, ils sont rebranchés sur le modèle.
- Les projets et les onglets eux-mêmes : seules les interfaces du modèle et de l'ordonnanceur les prévoient.
- La mémorisation des onglets masqués du panneau (reste en mémoire, comme aujourd'hui).
- Le moteur Lua au-delà de `send_macro`/`get_macros` (sous-projet 5).

## Décisions

1. **Périmètre** (validé par l'utilisateur) : modèle + chemin unique ; fenêtres rebranchées, pas réécrites.
2. **Forme** : structures GLib et fonctions de rappel (comme `GtTransport`), pas de GObject : un ou deux observateurs, testable sans boucle GTK, intégrable à `gtkterm_core`.
3. **Autonomie** : l'utilisateur a demandé de décider seul de toute la suite (sections 3 à 5 de la conception, plan, exécution, fusion). Les choix ci-dessous non validés explicitement sont ceux présentés dans la conception.
4. **Ancienne version** : une version antérieure de GTKTerm ne lit plus un fichier de macros réécrit au nouveau format ; la copie `.avant-migration` reste.

## 1. Architecture

| Couche | Fichiers | Dépendances | Rôle |
|---|---|---|---|
| Cœur (`gtkterm_core`) | `macros_list.c/.h` | GLib | `GtMacroLists` : listes nommées (instance, plus de globale) |
| Cœur | `macros_format.c/.h` | GLib, `macros_list.h` | analyse des spécificateurs, expansion, échappements ; reçoit les listes en paramètre |
| Cœur | `macro_set.c/.h` (nouveau) | GLib, format, listes | `GtMacro`, `GtMacroSet`, identifiants, édition, expansion unique |
| Cœur | `macro_file.c/.h` (nouveau) | GLib, `macro_set.h` | lecture/écriture `.ini` v2, lecture de l'ancien format, copie avant migration |
| Cœur | `macro_poller.c/.h` (nouveau) | GLib, `macro_set.h` | ordonnanceur de polling, une minuterie par macro en cours |
| Interface | `macros.c/.h` | GTK 3, cœur, `link.h` | état applicatif des macros (jeu courant, chemin, ordonnanceur), `macros_send()`, raccourcis, fenêtre d'édition |
| Interface | `macro_panel.c/.h` | GTK 3, `macros.h` | panneau de boutons, vue du jeu et de l'ordonnanceur |
| Interface | `script_engine.c` | `macros.h` | `send_macro`/`get_macros` passent par le jeu courant et `macros_send()` |
| Interface | `term_config.c` | `macros.h` | chargement/enregistrement du fichier de macros via `macros.c` |

Les nouveaux fichiers suivent le style dominant (tabulations, accolades seules sur leur ligne, `if(`). Les fichiers existants gardent leur style (GNU pour `macros*.c`, tabulations + `if (` pour `macro_panel.c`).

Fil d'exécution : le jeu, le fichier et l'ordonnanceur ne sont utilisés que sur le fil principal. Le fil Lua y accède uniquement via `run_on_main_sync` (règle existante, C10).

## 2. Listes nommées (`macros_list.h`)

```c
typedef struct _GtMacroLists GtMacroLists;

GtMacroLists *gt_macro_lists_new          (void);
void          gt_macro_lists_free         (GtMacroLists *lists);              /* NULL accepté */
GtMacroLists *gt_macro_lists_copy         (const GtMacroLists *lists);        /* NULL → liste vide */
gint          gt_macro_lists_find         (const GtMacroLists *lists, const gchar *name);   /* -1 si absente ; lists NULL → -1 */
void          gt_macro_lists_add          (GtMacroLists *lists, const gchar *name, const gchar *display, const gchar *value);
gint          gt_macro_lists_count        (const GtMacroLists *lists);
const gchar  *gt_macro_lists_name         (const GtMacroLists *lists, gint list_idx);
gint          gt_macro_lists_entry_count  (const GtMacroLists *lists, gint list_idx);
const gchar  *gt_macro_lists_entry_display(const GtMacroLists *lists, gint list_idx, gint entry_idx);
const gchar  *gt_macro_lists_entry_value  (const GtMacroLists *lists, gint list_idx, gint entry_idx);
```

- `add` crée la liste au premier nom rencontré (ordre d'apparition conservé) et ajoute l'entrée en fin ; `value` NULL prend `display` ; `name` ou `display` NULL : rien.
- Indices hors bornes : `NULL` ou `0`, jamais de plantage.
- Les types `list_entry_t`/`macro_list_t` deviennent privés ; `macro_list_remove_entry` disparaît (code mort).

## 3. Format des actions (`macros_format.h`)

Les fonctions qui dépendent de l'existence d'une liste reçoivent les listes (`NULL` = aucune liste, `%#x` est alors toujours le drapeau printf) :

```c
macro_arg_info_t *macro_get_arg_infos    (const GtMacroLists *lists, const gchar *action, gint *count_out);
gint              macro_count_format_args(const GtMacroLists *lists, const gchar *action);
gchar            *format_action_with_args(const GtMacroLists *lists, const gchar *action, const gchar **args, gint n_args);
```

Inchangées : `macro_arg_infos_free`, `macro_type_is_float`, `macro_type_value_valid`, `parse_macro_string`. Retirées (code mort) : `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types`. Le comportement de l'expansion est inchangé (les cas de `test_macros_format` restent vrais, adaptés à la nouvelle signature).

## 4. Modèle (`macro_set.h`)

```c
#define GT_MACRO_PERIOD_MIN_MS     1u
#define GT_MACRO_PERIOD_MAX_MS     86400000u   /* 24 h */
#define GT_MACRO_PERIOD_DEFAULT_MS 1000u

typedef struct
{
	guint    id;                 /* stable pendant la vie du jeu, jamais 0, jamais réutilisé dans le processus */
	gchar   *label;              /* jamais NULL ("" possible) */
	gchar   *shortcut;           /* jamais NULL ; "" = aucun raccourci */
	gchar   *action;             /* jamais NULL */
	gchar   *tab;                /* jamais NULL ; "" = onglet par défaut (« General » traduit, décidé par l'interface) */
	gchar  **args;               /* NULL, ou exactement macro_count_format_args() éléments */
	gboolean polling;            /* mode polling activé (persistant) ; la marche n'est jamais persistée */
	guint    polling_period_ms;  /* toujours dans [GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS] */
} GtMacro;

typedef struct _GtMacroSet GtMacroSet;

typedef struct
{
	guint        id;             /* 0 = nouvelle ligne */
	const gchar *label;
	const gchar *shortcut;
	const gchar *action;
	const gchar *tab;
} GtMacroEdit;

GtMacroSet    *gt_macro_set_new          (void);
void           gt_macro_set_free         (GtMacroSet *set);                  /* NULL accepté */
guint          gt_macro_set_count        (const GtMacroSet *set);
const GtMacro *gt_macro_set_get          (const GtMacroSet *set, guint index);     /* NULL hors bornes */
const GtMacro *gt_macro_set_find         (const GtMacroSet *set, guint id);        /* NULL si inconnu */
gint           gt_macro_set_index_of     (const GtMacroSet *set, guint id);        /* -1 si inconnu */
const GtMacro *gt_macro_set_find_label   (const GtMacroSet *set, const gchar *label); /* première macro de ce libellé */
guint          gt_macro_set_append       (GtMacroSet *set, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab);
const GtMacroLists *gt_macro_set_get_lists(const GtMacroSet *set);
void           gt_macro_set_take_lists   (GtMacroSet *set, GtMacroLists *lists);   /* remplace ; recalcule la validité des arguments */
gboolean       gt_macro_set_set_arg      (GtMacroSet *set, guint id, gint arg_index, const gchar *value);
gboolean       gt_macro_set_set_args     (GtMacroSet *set, guint id, const gchar *const *args, gint n_args);
gboolean       gt_macro_set_set_polling  (GtMacroSet *set, guint id, gboolean polling, guint period_ms);
void           gt_macro_set_apply_edits  (GtMacroSet *set, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists);

gboolean       gt_macro_action_is_separator(const gchar *action, gchar **label_out);
gboolean       gt_macro_is_sendable      (const GtMacro *macro);
GBytes        *gt_macro_expand           (const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args);
```

Règles :
- **Normalisation à l'entrée** (`append`, `apply_edits`, lecture du fichier) : `NULL` → `""` ; un raccourci `"None"` (ancienne valeur « pas de raccourci ») → `""`.
- **Identifiants** : compteur du processus (fil principal), commençant à 1. `append` et `apply_edits` (ligne d'identifiant 0 ou inconnu) en attribuent un nouveau ; un nouveau jeu ne réutilise jamais un identifiant d'un ancien jeu.
- **Arguments** : `set_arg` crée le tableau à la bonne taille (éléments `""`) si besoin ; refuse (`FALSE`) un indice hors `[0, n)` ou une macro inconnue ; `set_args` refuse un nombre différent de `n`. `take_lists` et `apply_edits` remettent `args` à `NULL` pour toute macro dont le nombre d'arguments enregistrés n'est plus égal au nombre de spécificateurs.
- **Polling** : `set_polling` borne la période dans `[MIN, MAX]` (0 → `DEFAULT`) ; refuse une macro inconnue.
- **`apply_edits`** (validation de la fenêtre d'édition, correctif de M16) : les listes d'abord (si `lists` non NULL, il est pris ; sinon les listes sont gardées), puis le jeu devient exactement les lignes, dans leur ordre. Une ligne dont l'identifiant existe garde cet identifiant, ses arguments (si leur nombre correspond toujours) et ses réglages de polling ; une ligne nouvelle a polling désactivé, période par défaut, pas d'arguments. Une macro absente des lignes est supprimée. Deux lignes portant le même identifiant : la première le garde, la seconde est traitée comme nouvelle.
- **Séparateur** : `gt_macro_action_is_separator` reconnaît `%&` et `[Libellé]%&` (espaces permis entre `]` et `%&`), comme `macro_is_separator` aujourd'hui (déplacé du panneau vers le cœur).
- **Envoyable** : action non vide et non séparateur.
- **Expansion unique** : `n_args < 0` → arguments enregistrés (`NULL` → zéro argument) ; sinon les arguments fournis. Résultat : `format_action_with_args` avec les listes du jeu, puis `parse_macro_string`, en `GBytes` (vide possible, jamais `NULL` pour une macro non NULL).

## 5. Fichier (`macro_file.h`)

```c
gboolean gt_macro_file_load(const gchar *path, GtMacroSet **set_out, GError **error);
gboolean gt_macro_file_save(const GtMacroSet *set, const gchar *path, GError **error);
```

### 5.1 Format v2

Une macro ou une liste par groupe ; l'ordre des groupes dans le fichier fait l'ordre du jeu. Les noms vont dans des clés (un nom de groupe ne peut contenir ni `[` ni `]`). Toutes les chaînes passent par `g_key_file_set_string`/`set_string_list` : `;`, `\`, espaces de tête, `::`, `|`, `=` et UTF-8 relus à l'identique.

```ini
[gtkterm-macros]
version=2

[macro:1]
label=Lecture
shortcut=<Control>F5
action=R%d::x\\r
tab=Général
args=42;
polling=true
polling_period_ms=1000

[list:1]
name=Vitesses
displays=Lent;Rapide
values=0;1
```

Écriture : groupe `[gtkterm-macros]` (`version=2`) puis `[macro:N]` (N = rang à partir de 1) puis `[list:N]`. `args` est écrit seulement si la macro a des arguments enregistrés ; `shortcut`/`tab` vides écrits vides. Écriture atomique (`g_file_set_contents`) ; erreur → `FALSE` + `GError` dont le message cite le chemin.

Lecture v2 :
- `version` absente, non entière ou différente de 2 dans `[gtkterm-macros]` : erreur `G_KEY_FILE_ERROR_INVALID_VALUE` (« format plus récent ») ; rien n'est remplacé.
- Groupe `macro:*` : clés manquantes → `""` ; `args` gardé seulement si sa longueur est égale au nombre de spécificateurs (listes du fichier comprises) ; `polling` absent ou invalide → faux ; `polling_period_ms` absent ou invalide → défaut, sinon borné.
- Groupe `list:*` : `name` vide ou absent → groupe ignoré ; `values` manquantes ou plus courtes → la valeur prend l'affichage ; valeurs en trop ignorées ; même nom dans deux groupes → entrées fusionnées dans l'ordre.
- Autres groupes ignorés (et donc perdus au prochain enregistrement).
- Les listes sont lues avant les macros (le décompte des arguments en dépend).
- *Amendé à la relecture finale :* `label`, `shortcut`, `action`, `tab` et `name` dont l'échappement est invalide (`action=AT\x0D` tapé à la main : `G_KEY_FILE_ERROR_INVALID_VALUE`) sont lus en texte brut (`g_key_file_get_value`, la syntaxe des macros) au lieu d'être vidés puis enregistrés vides.

### 5.2 Ancien format et migration

Un fichier sans `[gtkterm-macros]` est lu comme l'ancien format, avec l'analyse actuelle de `macros_file_load` (séparateurs `::` et `|`, listes `Nom:aff=val|…`), sans ses défauts :
- clé de `[macros]` dont la valeur est vide, absente ou contient moins de deux `::` : ignorée (pas de macro fantôme) ;
- sans `[macros]` : le jeu n'a aucune macro (les listes éventuelles sont lues) — plus de mélange « anciennes macros, nouvelles listes » ;
- plus de fuite de `macro_keys` ni de `GError` réutilisé.

Un fichier ni v2 ni ancien format (vide, ou sans aucun des groupes connus) donne un jeu vide sans erreur.
*Amendé à la relecture finale :* seul un fichier vide (ou sans aucun groupe) donne un jeu vide. Un fichier non vide avec des groupes mais aucun groupe connu (`[gtkterm-macros]`, `[macros]`, `[lists]`) est refusé : `FALSE` + `G_KEY_FILE_ERROR_GROUP_NOT_FOUND`, « %s is not a GTKTerm macros file » avec le chemin (un fichier étranger n'est plus lu comme un jeu vide qui l'écraserait ensuite).

**Copie avant migration** : `gt_macro_file_save` regarde le fichier présent sur le disque à `path` ; s'il est à l'ancien format (lisible, sans `[gtkterm-macros]`, avec `[macros]` ou `[lists]`) et que `<path>.avant-migration` n'existe pas, il le copie d'abord à l'identique. La copie n'est jamais écrasée. Échec de la copie → l'enregistrement échoue (`FALSE`), le fichier ancien reste intact.
*Amendé à la relecture finale :* la copie couvre tout fichier présent, non vide, qui n'est pas un v2 lisible (ancien format, syntaxe invalide, version inconnue, fichier étranger, fichier qu'on ne peut pas lire). Si `<path>.avant-migration` existe déjà avec un contenu identique au fichier, l'enregistrement continue ; avec un contenu différent, il est refusé (`FALSE` + `G_FILE_ERROR_EXIST`, message citant les deux chemins) et rien n'est écrasé. Un fichier présent mais qu'on ne peut pas lire (droits) fait aussi échouer l'enregistrement, faute de pouvoir le copier.

### 5.3 Erreurs

- Fichier absent : `FALSE` + `G_FILE_ERROR_NOENT` ; l'appelant décide (voir §6.1).
- Fichier illisible ou syntaxe GKeyFile invalide : `FALSE` + `GError` ; `*set_out` inchangé.
- *Amendé à la relecture finale :* fichier étranger (des groupes, aucun de connu) : `FALSE` + `G_KEY_FILE_ERROR_GROUP_NOT_FOUND` ; le message de refus de version cite lui aussi le chemin du fichier.

## 6. Interface

### 6.1 État applicatif (`macros.c` / `macros.h`)

`macros.c` garde l'état des macros de l'application (un seul jeu aujourd'hui) et remplace le tableau global `macro_t *macros` (supprimé, ainsi que `macro_t`, `get_shortcuts`, `create_shortcuts`, `send_macro_with_arg(s)`, `macro_set_arg`, `macro_set_polling`) :

```c
GtMacroSet   *macros_get_set        (void);                 /* jamais NULL après macros_init */
GtMacroPoller *macros_get_poller    (void);
void          macros_init           (void);                 /* jeu vide, ordonnanceur sur la liaison par défaut */
gboolean      macros_send           (guint id, const gchar *const *args, gint n_args);  /* n_args < 0 : arguments enregistrés */
gboolean      macros_load_file      (const gchar *path, gboolean interactive);
gboolean      macros_save_file      (const gchar *path, gboolean interactive);   /* NULL : chemin courant */
const gchar  *macros_file_get_path  (void);                 /* chemin courant (peut être NULL avant tout chargement) */
gchar        *macros_file_default_path(void);               /* alloué, à libérer */
void          macros_changed        (void);                 /* raccourcis reconnectés + panneau reconstruit */
void          Config_macros         (GtkAction *action, gpointer data);
```

- **`macros_send`** : macro inconnue ou non envoyable → `FALSE`, rien n'est envoyé. Sinon `gt_macro_expand`, `gt_link_send(gt_link_get_default(), …, GT_SEND_MACRO)` si non vide, message temporaire « Macro "…" sent! » (libellé, à défaut raccourci ; *amendé à la relecture finale :* seulement si au moins un octet a été écrit — port fermé ou perdu, pas de message et « — lost » reste visible). Renvoie `TRUE` si tous les octets ont été écrits. C'est le **seul** envoi de macro de l'interface : clic de bouton, bouton de liste à deux valeurs, raccourci, ordonnanceur, Lua.
- **`macros_init`** est appelé dans `main` avant `create_main_window()` (le panneau lit le jeu dès sa création).
- **`macros_load_file`** : succès → le jeu courant est remplacé (l'ancien libéré), l'ordonnanceur passe au nouveau jeu en arrêtant tout (`gt_macro_poller_set_set`), le chemin devient `path`, `macros_changed()`. Fichier absent → jeu vide, chemin = `path`, pas de message (premier lancement, profil pointant un fichier pas encore créé). Autre erreur → jeu courant et chemin inchangés ; message dans une boîte si `interactive`, sinon `g_warning`.
  *Amendé à la relecture finale :* seul le chargement interactif (menu) garde jeu et chemin inchangés. Un échec non interactif (profil) donne un jeu vide et prend quand même `path` comme chemin courant : les enregistrements suivants visent ce fichier, protégé par la copie `.avant-migration` du §5.2, jamais le fichier par défaut, et `config_remember_macros_file()` ne redirige plus le profil. L'erreur est signalée par `g_warning` plus une boîte différée (`g_idle_add` → `show_message`) ; `macros_load_file` renvoie `FALSE`. Un fichier absent reste « jeu vide sans message ».
- **`macros_save_file`** : écrit le jeu courant ; succès avec `path` non NULL → le chemin courant devient `path`. Échec → boîte si `interactive`, sinon `g_warning`. Les enregistrements implicites (argument modifié, polling basculé, période changée, validation de la fenêtre) sont interactifs une fois par message : un même échec répété n'ouvre pas une boîte à chaque frappe (une seule boîte tant que le chemin et le message d'erreur ne changent pas).
  *Amendé à la relecture finale :* ce filtre ne vaut que pour les enregistrements implicites ; le menu « Save macros file » appelle d'abord `macros_forget_save_error()`, si bien qu'un enregistrement explicite réaffiche toujours son erreur.
- `term_config.c` appelle `macros_load_file`/`macros_save_file` (chargement de profil : non interactif ; menus « Charger/Enregistrer le fichier de macros » : interactif) puis `config_remember_macros_file()` comme aujourd'hui ; la fuite `g_path_get_dirname` des deux dialogues de fichier est corrigée.

### 6.2 Raccourcis

- `macros_changed()` déconnecte toutes les fermetures puis connecte une fermeture par macro au raccourci analysable, avec l'**identifiant** en donnée (plus l'indice).
- Rappel : `macros_send(id, NULL, -1)` ; macro introuvable ou non envoyable → `FALSE` (la touche suit son cours, M43 conservé) ; sinon `TRUE`.

### 6.3 Fenêtre d'édition

- Le modèle de la liste porte une colonne cachée `id` (0 pour une ligne ajoutée). Tri, déplacement, ajout, suppression ne touchent que le modèle de la fenêtre.
- « Enregistrer » : construit les `GtMacroEdit` dans l'ordre affiché, les listes depuis l'onglet des listes (`GtMacroLists` neuve), appelle `gt_macro_set_apply_edits`, informe l'ordonnanceur (`gt_macro_poller_sync`), `macros_changed()`, enregistre (`macros_save_file(NULL, TRUE)`).
- Colonne raccourci : `""` affiché comme « None » (traduit), retraduit en `""` à la validation.
- Les trois gestionnaires `label_edited`/`action_edited`/`tab_edited` deviennent un seul gestionnaire paramétré par la colonne ; idem pour les trois gestionnaires de l'onglet des listes. `lists_model` est libéré à la fermeture de la fenêtre (fuite à chaque ouverture).
- L'aide de la fenêtre est inchangée (corrigée au sous-projet 1).

### 6.4 Panneau (`macro_panel.c`)

- Le panneau est une vue : les boutons portent l'**identifiant** de leur macro (`g_object_set_data`), une table identifiant → bouton est reconstruite à chaque `rebuild_macro_buttons()` et sert aux mises à jour d'état.
- **Arguments** : un champ ou une liste déroulante écrit dans le modèle (`gt_macro_set_set_arg`) sur Entrée, perte de focus ou changement de sélection, puis enregistre le fichier (comme aujourd'hui). Un clic sur le bouton d'une macro à arguments écrit d'abord la valeur de tous ses champs dans le modèle, puis envoie (`macros_send(id, NULL, -1)`) ou bascule le polling. Il n'y a plus de copie d'arguments propre au polling.
- **Bouton de liste à deux valeurs** : envoie avec la valeur du bouton (`macros_send(id, {valeur}, 1)`) ; pas de menu de polling (comme aujourd'hui).
- **Reconstruire le panneau ne touche pas à l'ordonnanceur.** L'apparence d'un bouton se calcule depuis le modèle (`polling`) et l'ordonnanceur (`gt_macro_poller_is_running`).
- **Libellé** : « ⏱ Libellé » si le mode polling est activé ; chaîne libérée (fuite de `update_button_label`).
- **Info-bulle** : construite par `g_strdup_printf` (plus de `tooltip[256]` qui coupe l'UTF-8).
- **Menu contextuel de polling** : case « Polling Mode » (bascule `polling` dans le modèle et enregistre ; désactiver arrête la macro si elle tournait) ; champ « Period (ms) » appliqué sur Entrée et à la fermeture du menu, **jamais à chaque frappe** (M22). Valeur valide : entier décimal dans `[1, 86400000]` ; sinon le champ passe en rouge (classe `error`), rien n'est appliqué, l'ancienne période reste. Une période appliquée à une macro en cours relance sa minuterie.
- **Masquer un onglet** arrête seulement les pollings des macros de cet onglet ; les autres continuent (note EM-1).
- **Clignotement** : une minuterie de 500 ms qui n'existe que tant qu'au moins une macro tourne (démarrée/arrêtée sur le rappel `running-changed`).
- `macro_stack` (inutilisé) disparaît.

### 6.5 Lua (`script_engine.c`)

- `send_macro(indice|libellé, …)` : résolution sur le fil principal dans le jeu courant (indice 0-based comme aujourd'hui, ou premier libellé égal), puis `macros_send(id, args, n)`. **Sans argument Lua, les arguments enregistrés sont utilisés** (`n_args = -1`), comme pour le raccourci et le polling ; avec des arguments, ceux-ci. Retour inchangé : `true`/`false` selon l'écriture complète, `false, "macro not found"` si introuvable ou non envoyable.
- `get_macros()` : instantané du jeu courant sur le fil principal (libellés, infos d'arguments avec les listes du jeu), comme aujourd'hui.

## 7. Ordonnanceur (`macro_poller.h`)

```c
typedef struct _GtMacroPoller GtMacroPoller;

typedef gboolean (*GtMacroPollerSendFunc)   (guint id, gpointer user_data);         /* envoie la macro avec ses arguments enregistrés */
typedef void     (*GtMacroPollerStateFunc)  (guint id, gboolean running, gpointer user_data);

GtMacroPoller *gt_macro_poller_new       (GtMacroSet *set, GtMacroPollerSendFunc send, GtMacroPollerStateFunc state_changed, gpointer user_data);
void           gt_macro_poller_free      (GtMacroPoller *poller);   /* arrête tout, sans rappel d'état */
void           gt_macro_poller_set_set   (GtMacroPoller *poller, GtMacroSet *set);  /* nouveau jeu : arrête tout */
gboolean       gt_macro_poller_start     (GtMacroPoller *poller, guint id);
void           gt_macro_poller_stop      (GtMacroPoller *poller, guint id);
gboolean       gt_macro_poller_toggle    (GtMacroPoller *poller, guint id);  /* renvoie le nouvel état */
gboolean       gt_macro_poller_is_running(const GtMacroPoller *poller, guint id);
guint          gt_macro_poller_running_count(const GtMacroPoller *poller);
void           gt_macro_poller_reset     (GtMacroPoller *poller);   /* arrête tout (rappels émis) */
void           gt_macro_poller_sync      (GtMacroPoller *poller);   /* après une modification du jeu */
void           gt_macro_poller_period_changed(GtMacroPoller *poller, guint id);
```

- `start` : refusé (`FALSE`) si la macro est inconnue, non envoyable ou n'a pas le mode polling ; si déjà en cours, `TRUE` sans rien faire. Sinon **envoi immédiat**, puis une minuterie GLib (`g_timeout_add`, contexte par défaut) à `polling_period_ms` qui renvoie la macro à chaque échéance ; rappel d'état `running = TRUE`.
- Les minuteries sont en millisecondes GLib (`guint`), plus de calcul `period_ms * 1000` ni de minuterie à 1 ms permanente (M22). Le maximum 86 400 000 ms tient dans un `guint`.
- `stop`/`reset` : minuterie retirée, rappel `running = FALSE` pour chaque macro qui tournait.
- `sync` : pour chaque macro en cours, si elle n'existe plus, n'est plus envoyable ou n'a plus le mode polling → arrêtée ; si sa période a changé → minuterie relancée à la nouvelle période (sans envoi immédiat).
- `period_changed` : relance la minuterie de la macro si elle tourne (sans envoi immédiat).
- L'envoi se fait toujours par la fonction fournie (dans l'application : `macros_send(id, NULL, -1)`), donc avec les arguments enregistrés et vers la liaison de l'application. Liaison non ouverte : l'envoi n'écrit rien, le polling continue et reprend de lui-même à la reconnexion.
- Les rappels d'état ne sont jamais émis depuis `gt_macro_poller_free`. Le rappel `send` peut arrêter la macro (via `stop`) sans corrompre l'ordonnanceur.

## 8. Défauts et remarques traités

| Référence | Correctif |
|---|---|
| M16 | identité stable et `apply_edits` (§4, §6.3) |
| M21 | format v2 échappé, lecture de l'ancien format sans fuite ni mélange (§5) |
| M22 | période appliquée sur Entrée/fermeture, bornée ; une minuterie par macro ; pas de débordement (§6.4, §7) |
| Architecture point 3 | état du polling unique (modèle + ordonnanceur), arguments uniques (modèle) |
| Note EM-1 (SP1) | masquer un onglet n'arrête que ses pollings ; la reconstruction ne touche pas à l'ordonnanceur |
| Remarques reportées | fuite `update_button_label`, fuite `lists_model`, fuite `g_path_get_dirname`, clé `.ini` sans valeur, `tooltip[256]`, `macros_file_get_default_path` qui alloue en se déclarant `const` |
| Code mort | `send_macro_with_arg`, `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types`, `macro_list_remove_entry`, `macro_stack` |
| Duplication | gestionnaires `*_edited` de la fenêtre d'édition ; deux chemins d'expansion+envoi (`macros.c`, `script_engine.c`) |

La revue est mise à jour (M16, M21, M22 marqués « Corrigé par le sous-projet 4 (`feature/refonte-macros`) » ; remarques reportées correspondantes annotées).

## 9. Tests

Unitaires (cœur, `gtkterm_core`) :
- `test_macros_list` : adapté à `GtMacroLists` (instance) ; copie indépendante ; indices hors bornes.
- `test_macros_format` : adapté aux listes en paramètre ; `%#x` avec et sans liste « x » ; `NULL` comme listes.
- `test_macro_set` (nouveau) : normalisation (`NULL`, `"None"`) ; identifiants uniques, jamais réutilisés entre deux jeux ; `set_arg`/`set_args` (bornes, création du tableau) ; `set_polling` (bornes 0, 1, 86 400 000, au-delà) ; **`apply_edits` après suppression, déplacement, tri, ajout et identifiant dupliqué : arguments et polling suivent la bonne macro (M16)** ; arguments remis à `NULL` quand le nombre de spécificateurs change (y compris par changement de listes) ; séparateurs ; `is_sendable` ; `expand` avec arguments enregistrés, fournis, aucun, et liste.
- `test_macro_file` (nouveau, répertoire temporaire isolé) : aller-retour v2 avec `::`, `|`, `;`, `=`, `\`, `\r` littéral, espaces de tête, UTF-8, arguments vides, listes ; ordre conservé ; version absente ou 3 refusée sans remplacer ; clés manquantes ou invalides ; période bornée ; ancien format (fixtures : complet, sans `[macros]`, clé vide, `::` de moins, liste sans `=`) ; fichier vide ou inconnu → jeu vide ; fichier absent → `G_FILE_ERROR_NOENT` ; copie `.avant-migration` faite au premier enregistrement d'un ancien fichier, identique à l'original, jamais écrasée, absente pour un fichier v2 ; échec d'écriture (répertoire absent) → `FALSE` avec chemin dans le message. *Amendé à la relecture finale :* fichier inconnu (`[autre]`) → erreur ; copie avant d'écraser une version 3, une syntaxe invalide, un fichier étranger ; refus si une copie différente existe ; antislash invalide relu en texte brut ; `test_macros_send` : chargement de profil d'un fichier abîmé (jeu vide, chemin retenu, boîte différée, copie au premier enregistrement).
- `test_macro_poller` (nouveau, boucle GLib, périodes de 20 à 50 ms) : envoi immédiat puis périodique ; `start` refusé (inconnue, non envoyable, sans mode polling) ; `stop`, `toggle`, `reset` et rappels d'état ; `sync` après suppression, désactivation du mode, changement de période ; période maximale acceptée sans débordement ; arrêt depuis le rappel d'envoi ; `free` sans rappel.

Intégration (bouchons d'interface) :
- `test_macros_send` : adapté — `macros_send` par identifiant avec arguments enregistrés, fournis et `%%` ; raccourci par identifiant (connecté une fois, renvoie TRUE/FALSE) ; macro inexistante ; chargement d'un fichier : arguments de liste conservés.
- `test_script_engine` : `send_macro` par indice et par libellé, avec et sans arguments (sans → arguments enregistrés), « macro not found », via le bouchon du jeu courant.

Suite graphique (isolée, broadway) :
- `test_polling.py` étendu : masquer un autre onglet laisse tourner le polling ; taper `2000` dans la période n'envoie rien à 2 ms et la période s'applique sur Entrée ; valeur `0` refusée (champ en erreur).
- `test_macros_edition.py` (nouveau) : supprimer puis déplacer une ligne dans la fenêtre d'édition, valider : le polling et les arguments restent sur la bonne macro (M16) ; une action contenant `::` et `|` survit à l'enregistrement et au rechargement.
- `test_macros_migration.py` (nouveau) : un ancien `.ini` est chargé au démarrage (macros, arguments, listes visibles), réécrit au nouveau format au premier enregistrement, avec `.avant-migration` identique à l'original.
- `test_raccourcis_macros.py` : inchangé, doit rester vert.

## 10. Critères d'acceptation

1. `meson test -C build` : tout vert ; aucun `# TODO` restant pour M16, M21, M22.
2. Build `build-asan` (`-Db_sanitize=address,undefined`) : `meson test -C build-asan` vert, aucun rapport LeakSanitizer sur les nouveaux tests.
3. `meson test -C build --suite gui` : tout vert (anciens et nouveaux scénarios).
4. Plus aucune référence à `macro_t`, `get_shortcuts`, `create_shortcuts`, `send_macro_with_arg`, `macro_polling_t`, `macro_list_find` (ancienne API globale) dans `src/` et `tests/`.
5. `CLAUDE.md` (paragraphe « Macros subsystem » et liste `core_sources`), `tests/README.md` si besoin, `po/POTFILES.in` et `po/fr.po` (nouveaux messages) à jour.
