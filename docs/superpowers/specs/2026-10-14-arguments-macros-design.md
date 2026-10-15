# Arguments de macros : limites, contraintes des textes, aperçu de la ligne envoyée

Date : 2026-10-14. Suite de 6c (projets), chantier (3) du découpage du 2026-10-14. Le chantier (4), modèles de macros liés et surcharge par projet,
viendra après et n'est pas traité ici.

## 1. Ce que l'utilisateur a demandé

- Pour chaque argument d'une macro : une **valeur par défaut** (existe depuis 6c) et des **limites optionnelles selon le type** :
  - entier et décimal : minimum et maximum ;
  - `%s` : longueur minimale et maximale **en octets envoyés**, et **jeu de caractères** : UTF-8 libre (aujourd'hui), ASCII imprimable, ASCII complet,
    8 bits (Latin-1), classe personnalisée du type `A-Z0-9_-` ;
  - `%c` et liste : valeur par défaut seulement.
- Voir à quoi ressemble la **ligne de macro** envoyée : texte, arguments, échappements, octets en hexadécimal.
- Maquette validée (« La maquette est super », planche 3) : tableau des arguments sous la liste des macros, aperçu en direct ; dans le panneau, un
  champ vide montre le défaut en grisé et c'est lui qui est envoyé ; un champ hors limites est rouge et l'envoi est refusé.

## 2. Décisions

1. **Limites rangées avec la macro**, comme `args` : trois vecteurs parallèles aux arguments dans `GtMacro`, `arg_min`, `arg_max`, `arg_charset`
   (chaînes, `""` = aucune contrainte), même règle de longueur que `args` (un vecteur dont la longueur ne correspond plus au nombre d'arguments est
   abandonné, comme `args` aujourd'hui). Pour `%s`, `min`/`max` sont des **longueurs en octets** ; pour les nombres, des **valeurs**. Ce choix évite un
   quatrième et un cinquième vecteur et garde le fichier lisible.
2. **Fichier** (`.ini` v2 et projet, via `gt_macro_set_to_key_file` / `from_key_file`, donc le même code pour les deux) : clés de liste
   `arg_min`, `arg_max`, `arg_charset` dans le groupe de la macro, écrites seulement si l'une de leurs valeurs n'est pas vide. La version du fichier
   reste 2 : un GTKTerm plus ancien ignore ces clés. Le texte comparé pour « projet modifié » les contient donc automatiquement.
3. **Jeux de caractères** (valeur de `arg_charset`) : `""` UTF-8 libre ; `printable` 0x20–0x7E ; `ascii` 0x00–0x7F ; `latin1` U+0000–U+00FF,
   **envoyé en un octet par caractère** (conversion ISO-8859-1 : c'est ce qui donne un sens à « 8 bits », la saisie restant de l'UTF-8) ;
   `class:<classe>` : caractères de la classe, envoyés en UTF-8. Syntaxe de classe : caractères isolés et plages `a-b` (points de code, a ≤ b) ;
   `-` littéral en premier ou en dernier ; `\` protège le caractère suivant (`\-`, `\\`) ; classe vide, plage inversée ou `\` final = invalide ;
   256 caractères au plus.
4. **Les arguments restent littéraux** : un `\r` tapé dans un champ envoie `\` puis `r` (comportement actuel, M19). La longueur d'un `%s` est celle
   de la **valeur encodée** (UTF-8, ou Latin-1), avant la largeur printf (`%5s` ne change pas la longueur contrôlée ; une précision `%.3s` tronque ensuite la valeur, au choix de l'auteur de la macro, comme aujourd'hui).
   `%c` n'a ni limite ni contrôle (il prend le premier octet, une espace si vide, comme aujourd'hui). Le jeu `ascii` admet 0x00–0x7F, même si les
   caractères de contrôle ne se tapent guère dans un champ.
5. **Vérification d'une valeur** (`gt_macro_arg_check`) : d'abord la validité du type (règles actuelles de `macro_type_value_valid`), puis les
   limites. Un nombre vide vaut 0, comme à l'envoi : il est comparé aux limites comme 0. Réponse : un verdict (`OK`, `BAD_VALUE`, `BELOW_MIN`,
   `ABOVE_MAX`, `TOO_SHORT`, `TOO_LONG`, `BAD_CHAR`) et un texte traduit (« above maximum 100 », « 9 bytes, at most 8 », « character "é" not allowed »).
6. **Limites elles-mêmes** (`gt_macro_arg_limit_valid`) : nombre valide pour le type de l'argument (entier : décimal ou `0x…`, signé seulement pour
   `d`/`i` ; décimal : point) ; longueur `%s` : entier de 0 à 65 535 ; `min ≤ max` quand les deux sont donnés ; `%c` et liste : aucune limite permise.
   Une limite doit elle-même être une valeur valide du type (`300` est refusé pour un `%hhu`). Domaines de comparaison : `gint64` pour `d`/`i`,
   `guint64` pour `u`/`o`/`x`/`X` (lecture actuelle, `-1` compris), `double` pour les décimaux ; `nan` et `inf` sont refusés dès qu'une limite
   existe. Au chargement, une limite invalide est remplacée par `""` avec un avertissement qui cite le fichier, le groupe et la clé (comme une évasion
   invalide). `check_args` revalide aussi chaque limite pour le type de l'argument de même rang (l'action a pu passer de `%s %d` à `%d %s` sans
   changer de nombre) et remet `""` sans avertissement. Dans l'éditeur, un champ de limite invalide est rouge et n'est pas retenu, comme un défaut invalide aujourd'hui.
7. **Valeurs d'usage et champ vide** (change le comportement de 6c) : dans le panneau, un champ d'entrée vide signifie « valeur par défaut ». Il est
   affiché vide avec le défaut en texte indicatif grisé (à défaut, l'indication actuelle `text`/`0`/`0.0`) ; sa largeur suit le plus long du texte et
   de l'indication (4 à 24 caractères). Conséquence assumée : on ne peut plus envoyer `""` pour un `%s` dont le défaut n'est pas vide (il suffit de
   ne pas lui donner de défaut). `gt_macros_get_args()` rend désormais un vecteur **résolu** argument par argument : valeur d'usage non vide, sinon
   défaut, sinon `""`. Exception gardée : sans défaut (`GtMacro.args` NULL) **et** sans valeur d'usage valable, il rend NULL comme aujourd'hui
   (l'action part avec ses spécificateurs, `V%d` envoie `V%d`). Un vecteur d'usage de mauvaise longueur est ignoré en entier (et toujours purgé par
   `gt_macros_apply_edits`). Le vecteur résolu appartient à `GtMacros` (cache par identifiant) et reste valable jusqu'au prochain appel pour cette
   macro ou au prochain changement du jeu ou des valeurs d'usage. Nouvelle fonction `gt_macros_get_usage()` : valeurs d'usage brutes (NULL : aucune),
   seules utilisées par le panneau pour remplir ses champs et pour comparer avant d'écrire. « Reset arguments » vide donc les champs.
   `gt_macros_has_usage` est inchangé (présence du vecteur). Une liste déroulante n'a pas d'état vide : inchangé.
8. **Envoi refusé** : clic, raccourci, polling et Lua vérifient les arguments résolus (ou ceux du script) **avant** d'écrire, numérotés à partir de 1
   dans les messages. Un argument invalide : rien n'est envoyé, `send` rend FALSE ; pour l'envoi annoncé (panneau, raccourci, polling), message
   temporaire « Macro "%s" not sent: argument %d: %s » (3 s, barre d'état de l'onglet), **pas répété pour la même macro moins de 3 s après** (un
   polling à 1 ms n'inonde pas la barre d'état). Le polling continue. Lua : `main_send_macro_cb` appelle d'abord `gt_macros_check_args` ;
   refus → `false, "invalid argument"` ; les nombres Lua à valeur entière (`5.0`) sont passés sous forme entière (`lua_tointegerx`), comme
   `strtol` les acceptait. Un nombre d'arguments Lua différent de celui de la macro : seuls les arguments fournis qui ont un rang d'argument sont
   vérifiés. `tests/stubs/stub_macros.c` fournit `gt_macros_check_args`.
9. **Expansion : les octets ne changent pas.** Les octets envoyés restent calculés exactement comme aujourd'hui : `format_action_with_args()` puis
   `parse_macro_string()` (un échappement à cheval sur un argument, `\x%02X`, `\x4%d`, `\%c`, garde son effet ; le retrait des libellés `[x]`
   reste fait sur le texte brut). Seul ajout : la mise en forme note, pour chaque argument, sa plage dans le texte mis en forme, et
   l'interprétation des échappements note, pour chaque octet produit, sa plage d'origine. `gt_macro_expand_segments()` en déduit les segments :
   échappement (séquence de plusieurs caractères, y compris à cheval sur un argument), argument N (octet issu de sa plage), texte sinon ; segments
   consécutifs de même sorte fusionnés. `gt_macro_expand()` rend les mêmes octets. Un test d'égalité porte sur tous les cas de `test_macros_format`
   plus `\x%02X`, `\x4%d`, `\%c`, `[a\r] %d`. Encodage `latin1` : la valeur est convertie avant la mise en forme printf (l'antislash 0x5C reste
   doublé comme aujourd'hui).
10. **Aperçu** : sous la table de l'éditeur, pour une seule ligne sélectionnée envoyable, « Line sent (default values): » puis la ligne en
    monospace : texte normal, arguments en bleu (rouge si invalides), échappements et caractères de contrôle en violet, nommés (`CR`, `LF`, `TAB`,
    `ESC`, `NUL`, `BEL`, `BS`, `VT`, `FF`, `DEL`, sinon `\xHH` ; un octet ≥ 0x80 qui ne forme pas d'UTF-8 valide : `\xHH`), puis les octets en
    hexadécimal majuscule séparés par des espaces et « · N bytes ». Au-delà de 256 octets, l'hexa est tronqué avec « … ». Mis à jour à chaque frappe
    dans l'action, un défaut, une limite, ou au retour de la page Lists. L'aperçu montre les valeurs **retenues** par la ligne (celles qu'OK
    appliquera) : un champ invalide, non retenu, n'y apparaît pas ; un défaut retenu qui ne respecte plus des limites changées ensuite y est rouge.
11. **Table des arguments** (remplace la rangée « Default values: » de 6c, même nom `macro-defaults`, visible seulement si la macro sélectionnée a des
    arguments) : une ligne par argument, colonnes `#`, `Format` (le spécificateur, ex. `%.1f`, `%#Mode`), `Type`, `Default`, `Min`, `Max`, `Charset`.
    Type : « integer », « unsigned », « hexadecimal », « octal », « decimal », « text », « character », « list ». Pour `%s`, les en-têtes restent
    Min/Max et le type dit « text (bytes) ». Charset : `GtkDropDown` (« UTF-8 », « Printable ASCII », « ASCII », « 8 bits (Latin-1) »,
    « Custom class ») suivi d'une entrée de classe visible seulement pour « Custom class ». Cellules sans objet : vides. Les lignes de la table
    sont associées aux arguments **par rang**. Noms : `macro-default-<k>` (gardé de 6c), `macro-min-<k>`, `macro-max-<k>`, `macro-charset-<k>`,
    `macro-class-<k>` ; aperçu `macro-preview` (texte) et `macro-preview-bytes`. Un défaut est retenu seulement s'il passe la vérification du
    point 5 avec les limites affichées ; une limite seulement si elle est valide. Changer une limite n'efface pas les valeurs d'usage (le champ du
    panneau passe simplement en rouge) ; changer un défaut les efface, comme en 6c.
12. **Panneau** : chaque champ porte une bulle d'aide qui décrit l'argument (`gt_macro_arg_describe`, ex. « integer, 0 – 100 (default 50) »,
    « text, 1 – 8 bytes, printable ASCII »), complétée par la raison quand il est invalide ; la classe `error` suit la vérification du point 5 sur la
    valeur résolue.
13. **Hors périmètre** : expressions régulières, échappements dans les valeurs d'argument, limites sur `%c` et les listes, liens vers des modèles.

## 3. Modules

- **`src/macro_arg.c` / `.h` (cœur, nouveau)** : `GtMacroArgCharset` (`UTF8`, `PRINTABLE`, `ASCII`, `LATIN1`, `CLASS`),
  `gt_macro_arg_charset_parse(text, &kind, &class)` (FALSE si invalide), `gt_macro_arg_class_valid`, `gt_macro_arg_class_contains`,
  `gt_macro_arg_limit_valid(type, limit)`, `gt_macro_arg_limits_valid(type, min, max, charset)`, `gt_macro_arg_encode(charset, value, &len)`
  (octets envoyés pour un `%s`, NULL si un caractère n'est pas encodable), `gt_macro_arg_check(type, value, min, max, charset, &reason)`,
  `gt_macro_arg_describe(info, min, max, charset, default)`, `gt_macro_arg_type_name(type)`.
- **`macro_set.c`** : champs `arg_min`, `arg_max`, `arg_charset` ; `gt_macro_set_set_limits(set, id, min, max, charset, n)` ; copie ; `GtMacroEdit`
  gagne `arg_min`, `arg_max`, `arg_charset` ; `check_args` les abandonne avec `args` ; `gt_macro_expand_segments()` et `gt_macro_expand()` ;
  `gt_macro_check_args(set, macro, args, n, &index, &reason)`.
- **`macros_format.c`** : variantes internes de la mise en forme (plages des arguments, encodage Latin-1 des `%s`) et de l'interprétation des
  échappements (plage d'origine de chaque octet) ; les fonctions publiques actuelles gardent leur contrat.
- **`macro_file.c`** : lecture et écriture des trois clés.
- **`macros.c`** : résolution champ par champ, `gt_macros_get_usage`, `gt_macros_check_args`, refus d'envoi et message.
- **`script_engine.c`** : `"invalid argument"`, nombres Lua entiers. **`tests/stubs/stub_macros.c`** : `gt_macros_check_args`.
- **Interface** : `macro_row.c` (propriétés `arg-min`, `arg-max`, `arg-charset`), `macro_arg_widget.c` (texte indicatif, bulle, vérification
  avec limites), `macro_panel.c` (champ vide = défaut), `macro_editor.c` (table, aperçu), `test_hooks.c` (le vidage donne `placeholder` et
  `tooltip`).

## 4. Tests

- Unitaires : `test_macro_arg` (nouveau : jeux, classes, limites, vérification, encodage, description), `test_macro_set` (limites, copie,
  modifications, segments, égalité avec l'ancien chemin), `test_macro_file` (aller-retour, limite invalide abandonnée avec avertissement, fichier sans
  ces clés), `test_macros_send` (résolution champ vide, refus et message), `test_macro_row` (propriétés), `test_project_file` (aller-retour).
- GUI : `test_macros_limites.py` (nouveau) : éditeur (table, limites, jeu, classe, aperçu et octets), panneau (texte indicatif, rouge hors limites,
  envoi refusé avec message, champ vide = défaut, `latin1` envoie un octet), `.ini` relu. `test_macros_arguments.py` adapté (champ vide après
  « Reset arguments »).
