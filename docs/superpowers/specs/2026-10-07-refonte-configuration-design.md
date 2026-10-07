# Sous-projet 3 — Configuration : un seul stockage GKeyFile, modèle de profil sans GTK

- Date : 2026-10-07
- Branche : `feature/refonte-configuration` (fusion dans `master` selon la règle habituelle : écrasement en commits thématiques, fusion, suppression de la branche, publication sur `Maison`)
- Entrées : revue `docs/superpowers/reviews/2026-10-06-revue-code.md` (point 4 de l'architecture, C5, C6, C7, C13, M9 à M14, remarques reportées du sous-projet 1 tâche 6 et tâches 14/15), sous-projets 0 à 2 fusionnés (socle de tests, `config_file.c`, transport par instance et `GtkTermLink`, suite graphique `tests/gui/`)

## Contexte : la suite du chantier

L'utilisateur prévoit deux évolutions, que ce sous-projet prépare sans les réaliser :
- une notion de **projet** qui applique une configuration, des scripts, et charge le bon fichier de macros tout seul ;
- **plusieurs onglets**, un par port ouvert (avec GTK 4, sous-projet 6).

Conséquences retenues : la configuration devient une **valeur** (`GtProfile`) qu'on copie — une par onglet demain, une seule aujourd'hui — et le fichier est organisé en **groupes préfixés** (`[general]`, `[profile:nom]`, plus tard `[project:nom]`) gérés par un module unique.

## Objectif

1. Un seul mécanisme de persistance : GKeyFile, derrière un module du cœur (`config_store.c`) qui est le **seul** à lire et écrire `.gtktermrc` ; `parsecfg.c` disparaît.
2. Un modèle de profil sans GTK (`profile.c`) : valeurs par défaut, validation qui **renvoie** des avertissements au lieu d'ouvrir des boîtes, conversion vers le transport ; testable sans fenêtre.
3. Le profil **en cours** (avec les options de ligne de commande) est distinct du profil **enregistré** ; seules les actions d'enregistrement explicites écrivent un profil complet.
4. Les anciens fichiers sont migrés sans perte, avec une copie de sauvegarde.
5. Corriger les défauts de configuration de la revue (§6).

## Hors périmètre

- La réécriture des dialogues Port et Terminal (sous-projet 6, GTK 4) : ils sont seulement rebranchés sur le profil en cours.
- Les projets et les onglets eux-mêmes (sous-projets ultérieurs) ; seul le format et l'interface du magasin les prévoient.
- Le fichier séparé des macros et listes (`.ini` de `macros.c`) : format inchangé (sous-projet 4).
- L'état d'application unique (drapeaux écho/CR-LF/horodatage dupliqués dans `interface.c`) : sous-projet 6. Les options de ligne de commande sont inchangées.

## Décisions

1. **Périmètre** (validé par l'utilisateur) : stockage + modèle ; dialogues seulement rebranchés.
2. **Format** (laissé au choix du concepteur par l'utilisateur, orienté par les projets et les onglets) : groupes préfixés, migration automatique avec copie de sauvegarde. Une ancienne version de GTKTerm ne lit plus les profils migrés ; la copie reste.
3. **Approche** : magasin dédié enveloppant GKeyFile plutôt que GKeyFile directement dans `term_config.c` (non testable, écrivains multiples) ou GSettings (schéma à installer, plus de fichier copiable, mal adapté à l'auto-mise à jour).

## 1. Architecture

| Couche | Fichiers | Dépendances | Rôle |
|---|---|---|---|
| Cœur (`gtkterm_core`) | `profile.c/.h` | GLib | `GtProfile`, défauts, validation, copie, conversion en `TransportParams` |
| Cœur | `config_store.c/.h` | GLib | `GtConfigStore` : chargement, migration, profils, réglages globaux, groupes réservés, enregistrement atomique |
| Cœur | `config_file.c/.h` (réduit) | GLib | préparation du chemin (`config_file_prepare`, migration de `~/.gtktermrc` vers `$XDG_CONFIG_HOME`) et `config_wait_char_from_file` ; le reste (retrait/remise des groupes, `remove_section`, accès parsecfg) disparaît |
| Interface | `term_config.c`, `cmdline.c`, `script_panel.c`, `interface.c`, `update.c`, `gtkterm.c` | GTK, VTE | un magasin ouvert au démarrage ; profil en cours ; application au terminal ; dialogues |

Supprimés : `parsecfg.c`, `parsecfg.h`, `tests/test_parsecfg.c`, `cfgStruct cfg[]` et les quelque quarante tableaux globaux de `term_config.c`, `struct configuration_port` et `display_config_t` (remplacés par `GtProfile`), `Copy_configuration`, `config_to_transport_params` (remplacé par `gt_profile_to_transport_params`).

## 2. Modèle (`profile.h`)

```c
typedef struct { gdouble red, green, blue, alpha; } GtColor;

typedef struct {
	/* connexion */
	TransportType transport;
	gchar *port;                 /* série */
	guint baudrate;
	gint bits, stopbits;
	GtParity parity;
	GtFlow flow;
	gint rs485_before_ms, rs485_after_ms;
	gboolean lock_port;          /* faux si --disable-port-lock ; non enregistré */
	gchar *host, *tcp_port;      /* TCP */
	gint wait_delay_ms;          /* envoi de fichier : délai de fin de ligne */
	gint wait_char;              /* 0-255, -1 : aucun */
	/* comportement */
	gboolean echo, crlf_auto, auto_reconnect, esc_clear_screen, timestamp;
	/* terminal */
	gchar *font;
	gboolean block_cursor;
	gint columns, rows;          /* dans ce sens (M14) */
	gint scrollback;
	gboolean visual_bell;        /* conservé ; VTE 2.91 n'a plus de cloche visuelle : non appliqué */
	GtColor foreground, background;
	/* macros */
	gchar *macros_file;          /* NULL : aucun */
} GtProfile;

void      gt_profile_init_defaults(GtProfile *p);
void      gt_profile_copy(GtProfile *dst, const GtProfile *src);
void      gt_profile_clear(GtProfile *p);
/* Corrige les valeurs invalides (bits, bits d'arrêt, délai…) et ajoute à warnings (GPtrArray de chaînes traduites, à libérer) un message par correction ou alerte (débit non standard). Renvoie TRUE si rien à signaler. */
gboolean  gt_profile_validate(GtProfile *p, GPtrArray *warnings);
void      gt_profile_to_transport_params(const GtProfile *p, TransportParams *out);
```

Valeurs par défaut : celles de `Hard_default_configuration` (port `/dev/ttyS0`, 115200 8-N-1, sans contrôle de flux, délais RS485 30 ms, caractère d'attente aucun, police `Monospace 12`, 80 colonnes × 25 lignes, défilement 10 000, curseur bloc, texte noir sur fond blanc, mise à jour vérifiée au démarrage). La cloche visuelle vaut `FALSE` par défaut (fin de l'incohérence M14).

Les textes d'avertissement reprennent les identifiants gettext actuels de `Verify_configuration`.

## 3. Magasin (`config_store.h`)

```c
typedef struct _GtConfigStore GtConfigStore;

GtConfigStore *gt_config_store_load(const gchar *path, GError **error);   /* jamais NULL : en cas d'erreur, magasin vide en lecture seule */
void           gt_config_store_free(GtConfigStore *s);
gboolean       gt_config_store_is_read_only(GtConfigStore *s);
gboolean       gt_config_store_was_migrated(GtConfigStore *s);
const gchar   *gt_config_store_get_path(GtConfigStore *s);

/* profils [profile:nom] ; noms comparés sans tenir compte de la casse, casse d'origine conservée */
gchar        **gt_config_store_list_profiles(GtConfigStore *s);            /* triés, à libérer avec g_strfreev */
gboolean       gt_config_store_has_profile(GtConfigStore *s, const gchar *name);
gboolean       gt_config_store_get_profile(GtConfigStore *s, const gchar *name, GtProfile *out);  /* clés absentes : valeurs par défaut */
void           gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p); /* remplace un profil de même nom à la casse près */
gboolean       gt_config_store_delete_profile(GtConfigStore *s, const gchar *name);
void           gt_config_store_set_profile_string(GtConfigStore *s, const gchar *name, const gchar *key, const gchar *value); /* écriture partielle */

/* réglages globaux [general] */
gchar         *gt_config_store_get_general_string(GtConfigStore *s, const gchar *key, const gchar *fallback);
gboolean       gt_config_store_get_general_boolean(GtConfigStore *s, const gchar *key, gboolean fallback);
void           gt_config_store_set_general_string(GtConfigStore *s, const gchar *key, const gchar *value);
void           gt_config_store_set_general_boolean(GtConfigStore *s, const gchar *key, gboolean value);

/* groupes réservés [window], [colors], [layout], [session] : accès direct pour leurs propriétaires */
GKeyFile      *gt_config_store_get_key_file(GtConfigStore *s);

/* relit le fichier si son identité (st_dev, st_ino, st_size, st_mtim à la nanoseconde) a changé depuis le dernier chargement ou enregistrement ;
   mêmes règles que load ; fichier inchangé : rien ; FALSE (GError, lecture seule) seulement si le fichier est devenu illisible (amendement F1) */
gboolean       gt_config_store_reload_if_changed(GtConfigStore *s, GError **error);

/* enregistrement atomique, fichier en 0600, dossier créé ; refusé (GError) en lecture seule ; copie de sauvegarde avant la première écriture d'un fichier migré */
gboolean       gt_config_store_save(GtConfigStore *s, GError **error);
```

Règles :
- **Chargement** : fichier absent → magasin vide, inscriptible ; fichier vide → idem ; fichier illisible (droits, syntaxe GKeyFile, encodage) → `GError` et magasin vide **en lecture seule** : rien ne sera écrit par-dessus (M12).
- **Groupes inconnus** (ni préfixés, ni réservés, ni anciens profils) : conservés tels quels à l'enregistrement.
- **Erreur d'écriture** : remontée par `GError` avec le chemin du fichier visé (ou de la copie de sauvegarde), jamais seulement celui du fichier temporaire ; l'interface l'affiche (M13).
- **Plusieurs instances** (amendement, relecture finale F1) : le magasin n'est pas un instantané figé. L'identité du fichier est relevée à chaque chargement et enregistrement réussi ; `gt_config_store_reload_if_changed` recharge (migration, lecture seule si illisible, fichier devenu absent → magasin vide inscriptible ; la copie `.avant-migration` n'est jamais réécrite si elle existe) et remplace le contenu en mémoire si elle a changé (autre GTKTerm, modification à la main).
- **Projets à venir** : l'implémentation range les fonctions de profil sur des primitives génériques par préfixe de groupe (`profile:`), réutilisables pour `project:` sans refonte.

## 4. Format et migration

Format écrit :

```
[general]
update_url=https://github.com/Mula-Gabriel/gtkterm.git
update_prefix=/usr/local
update_check_startup=true

[profile:default]
transport=serial            # serial | tcp-client | tcp-server
port=/dev/ttyUSB0
speed=115200
bits=8
stopbits=1
parity=none                 # none | odd | even
flow=none                   # none | xon | rts | rs485
wait_delay=0
wait_char=-1
rs485_rts_time_before_tx=30
rs485_rts_time_after_tx=30
socket_host=
socket_port=
echo=false
crlfauto=false
autoreconnect_enabled=false
esc_clear_screen=false
timestamp=false
font=Monospace 12
term_block_cursor=true
term_columns=80
term_rows=25
term_scrollback=10000
term_visual_bell=false
term_foreground=0;0;0;1
term_background=1;1;1;1
macros_file=/home/…/gtkterm_macros.ini

[window]   [colors]   [layout]   [session]   (contenu inchangé)
```

Nombres et couleurs en locale C (`g_key_file_set_double_list` / `g_ascii_*`).

Migration (en mémoire, au chargement) d'un fichier à l'ancien format :
- un groupe de premier niveau ni réservé (`window`, `colors`, `layout`, `session`, casse ignorée), ni `general`, ni préfixé est un **ancien profil** ; il devient `[profile:nom]` (nom débarrassé des guillemets parsecfg) ;
- valeurs normalisées : guillemets simples ou doubles retirés ; booléens `True`/`False`/`TRUE`/`yes`/`1`… sans tenir compte de la casse ; `wait_char` décodé par `config_wait_char_from_file` ; `transport_type` numérique (0/1/2) → `transport` ; couleurs en huit clés flottantes → deux listes ; `term_rows`/`term_columns`, qui contenaient l'inverse de leur nom (80/25), remis à l'endroit ; `show_rxtx` ignoré ;
- `update_url`, `update_prefix`, `update_check_startup` du profil `default` (sinon du premier profil) passent dans `[general]` et sont retirés des profils ;
- deux anciens profils de même nom à la casse près (fichier abîmé par C6) : le premier est gardé, le second renommé `nom (2)` ;
- `gt_config_store_was_migrated()` est vrai ; avant la première écriture, le fichier d'origine est copié une seule fois en `.gtktermrc.avant-migration` (jamais écrasé s'il existe déjà).

## 5. Flux de l'application

- **Démarrage** : `config_file_prepare` puis `gt_config_store_load` (erreur → boîte d'erreur avec le chemin, poursuite avec les défauts, magasin en lecture seule) ; profil en cours = `default` (ou `-c nom`) ; fichier absent, vide ou sans aucun profil → `[profile:default]` créé avec les défauts et enregistré, message actuel conservé ; options de ligne de commande appliquées au profil en cours seulement ; validation (avertissements réunis dans une seule boîte) ; application au terminal ; ouverture du port.
- **Charger une configuration** : liste = `gt_config_store_list_profiles` ; le profil choisi devient le profil en cours (copie), est validé, appliqué au terminal, le port est rouvert, les raccourcis et le panneau de macros rechargés depuis son `macros_file` ; « profil introuvable » conservé (`-c` inconnu).
- **Enregistrer une configuration** : nom saisi ; confirmation de remplacement si un profil existe déjà (casse ignorée) ; `put_profile` puis `save` ; le nom actif ne change qu'après réussite.
- **Supprimer** : liste, `delete_profile`, `save`.
- **Écritures partielles** (fin de M10) : charger ou enregistrer un fichier de macros écrit seulement `macros_file` du profil actif ; le champ « Update source » du dialogue À propos écrit `update_url` de `[general]`, seulement s'il a changé ; aucune n'écrit les options de ligne de commande.
- **Relire avant d'écrire** (amendement F1) : chaque écriture de l'interface (état de la fenêtre, panneau de scripts, fichier de macros, URL de mise à jour, enregistrer / supprimer une configuration) relit d'abord le fichier s'il a changé, applique son seul changement, puis enregistre ; les listes de profils des dialogues sont relues de même. Deux GTKTerm ouverts ne s'écrasent plus leurs profils.
- **Application au terminal** (`term_config.c`) : police, couleurs (`GtColor` → `GdkRGBA`), taille `vte_terminal_set_size(colonnes, lignes)`, défilement, forme du curseur — séparée du chargement.
- **État de la fenêtre et panneau de scripts** : ils lisent et écrivent leurs groupes par `gt_config_store_get_key_file` puis `gt_config_store_save` ; plus aucun autre code ne charge ou n'écrit `.gtktermrc`.
- **Magasin en lecture seule** : toute tentative d'enregistrement affiche que le fichier de configuration est illisible et que rien n'a été écrit, avec le chemin.

## 6. Défauts et remarques traités

- C5 (chaîne avec espaces rejetée par parsecfg), C6 (profils en double à la casse près), C7 (fichier vide → plantage), C13 (groupes réservés listés comme profils) : disparaissent avec parsecfg et les groupes préfixés.
- M9 (`setlocale` de parsecfg), M10 (options de ligne de commande enregistrées), M11 (`remove_section`), M12 (erreur de lecture ignorée → perte des profils), M13 (dossier absent, échecs silencieux), M14 (lignes/colonnes inversées, cloche visuelle incohérente).
- Remarques reportées du sous-projet 1, tâche 6 : (1) `-c session` puis enregistrement silencieux ; (2) casse renommée à l'enregistrement ; (3) copie temporaire en 0666 & umask ; (4) erreurs citant le chemin temporaire ; (5) anciens profils `[Session]`/`[Window]`/`[Colors]` inaccessibles (ils deviennent des profils `[profile:…]` s'ils ne sont pas les groupes réservés eux-mêmes — documenté) ; (6) échec d'écriture ignoré ; (7) `cfgParse` à -1 non détecté. Tâches 14/15 : autres écrivains de `.gtktermrc` (motif M12) ; fuite de `get_config_file_path`.

## 7. Tests

**Unitaires** (`gtkterm_core`) :
- `test_profile` : défauts ; validation (bits hors 5–8, bits d'arrêt hors 1–2, délai hors 0–500, débit non standard) → corrections et avertissements ; copie profonde ; conversion en `TransportParams`.
- `test_config_store` : fichier absent, vide, illisible (droits, syntaxe) → lecture seule et enregistrement refusé ; migration d'un vrai fichier écrit par parsecfg (fixture : guillemets, `True`/`False`, caractère d'attente négatif, flottants, `transport_type`, lignes/colonnes, groupes réservés conservés, réglages de mise à jour déplacés, doublon de casse) ; aller-retour sans perte ; groupes inconnus conservés ; casse des noms (liste, remplacement, suppression) ; écriture partielle qui ne touche pas le reste ; enregistrement atomique en 0600 ; dossier créé ; copie de sauvegarde unique.
- `test_config_file` adapté (préparation du chemin, caractère d'attente) ; `test_parsecfg` supprimé.

**Suite graphique** (`tests/gui/`, nouveaux scénarios) :
- migration au démarrage d'un `.gtktermrc` ancien format : port, police et fichier de macros conservés, copie de sauvegarde présente ;
- enregistrer une configuration sous un nouveau nom, la recharger, la supprimer, par les dialogues ;
- `--port` donné en ligne de commande puis chargement d'un fichier de macros : le profil enregistré garde son port.

## 8. Critères d'acceptation

1. `meson test -C build` et `meson test -C build-asan` : aucun échec ; `meson test -C build --suite gui` : aucun échec.
2. `parsecfg` n'existe plus dans le dépôt (sources, en-têtes, meson, POTFILES).
3. `config_store.c` est le seul fichier qui lit ou écrit le contenu de `.gtktermrc` (vérifié par `grep` de `g_key_file_load_from_file`, `g_key_file_save_to_file` et `g_file_set_contents` dans `src/`), à une exception près : le déplacement unique de l'ancien `$HOME/.gtktermrc` vers `$XDG_CONFIG_HOME` par `config_file_prepare` (`config_file.c`), dont la copie de repli est écrite en 0600 ; `profile.c` et `config_store.c` n'incluent aucun en-tête GTK, VTE ou d'interface.
4. Options de ligne de commande inchangées ; un ancien `.gtktermrc` est lu sans perte de profil.
