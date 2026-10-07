# Configuration : un seul stockage GKeyFile, modèle de profil sans GTK — plan d'implémentation (sous-projet 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** remplacer parsecfg par un magasin GKeyFile unique (`GtConfigStore`), introduire un modèle de profil sans GTK (`GtProfile`) qui remplace `config` et `term_conf`, migrer les anciens `.gtktermrc` sans perte, et séparer le profil en cours (options de ligne de commande comprises) du profil enregistré.

**Architecture :** deux modules purs dans `gtkterm_core` — `profile.c` (valeurs par défaut, validation qui renvoie des avertissements, conversion vers le transport, noms des enums) et `config_store.c` (chargement, migration de l'ancien format, profils `[profile:nom]`, réglages `[general]`, groupes réservés, enregistrement atomique 0600). L'interface (`term_config.c`) tient un magasin ouvert au démarrage, le profil en cours `GtProfile config` et les réglages globaux `GtkTermSettings settings` ; c'est le seul code qui lit ou écrit `.gtktermrc`, par le magasin. `parsecfg.c` disparaît.

**Tech Stack :** C (gnu11), GLib ≥ 2.70 (GKeyFile, `g_file_set_contents_full`), GTK 3 + VTE 2.91 (interface seulement), Meson ≥ 1.0, GLib GTest (TAP), suite graphique `tests/gui/` (Python + AT-SPI + broadway isolé).

**Spec :** `docs/superpowers/specs/2026-10-07-refonte-configuration-design.md`

## Global Constraints

- `profile.c` et `config_store.c` sont dans `gtkterm_core` et n'incluent aucun en-tête GTK, VTE ou d'interface.
- À la fin, `config_store.c` est le seul fichier de `src/` qui charge ou écrit `.gtktermrc` (`g_key_file_load_from_file`, `g_key_file_save_to_file`, `g_file_set_contents*` sur ce chemin) ; `parsecfg` n'existe plus (sources, en-têtes, meson, POTFILES, `tests/lsan.supp`).
- Options de ligne de commande inchangées (noms, sémantique) ; un ancien `.gtktermrc` est lu sans perte de profil.
- Format écrit : groupes `[general]`, `[profile:nom]`, groupes réservés `[window]`, `[colors]`, `[layout]`, `[session]` inchangés ; clés et valeurs exactement celles du §4 de la spec ; nombres en locale C.
- Chaînes d'interface : identifiants gettext en anglais ; les messages de validation reprennent **à l'identique** les identifiants actuels de `Verify_configuration` ; traductions françaises ajoutées dans `po/fr.po` (tâche 6).
- Style : celui du fichier modifié ; nouveaux fichiers C dans le style dominant (tabulations, Allman, `if(`, type de retour et nom sur une ligne) ; lignes ≤ 200 colonnes ; commentaires en français ; messages des tests en ASCII.
- Tests : jamais la vraie configuration de l'utilisateur ni de vrai port série ; fichiers temporaires dans un répertoire créé par `g_dir_make_tmp` ; la suite graphique ne tourne que par `meson test -C build --suite gui` (broadway isolé), jamais sur l'écran de l'utilisateur.
- Chaque commit en français, terminé par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branche `feature/refonte-configuration`.
- Construction : `ninja -C build` ; tests : `meson test -C build --print-errorlogs` ; ASan : `meson test -C build-asan --print-errorlogs` ; interface : `meson test -C build --suite gui --print-errorlogs`.

## Décisions prises par ce plan

1. **Réglages globaux** : `update_url`, `update_prefix`, `update_check_startup` vivent dans `[general]` ; à l'exécution, dans `GtkTermSettings settings` (`term_config.h`).
2. **Profil en cours** : la variable globale garde son nom `config`, de type `GtProfile` (les champs changent de nom, table de la tâche 4) ; `term_conf` disparaît dans `config`.
3. **Tâche 4 provisoire** : le passage à `GtProfile` se fait d'abord avec la persistance parsecfg existante (champs renommés), pour garder l'application fonctionnelle entre deux tâches ; la tâche 5 remplace la persistance.
4. **Nom actif** : nom du profil chargé (`default` par défaut) ; « Enregistrer » le remplace par le nom saisi après réussite seulement ; les écritures partielles (fichier de macros) visent le nom actif.
5. **Ancien profil nommé `general`** : le groupe `[general]` est traité comme un ancien profil s'il contient `port`, `speed` ou `transport_type`.
6. **Couleur du texte par défaut** : `0;0;0;1` (l'ancien défaut avait une opacité 0, ignorée par VTE).
7. **Lecture tolérante** : les booléens sont lus sans tenir compte de la casse (`true/false/yes/no/on/off/1/0`), même hors migration (fichier modifié à la main) ; `wait_char` passe toujours par `config_wait_char_from_file`.

## Review Focus

1. **Fichier de configuration modifié à la main ou abîmé** (clé numérique non numérique, liste de couleurs incomplète, groupe en double) : les valeurs fautives retombent sur le défaut, sans plantage ni perte du reste. Épinglé par `test_config_store` (`/config_store/valeurs-fautives-ignorees`).
2. **Magasin en lecture seule** (fichier illisible au démarrage) : aucune écriture, ni par la configuration, ni par l'état de la fenêtre, ni par le panneau de scripts ; un message une seule fois au démarrage. Épinglé par `test_config_store` (`/config_store/illisible-lecture-seule`) et la revue des appelants (tâche 5).
3. **Profil supprimé alors qu'il est actif**, puis écriture partielle (chargement d'un fichier de macros) : aucun profil fantôme — `config_remember_macros_file` ne met à jour que `config.macros_file` en mémoire et n'écrit rien quand le profil actif n'existe plus dans le magasin (il n'écrit pas non plus quand le fichier de macros n'a pas changé) ; pas de plantage. Le magasin, lui, crée le profil sur `set_profile_string` (`test_config_store`, `/config_store/ecriture-partielle-profil-absent`) : c'est l'appelant qui l'évite (tâche 5, correctif).
4. **Noms non ASCII et casse** (`Atelier`/`atelier`, `Équipe`/`équipe`) : un seul profil, casse d'origine conservée. Épinglé par `test_config_store` (`/config_store/casse-des-noms`).
5. **Migration d'un fichier déjà partiellement réécrit par GKeyFile** (clés `cle=valeur` sans tabulation, mélange de styles) : migré comme un fichier parsecfg pur. Épinglé par `test_config_store` (`/config_store/migration-style-mixte`).

## Carte des fichiers

| Fichier | Tâche | Rôle |
|---|---|---|
| `src/profile.c/.h` (créés) | 1 | `GtProfile`, `GtColor`, défauts, copie, validation, conversion, noms des enums |
| `src/config_store.c/.h` (créés) | 2, 3 | `GtConfigStore` ; migration (tâche 3) |
| `src/term_config.c/.h`, `src/cmdline.c`, `src/files.c`, `src/interface.c`, `src/update.c`, `src/script_panel.c`, `src/macro_panel.c`, `src/gtkterm.c`, `tests/stubs/stub_ui.c` | 4, 5 | passage à `GtProfile` puis au magasin |
| `src/parsecfg.c/.h`, `tests/test_parsecfg.c`, `tests/lsan.supp` | 5 (supprimés) | — |
| `src/config_file.c/.h`, `tests/test_config_file.c` | 5 (réduits) | préparation du chemin, caractère d'attente |
| `tests/test_profile.c`, `tests/test_config_store.c` (créés) | 1-3 | tests unitaires |
| `tests/gui/test_config_migration.py`, `test_config_profils.py`, `test_config_ligne_commande.py` (créés) | 6 | suite graphique |
| `po/POTFILES.in`, `po/fr.po`, `CLAUDE.md`, `tests/README.md`, revue | 5, 6 | documentation, traductions |

---

### Task 1 : modèle de profil (`GtProfile`)

**Files :**
- Create : `src/profile.h`, `src/profile.c`, `tests/test_profile.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : `TransportType`, `GtParity`, `GtFlow`, `TransportParams`, `find_standard_baudrate` (`transport.h`).
- Produces :
  ```c
  typedef struct { gdouble red, green, blue, alpha; } GtColor;
  typedef struct { TransportType transport; gchar *port; guint baudrate; gint bits; gint stopbits; GtParity parity; GtFlow flow;
                   gint rs485_before_ms; gint rs485_after_ms; gboolean lock_port; gchar *host; gchar *tcp_port; gint wait_delay_ms; gint wait_char;
                   gboolean echo; gboolean crlf_auto; gboolean auto_reconnect; gboolean esc_clear_screen; gboolean timestamp;
                   gchar *font; gboolean block_cursor; gint columns; gint rows; gint scrollback; gboolean visual_bell; GtColor foreground; GtColor background;
                   gchar *macros_file; } GtProfile;
  void gt_profile_init_defaults(GtProfile *p);
  void gt_profile_copy(GtProfile *dst, const GtProfile *src);
  void gt_profile_clear(GtProfile *p);
  void gt_profile_set_string(gchar **field, const gchar *value);
  gboolean gt_profile_validate(GtProfile *p, GPtrArray *warnings);
  void gt_profile_to_transport_params(const GtProfile *p, TransportParams *out);
  const gchar *gt_parity_to_string(GtParity v);   gboolean gt_parity_from_string(const gchar *s, GtParity *out);
  const gchar *gt_flow_to_string(GtFlow v);       gboolean gt_flow_from_string(const gchar *s, GtFlow *out);
  const gchar *gt_transport_to_string(TransportType v); gboolean gt_transport_from_string(const gchar *s, TransportType *out);
  #define GT_PROFILE_DEFAULT_PORT "/dev/ttyS0"
  #define GT_PROFILE_DEFAULT_FONT "Monospace 12"
  ```

- [ ] **Step 1 : écrire le test**

`tests/test_profile.c` :

```c
/* Tests unitaires du modèle de profil (profile.c) : défauts, copie, validation, conversion, noms des enums. */

#include <glib.h>
#include <string.h>

#include "profile.h"

static void
test_defauts(void)
{
	GtProfile p;

	gt_profile_init_defaults(&p);
	g_assert_cmpint(p.transport, ==, TRANSPORT_SERIAL);
	g_assert_cmpstr(p.port, ==, "/dev/ttyS0");
	g_assert_cmpuint(p.baudrate, ==, 115200);
	g_assert_cmpint(p.bits, ==, 8);
	g_assert_cmpint(p.stopbits, ==, 1);
	g_assert_cmpint(p.parity, ==, GT_PARITY_NONE);
	g_assert_cmpint(p.flow, ==, GT_FLOW_NONE);
	g_assert_cmpint(p.rs485_before_ms, ==, 30);
	g_assert_cmpint(p.rs485_after_ms, ==, 30);
	g_assert_true(p.lock_port);
	g_assert_cmpstr(p.host, ==, "");
	g_assert_cmpstr(p.tcp_port, ==, "");
	g_assert_cmpint(p.wait_delay_ms, ==, 0);
	g_assert_cmpint(p.wait_char, ==, -1);
	g_assert_false(p.echo || p.crlf_auto || p.auto_reconnect || p.esc_clear_screen || p.timestamp);
	g_assert_cmpstr(p.font, ==, "Monospace 12");
	g_assert_true(p.block_cursor);
	g_assert_cmpint(p.columns, ==, 80);
	g_assert_cmpint(p.rows, ==, 25);
	g_assert_cmpint(p.scrollback, ==, 10000);
	g_assert_false(p.visual_bell);
	g_assert_cmpfloat(p.foreground.red, ==, 0.0);
	g_assert_cmpfloat(p.foreground.alpha, ==, 1.0);
	g_assert_cmpfloat(p.background.red, ==, 1.0);
	g_assert_cmpfloat(p.background.alpha, ==, 1.0);
	g_assert_null(p.macros_file);
	gt_profile_clear(&p);
}

static void
test_copie_profonde(void)
{
	GtProfile a, b;

	gt_profile_init_defaults(&a);
	gt_profile_init_defaults(&b);
	gt_profile_set_string(&a.port, "/dev/ttyUSB3");
	gt_profile_set_string(&a.macros_file, "/tmp/m.ini");
	a.baudrate = 9600;
	gt_profile_copy(&b, &a);
	g_assert_cmpstr(b.port, ==, "/dev/ttyUSB3");
	g_assert_cmpstr(b.macros_file, ==, "/tmp/m.ini");
	g_assert_cmpuint(b.baudrate, ==, 9600);
	g_assert_true(b.port != a.port);
	gt_profile_set_string(&a.port, "/dev/autre");
	g_assert_cmpstr(b.port, ==, "/dev/ttyUSB3");
	gt_profile_copy(&b, &b);
	g_assert_cmpstr(b.port, ==, "/dev/ttyUSB3");
	gt_profile_clear(&a);
	gt_profile_clear(&b);
	g_assert_null(b.port);
}

static void
test_profil_a_zero_est_vide(void)
{
	GtProfile p;

	memset(&p, 0, sizeof(p));
	gt_profile_clear(&p);
	gt_profile_init_defaults(&p);
	gt_profile_clear(&p);
}

static void
test_validation_corrige(void)
{
	GtProfile p;
	GPtrArray *avert = g_ptr_array_new_with_free_func(g_free);

	gt_profile_init_defaults(&p);
	g_assert_true(gt_profile_validate(&p, avert));
	g_assert_cmpuint(avert->len, ==, 0);

	p.bits = 9;
	p.stopbits = 3;
	p.wait_delay_ms = 600;
	g_free(p.font);
	p.font = NULL;
	p.columns = 0;
	p.rows = -1;
	p.scrollback = 0;
	p.wait_char = 300;
	g_assert_false(gt_profile_validate(&p, avert));
	g_assert_cmpuint(avert->len, ==, 3);
	g_assert_cmpint(p.bits, ==, 8);
	g_assert_cmpint(p.stopbits, ==, 1);
	g_assert_cmpint(p.wait_delay_ms, ==, 0);
	g_assert_cmpstr(p.font, ==, "Monospace 12");
	g_assert_cmpint(p.columns, ==, 80);
	g_assert_cmpint(p.rows, ==, 25);
	g_assert_cmpint(p.scrollback, ==, 10000);
	g_assert_cmpint(p.wait_char, ==, -1);
	g_assert_nonnull(strstr(g_ptr_array_index(avert, 0), "stop-bits: 3"));
	g_assert_nonnull(strstr(g_ptr_array_index(avert, 1), "bits: 9"));
	gt_profile_clear(&p);
	g_ptr_array_unref(avert);
}

static void
test_validation_debit_non_standard(void)
{
	GtProfile p;
	GPtrArray *avert = g_ptr_array_new_with_free_func(g_free);

	gt_profile_init_defaults(&p);
	p.baudrate = 123457;
	g_assert_false(gt_profile_validate(&p, avert));
	g_assert_cmpuint(avert->len, ==, 1);
	g_assert_nonnull(strstr(g_ptr_array_index(avert, 0), "123457"));
	g_assert_cmpuint(p.baudrate, ==, 123457);	/* signalé, pas corrigé */
	gt_profile_clear(&p);
	g_ptr_array_unref(avert);
}

static void
test_conversion_transport(void)
{
	GtProfile p;
	TransportParams tp;

	gt_profile_init_defaults(&p);
	p.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&p.port, "/dev/ttyACM1");
	gt_profile_set_string(&p.host, "10.0.0.2");
	gt_profile_set_string(&p.tcp_port, "2000");
	p.baudrate = 57600;
	p.bits = 7;
	p.stopbits = 2;
	p.parity = GT_PARITY_EVEN;
	p.flow = GT_FLOW_RS485;
	p.rs485_before_ms = 5;
	p.rs485_after_ms = 6;
	p.lock_port = FALSE;
	gt_profile_to_transport_params(&p, &tp);
	g_assert_cmpint(tp.type, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(tp.port, ==, "/dev/ttyACM1");
	g_assert_true(tp.port != p.port);
	g_assert_cmpstr(tp.host, ==, "10.0.0.2");
	g_assert_cmpstr(tp.tcp_port, ==, "2000");
	g_assert_cmpuint(tp.baudrate, ==, 57600);
	g_assert_cmpint(tp.bits, ==, 7);
	g_assert_cmpint(tp.stopbits, ==, 2);
	g_assert_cmpint(tp.parity, ==, GT_PARITY_EVEN);
	g_assert_cmpint(tp.flow, ==, GT_FLOW_RS485);
	g_assert_cmpint(tp.rs485_before_ms, ==, 5);
	g_assert_cmpint(tp.rs485_after_ms, ==, 6);
	g_assert_false(tp.lock_port);
	transport_params_clear(&tp);
	gt_profile_clear(&p);
}

static void
test_noms_des_enums(void)
{
	GtParity par = GT_PARITY_NONE;
	GtFlow flux = GT_FLOW_NONE;
	TransportType tr = TRANSPORT_SERIAL;

	g_assert_cmpstr(gt_parity_to_string(GT_PARITY_ODD), ==, "odd");
	g_assert_cmpstr(gt_flow_to_string(GT_FLOW_XONXOFF), ==, "xon");
	g_assert_cmpstr(gt_flow_to_string(GT_FLOW_RTSCTS), ==, "rts");
	g_assert_cmpstr(gt_flow_to_string(GT_FLOW_RS485), ==, "rs485");
	g_assert_cmpstr(gt_transport_to_string(TRANSPORT_TCP_SERVER), ==, "tcp-server");
	g_assert_true(gt_parity_from_string("EVEN", &par));
	g_assert_cmpint(par, ==, GT_PARITY_EVEN);
	g_assert_true(gt_flow_from_string("Rs485", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_RS485);
	g_assert_true(gt_transport_from_string("tcp-client", &tr));
	g_assert_cmpint(tr, ==, TRANSPORT_TCP_CLIENT);
	g_assert_false(gt_parity_from_string("mark", &par));
	g_assert_cmpint(par, ==, GT_PARITY_EVEN);	/* inchangé */
	g_assert_false(gt_flow_from_string(NULL, &flux));
	g_assert_false(gt_transport_from_string("udp", &tr));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/profile/defauts", test_defauts);
	g_test_add_func("/profile/copie-profonde", test_copie_profonde);
	g_test_add_func("/profile/profil-a-zero-est-vide", test_profil_a_zero_est_vide);
	g_test_add_func("/profile/validation-corrige", test_validation_corrige);
	g_test_add_func("/profile/validation-debit-non-standard", test_validation_debit_non_standard);
	g_test_add_func("/profile/conversion-transport", test_conversion_transport);
	g_test_add_func("/profile/noms-des-enums", test_noms_des_enums);
	return g_test_run();
}
```

`tests/meson.build`, `unit_tests` : ajouter `'test_profile' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | grep -m3 error` — Expected : `profile.h: No such file or directory`.

- [ ] **Step 3 : écrire l'implémentation**

`src/profile.h` :

```c
/* Profil de connexion et d'affichage (sous-projet 3) : une valeur sans GTK, copiée par le profil en cours (et demain par onglet).
 * Une structure mise à zéro est un profil vide valide (gt_profile_clear l'accepte). */
#ifndef PROFILE_H_
#define PROFILE_H_

#include <glib.h>

#include "transport.h"

#define GT_PROFILE_DEFAULT_PORT "/dev/ttyS0"
#define GT_PROFILE_DEFAULT_FONT "Monospace 12"

typedef struct
{
	gdouble red, green, blue, alpha;
} GtColor;

typedef struct
{
	/* connexion */
	TransportType transport;
	gchar *port;			/* série */
	guint baudrate;
	gint bits;			/* 5 à 8 */
	gint stopbits;			/* 1 ou 2 */
	GtParity parity;
	GtFlow flow;
	gint rs485_before_ms;
	gint rs485_after_ms;
	gboolean lock_port;		/* faux avec --disable-port-lock ; jamais enregistré */
	gchar *host;			/* TCP */
	gchar *tcp_port;
	gint wait_delay_ms;		/* envoi de fichier : délai après chaque fin de ligne */
	gint wait_char;			/* envoi de fichier : caractère attendu 0-255, -1 : aucun */
	/* comportement */
	gboolean echo;
	gboolean crlf_auto;
	gboolean auto_reconnect;
	gboolean esc_clear_screen;
	gboolean timestamp;
	/* terminal */
	gchar *font;
	gboolean block_cursor;
	gint columns;
	gint rows;
	gint scrollback;
	gboolean visual_bell;		/* conservé ; VTE 2.91 n'a plus de cloche visuelle */
	GtColor foreground;
	GtColor background;
	/* macros */
	gchar *macros_file;		/* NULL : aucun */
} GtProfile;

/* p non initialisé ou vidé par gt_profile_clear */
void         gt_profile_init_defaults(GtProfile *p);
/* dst initialisé (ou mis à zéro) : vidé puis copie profonde de src */
void         gt_profile_copy(GtProfile *dst, const GtProfile *src);
void         gt_profile_clear(GtProfile *p);
void         gt_profile_set_string(gchar **field, const gchar *value);
/* Corrige les valeurs invalides et ajoute à warnings (chaînes traduites, libérées par le tableau) un message par correction
 * ou alerte. Renvoie TRUE si aucun message n'a été ajouté. */
gboolean     gt_profile_validate(GtProfile *p, GPtrArray *warnings);
/* À libérer avec transport_params_clear */
void         gt_profile_to_transport_params(const GtProfile *p, TransportParams *out);

/* Noms écrits dans .gtktermrc ; lecture sans tenir compte de la casse, out inchangé si le nom est inconnu */
const gchar *gt_parity_to_string(GtParity v);
gboolean     gt_parity_from_string(const gchar *s, GtParity *out);
const gchar *gt_flow_to_string(GtFlow v);
gboolean     gt_flow_from_string(const gchar *s, GtFlow *out);
const gchar *gt_transport_to_string(TransportType v);
gboolean     gt_transport_from_string(const gchar *s, TransportType *out);

#endif
```

`src/profile.c` :

```c
/***********************************************************************/
/* profile.c                                                           */
/*                                                                     */
/*   Purpose                                                           */
/*      Profil de connexion et d'affichage, sans GTK (sous-projet 3)   */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include <config.h>
#include <glib/gi18n.h>

#include "profile.h"

static const gchar * const parity_names[] = { "none", "odd", "even" };
static const gchar * const flow_names[] = { "none", "xon", "rts", "rs485" };
static const gchar * const transport_names[] = { "serial", "tcp-client", "tcp-server" };

void gt_profile_init_defaults(GtProfile *p)
{
	memset(p, 0, sizeof(*p));
	p->transport = TRANSPORT_SERIAL;
	p->port = g_strdup(GT_PROFILE_DEFAULT_PORT);
	p->baudrate = 115200;
	p->bits = 8;
	p->stopbits = 1;
	p->parity = GT_PARITY_NONE;
	p->flow = GT_FLOW_NONE;
	p->rs485_before_ms = 30;
	p->rs485_after_ms = 30;
	p->lock_port = TRUE;
	p->host = g_strdup("");
	p->tcp_port = g_strdup("");
	p->wait_delay_ms = 0;
	p->wait_char = -1;
	p->font = g_strdup(GT_PROFILE_DEFAULT_FONT);
	p->block_cursor = TRUE;
	p->columns = 80;
	p->rows = 25;
	p->scrollback = 10000;
	p->visual_bell = FALSE;
	p->foreground = (GtColor){ 0.0, 0.0, 0.0, 1.0 };
	p->background = (GtColor){ 1.0, 1.0, 1.0, 1.0 };
	p->macros_file = NULL;
}

void gt_profile_clear(GtProfile *p)
{
	g_clear_pointer(&p->port, g_free);
	g_clear_pointer(&p->host, g_free);
	g_clear_pointer(&p->tcp_port, g_free);
	g_clear_pointer(&p->font, g_free);
	g_clear_pointer(&p->macros_file, g_free);
}

void gt_profile_copy(GtProfile *dst, const GtProfile *src)
{
	if(dst == src)
		return;
	gt_profile_clear(dst);
	*dst = *src;
	dst->port = g_strdup(src->port);
	dst->host = g_strdup(src->host);
	dst->tcp_port = g_strdup(src->tcp_port);
	dst->font = g_strdup(src->font);
	dst->macros_file = g_strdup(src->macros_file);
}

void gt_profile_set_string(gchar **field, const gchar *value)
{
	gchar *copy = g_strdup(value);

	g_free(*field);
	*field = copy;
}

gboolean gt_profile_validate(GtProfile *p, GPtrArray *warnings)
{
	guint before = warnings->len;

	if(find_standard_baudrate(p->baudrate) == B0)
		g_ptr_array_add(warnings, g_strdup_printf(_("Baud rate %u may not be supported by all hardware"), p->baudrate));

	if(p->stopbits != 1 && p->stopbits != 2)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid number of stop-bits: %d\nFalling back to default number of stop-bits number: %d\n"),
		                                          p->stopbits, 1));
		p->stopbits = 1;
	}

	if(p->bits < 5 || p->bits > 8)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid number of bits: %d\nFalling back to default number of bits: %d\n"), p->bits, 8));
		p->bits = 8;
	}

	if(p->wait_delay_ms < 0 || p->wait_delay_ms > 500)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid delay: %d ms\nFalling back to default delay: %d ms\n"), p->wait_delay_ms, 0));
		p->wait_delay_ms = 0;
	}

	/* Corrections silencieuses : valeurs absurdes d'un fichier modifié à la main */
	if(p->font == NULL || p->font[0] == '\0')
		gt_profile_set_string(&p->font, GT_PROFILE_DEFAULT_FONT);
	if(p->columns <= 0)
		p->columns = 80;
	if(p->rows <= 0)
		p->rows = 25;
	if(p->scrollback <= 0)
		p->scrollback = 10000;
	if(p->wait_char < -1 || p->wait_char > 255)
		p->wait_char = -1;
	if(p->port == NULL)
		gt_profile_set_string(&p->port, GT_PROFILE_DEFAULT_PORT);
	if(p->host == NULL)
		gt_profile_set_string(&p->host, "");
	if(p->tcp_port == NULL)
		gt_profile_set_string(&p->tcp_port, "");

	return warnings->len == before;
}

void gt_profile_to_transport_params(const GtProfile *p, TransportParams *out)
{
	memset(out, 0, sizeof(*out));
	out->type = p->transport;
	out->port = g_strdup(p->port);
	out->baudrate = p->baudrate;
	out->bits = p->bits;
	out->stopbits = p->stopbits;
	out->parity = p->parity;
	out->flow = p->flow;
	out->rs485_before_ms = p->rs485_before_ms;
	out->rs485_after_ms = p->rs485_after_ms;
	out->lock_port = p->lock_port;
	out->host = g_strdup(p->host);
	out->tcp_port = g_strdup(p->tcp_port);
}

static gint name_index(const gchar * const *names, gint count, const gchar *s)
{
	if(s == NULL)
		return -1;
	for(gint i = 0; i < count; i++)
		if(g_ascii_strcasecmp(names[i], s) == 0)
			return i;
	return -1;
}

const gchar *gt_parity_to_string(GtParity v)
{
	return (v >= GT_PARITY_NONE && v <= GT_PARITY_EVEN) ? parity_names[v] : parity_names[0];
}

gboolean gt_parity_from_string(const gchar *s, GtParity *out)
{
	gint i = name_index(parity_names, G_N_ELEMENTS(parity_names), s);

	if(i < 0)
		return FALSE;
	*out = (GtParity)i;
	return TRUE;
}

const gchar *gt_flow_to_string(GtFlow v)
{
	return (v >= GT_FLOW_NONE && v <= GT_FLOW_RS485) ? flow_names[v] : flow_names[0];
}

gboolean gt_flow_from_string(const gchar *s, GtFlow *out)
{
	gint i = name_index(flow_names, G_N_ELEMENTS(flow_names), s);

	if(i < 0)
		return FALSE;
	*out = (GtFlow)i;
	return TRUE;
}

const gchar *gt_transport_to_string(TransportType v)
{
	return (v >= TRANSPORT_SERIAL && v <= TRANSPORT_TCP_SERVER) ? transport_names[v] : transport_names[0];
}

gboolean gt_transport_from_string(const gchar *s, TransportType *out)
{
	gint i = name_index(transport_names, G_N_ELEMENTS(transport_names), s);

	if(i < 0)
		return FALSE;
	*out = (TransportType)i;
	return TRUE;
}
```

`src/meson.build`, `core_sources` : ajouter `'profile.c',` et `'profile.h',`.

- [ ] **Step 4 : constater la réussite**

Run : `ninja -C build && meson test -C build test_profile --print-errorlogs` — Expected : OK (7 cas). Si `/profile/validation-debit-non-standard` échoue parce que 123457 serait standard, c'est un défaut du test : choisir un débit absent de `src/baudrates.sh` et le dire dans le rapport.

- [ ] **Step 5 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/profile.c src/profile.h src/meson.build tests/test_profile.c tests/meson.build
git commit -m "cœur : modèle de profil sans GTK (défauts, validation, conversion vers le transport)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2 : magasin de configuration (`GtConfigStore`), sans migration

**Files :**
- Create : `src/config_store.h`, `src/config_store.c`, `tests/test_config_store.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : `GtProfile` et ses fonctions (tâche 1), `config_wait_char_from_file` (`config_file.h`).
- Produces (spec §3 ; utilisés par les tâches 3 et 5) :
  ```c
  typedef struct _GtConfigStore GtConfigStore;
  GtConfigStore *gt_config_store_load(const gchar *path, GError **error);
  void           gt_config_store_free(GtConfigStore *s);
  gboolean       gt_config_store_is_read_only(GtConfigStore *s);
  gboolean       gt_config_store_was_migrated(GtConfigStore *s);
  const gchar   *gt_config_store_get_path(GtConfigStore *s);
  gchar        **gt_config_store_list_profiles(GtConfigStore *s);
  gboolean       gt_config_store_has_profile(GtConfigStore *s, const gchar *name);
  gboolean       gt_config_store_get_profile(GtConfigStore *s, const gchar *name, GtProfile *out);
  void           gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p);
  gboolean       gt_config_store_delete_profile(GtConfigStore *s, const gchar *name);
  void           gt_config_store_set_profile_string(GtConfigStore *s, const gchar *name, const gchar *key, const gchar *value);
  gchar         *gt_config_store_get_general_string(GtConfigStore *s, const gchar *key, const gchar *fallback);
  gboolean       gt_config_store_get_general_boolean(GtConfigStore *s, const gchar *key, gboolean fallback);
  void           gt_config_store_set_general_string(GtConfigStore *s, const gchar *key, const gchar *value);
  void           gt_config_store_set_general_boolean(GtConfigStore *s, const gchar *key, gboolean value);
  GKeyFile      *gt_config_store_get_key_file(GtConfigStore *s);
  gboolean       gt_config_store_save(GtConfigStore *s, GError **error);
  /* utilitaire partagé avec la migration (tâche 3) */
  gboolean       gt_config_parse_boolean(const gchar *s, gboolean *out);
  ```

Comportement : spec §3 et §4. `get_profile` vide `out` puis le remplit des défauts, puis lit chaque clé présente ; une valeur illisible garde le défaut. `put_profile` réutilise le groupe existant de même nom à la casse près (casse d'origine conservée), écrit toutes les clés du §4 (et retire `macros_file` si `NULL`), garde les clés inconnues. Comparaison des noms : `g_utf8_casefold` des deux côtés.

- [ ] **Step 1 : écrire le test**

`tests/test_config_store.c` :

```c
/* Tests unitaires du magasin de configuration (config_store.c) : chargement, profils, réglages globaux, enregistrement. */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config_store.h"

static gchar *dossier;

static gchar *
chemin(const gchar *nom)
{
	return g_build_filename(dossier, nom, NULL);
}

static gchar *
ecrire(const gchar *nom, const gchar *contenu)
{
	gchar *p = chemin(nom);

	g_assert_true(g_file_set_contents(p, contenu, -1, NULL));
	return p;
}

static gchar *
lire(const gchar *p)
{
	gchar *c = NULL;

	g_assert_true(g_file_get_contents(p, &c, NULL, NULL));
	return c;
}

static GtConfigStore *
charger(const gchar *p)
{
	GError *err = NULL;
	GtConfigStore *s = gt_config_store_load(p, &err);

	g_assert_no_error(err);
	g_assert_nonnull(s);
	return s;
}

static void
enregistrer(GtConfigStore *s)
{
	GError *err = NULL;

	g_assert_true(gt_config_store_save(s, &err));
	g_assert_no_error(err);
}

static void
test_fichier_absent(void)
{
	gchar *p = chemin("absent.rc");
	GtConfigStore *s = charger(p);
	gchar **noms = gt_config_store_list_profiles(s);
	GtProfile prof = { 0 };

	g_assert_false(gt_config_store_is_read_only(s));
	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_cmpuint(g_strv_length(noms), ==, 0);
	g_assert_false(gt_config_store_has_profile(s, "default"));
	g_assert_false(gt_config_store_get_profile(s, "default", &prof));
	g_assert_cmpstr(gt_config_store_get_path(s), ==, p);
	g_strfreev(noms);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_fichier_vide(void)
{
	gchar *p = ecrire("vide.rc", "");
	GtConfigStore *s = charger(p);
	gchar **noms = gt_config_store_list_profiles(s);

	g_assert_false(gt_config_store_is_read_only(s));
	g_assert_cmpuint(g_strv_length(noms), ==, 0);
	g_strfreev(noms);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_illisible_lecture_seule(void)
{
	gchar *p = ecrire("syntaxe.rc", "ceci n'est pas un fichier ini\n[profile:a]\nport=/dev/x\n");
	gchar *avant = lire(p);
	GError *err = NULL;
	GtConfigStore *s = gt_config_store_load(p, &err);
	gchar *apres;

	g_assert_nonnull(err);
	g_clear_error(&err);
	g_assert_nonnull(s);
	g_assert_true(gt_config_store_is_read_only(s));
	g_assert_false(gt_config_store_has_profile(s, "a"));
	g_assert_false(gt_config_store_save(s, &err));
	g_assert_nonnull(err);
	g_assert_nonnull(strstr(err->message, p));
	g_clear_error(&err);
	apres = lire(p);
	g_assert_cmpstr(apres, ==, avant);
	gt_config_store_free(s);
	g_free(avant);
	g_free(apres);
	g_free(p);

	if(geteuid() != 0)
	{
		p = ecrire("droits.rc", "[profile:a]\nport=/dev/x\n");
		g_assert_cmpint(g_chmod(p, 0), ==, 0);
		s = gt_config_store_load(p, &err);
		g_assert_nonnull(err);
		g_clear_error(&err);
		g_assert_true(gt_config_store_is_read_only(s));
		g_assert_false(gt_config_store_save(s, NULL));
		gt_config_store_free(s);
		g_chmod(p, 0600);
		g_free(p);
	}
}

static void
profil_complet(GtProfile *p)
{
	gt_profile_init_defaults(p);
	p->transport = TRANSPORT_TCP_SERVER;
	gt_profile_set_string(&p->port, "/dev/ttyUSB7");
	p->baudrate = 230400;
	p->bits = 7;
	p->stopbits = 2;
	p->parity = GT_PARITY_ODD;
	p->flow = GT_FLOW_RTSCTS;
	p->rs485_before_ms = 11;
	p->rs485_after_ms = 12;
	gt_profile_set_string(&p->host, "::1");
	gt_profile_set_string(&p->tcp_port, "2001");
	p->wait_delay_ms = 40;
	p->wait_char = 233;
	p->echo = TRUE;
	p->crlf_auto = TRUE;
	p->auto_reconnect = TRUE;
	p->esc_clear_screen = TRUE;
	p->timestamp = TRUE;
	gt_profile_set_string(&p->font, "DejaVu Sans Mono Bold 11");
	p->block_cursor = FALSE;
	p->columns = 132;
	p->rows = 43;
	p->scrollback = 5000;
	p->visual_bell = TRUE;
	p->foreground = (GtColor){ 0.25, 0.5, 0.75, 1.0 };
	p->background = (GtColor){ 0.125, 0.0, 1.0, 0.5 };
	gt_profile_set_string(&p->macros_file, "/home/u/mes macros/a.ini");
}

static void
comparer(const GtProfile *a, const GtProfile *b)
{
	g_assert_cmpint(a->transport, ==, b->transport);
	g_assert_cmpstr(a->port, ==, b->port);
	g_assert_cmpuint(a->baudrate, ==, b->baudrate);
	g_assert_cmpint(a->bits, ==, b->bits);
	g_assert_cmpint(a->stopbits, ==, b->stopbits);
	g_assert_cmpint(a->parity, ==, b->parity);
	g_assert_cmpint(a->flow, ==, b->flow);
	g_assert_cmpint(a->rs485_before_ms, ==, b->rs485_before_ms);
	g_assert_cmpint(a->rs485_after_ms, ==, b->rs485_after_ms);
	g_assert_cmpstr(a->host, ==, b->host);
	g_assert_cmpstr(a->tcp_port, ==, b->tcp_port);
	g_assert_cmpint(a->wait_delay_ms, ==, b->wait_delay_ms);
	g_assert_cmpint(a->wait_char, ==, b->wait_char);
	g_assert_cmpint(a->echo, ==, b->echo);
	g_assert_cmpint(a->crlf_auto, ==, b->crlf_auto);
	g_assert_cmpint(a->auto_reconnect, ==, b->auto_reconnect);
	g_assert_cmpint(a->esc_clear_screen, ==, b->esc_clear_screen);
	g_assert_cmpint(a->timestamp, ==, b->timestamp);
	g_assert_cmpstr(a->font, ==, b->font);
	g_assert_cmpint(a->block_cursor, ==, b->block_cursor);
	g_assert_cmpint(a->columns, ==, b->columns);
	g_assert_cmpint(a->rows, ==, b->rows);
	g_assert_cmpint(a->scrollback, ==, b->scrollback);
	g_assert_cmpint(a->visual_bell, ==, b->visual_bell);
	g_assert_cmpfloat(a->foreground.green, ==, b->foreground.green);
	g_assert_cmpfloat(a->background.alpha, ==, b->background.alpha);
	g_assert_cmpfloat(a->background.red, ==, b->background.red);
	g_assert_cmpstr(a->macros_file, ==, b->macros_file);
}

static void
test_aller_retour(void)
{
	gchar *p = chemin("aller-retour.rc");
	GtConfigStore *s = charger(p);
	GtProfile ecrit, lu = { 0 };
	gchar *contenu;

	profil_complet(&ecrit);
	gt_config_store_put_profile(s, "atelier", &ecrit);
	enregistrer(s);
	gt_config_store_free(s);

	contenu = lire(p);
	g_assert_nonnull(strstr(contenu, "[profile:atelier]"));
	g_assert_nonnull(strstr(contenu, "transport=tcp-server"));
	g_assert_nonnull(strstr(contenu, "parity=odd"));
	g_assert_nonnull(strstr(contenu, "flow=rts"));
	g_assert_nonnull(strstr(contenu, "font=DejaVu Sans Mono Bold 11"));
	g_assert_nonnull(strstr(contenu, "term_columns=132"));
	g_assert_nonnull(strstr(contenu, "echo=true"));
	g_assert_null(strstr(contenu, "lock_port"));

	s = charger(p);
	g_assert_true(gt_config_store_get_profile(s, "atelier", &lu));
	comparer(&lu, &ecrit);
	g_assert_true(lu.lock_port);	/* jamais enregistré : défaut */
	gt_profile_clear(&ecrit);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(contenu);
	g_free(p);
}

static void
test_cles_absentes_defauts(void)
{
	gchar *p = ecrire("partiel.rc", "[profile:x]\nport=/dev/ttyX\necho=True\nspeed=9600\n");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };

	g_assert_true(gt_config_store_get_profile(s, "x", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/ttyX");
	g_assert_cmpuint(lu.baudrate, ==, 9600);
	g_assert_true(lu.echo);
	g_assert_cmpint(lu.bits, ==, 8);
	g_assert_cmpstr(lu.font, ==, "Monospace 12");
	g_assert_null(lu.macros_file);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_valeurs_fautives_ignorees(void)
{
	gchar *p = ecrire("fautif.rc", "[profile:x]\nspeed=vite\nbits=huit\nparity=mark\nterm_foreground=0.5;0.5\necho=peut-etre\nport=/dev/ttyQ\n");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };

	g_assert_true(gt_config_store_get_profile(s, "x", &lu));
	g_assert_cmpuint(lu.baudrate, ==, 115200);
	g_assert_cmpint(lu.bits, ==, 8);
	g_assert_cmpint(lu.parity, ==, GT_PARITY_NONE);
	g_assert_cmpfloat(lu.foreground.alpha, ==, 1.0);
	g_assert_false(lu.echo);
	g_assert_cmpstr(lu.port, ==, "/dev/ttyQ");
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_casse_des_noms(void)
{
	gchar *p = chemin("casse.rc");
	GtConfigStore *s = charger(p);
	GtProfile a, lu = { 0 };
	gchar **noms;

	gt_profile_init_defaults(&a);
	gt_profile_set_string(&a.port, "/dev/un");
	gt_config_store_put_profile(s, "Atelier", &a);
	gt_profile_set_string(&a.port, "/dev/deux");
	gt_config_store_put_profile(s, "ATELIER", &a);
	gt_config_store_put_profile(s, "Équipe", &a);
	g_assert_true(gt_config_store_has_profile(s, "atelier"));
	g_assert_true(gt_config_store_has_profile(s, "équipe"));
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpuint(g_strv_length(noms), ==, 2);
	g_assert_cmpstr(noms[0], ==, "Atelier");
	g_assert_cmpstr(noms[1], ==, "Équipe");
	g_strfreev(noms);
	g_assert_true(gt_config_store_get_profile(s, "aTeLiEr", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/deux");
	g_assert_true(gt_config_store_delete_profile(s, "atelier"));
	g_assert_false(gt_config_store_has_profile(s, "Atelier"));
	g_assert_false(gt_config_store_delete_profile(s, "atelier"));
	gt_profile_clear(&a);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_liste_triee(void)
{
	gchar *p = chemin("tri.rc");
	GtConfigStore *s = charger(p);
	GtProfile a;
	gchar **noms;

	gt_profile_init_defaults(&a);
	gt_config_store_put_profile(s, "b", &a);
	gt_config_store_put_profile(s, "A", &a);
	gt_config_store_put_profile(s, "c", &a);
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpstr(noms[0], ==, "A");
	g_assert_cmpstr(noms[1], ==, "b");
	g_assert_cmpstr(noms[2], ==, "c");
	g_assert_null(noms[3]);
	g_strfreev(noms);
	gt_profile_clear(&a);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_groupes_conserves(void)
{
	gchar *p = ecrire("groupes.rc", "[window]\nwidth=750\n\n[session]\nscript_path=/home/u/mes scripts/a.lua\n\n[extension]\nfoo=bar\n\n[profile:default]\nport=/dev/ttyS1\n");
	GtConfigStore *s = charger(p);
	GtProfile a;
	GKeyFile *kf;
	gchar *v;

	gt_profile_init_defaults(&a);
	gt_config_store_put_profile(s, "autre", &a);
	enregistrer(s);
	gt_config_store_free(s);

	s = charger(p);
	kf = gt_config_store_get_key_file(s);
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 750);
	v = g_key_file_get_string(kf, "session", "script_path", NULL);
	g_assert_cmpstr(v, ==, "/home/u/mes scripts/a.lua");
	g_free(v);
	v = g_key_file_get_string(kf, "extension", "foo", NULL);
	g_assert_cmpstr(v, ==, "bar");
	g_free(v);
	g_assert_true(gt_config_store_has_profile(s, "default"));
	g_assert_true(gt_config_store_has_profile(s, "autre"));
	gt_profile_clear(&a);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_ecriture_partielle(void)
{
	gchar *p = chemin("partielle.rc");
	GtConfigStore *s = charger(p);
	GtProfile a, lu = { 0 };

	gt_profile_init_defaults(&a);
	gt_profile_set_string(&a.port, "/dev/ttyP");
	gt_config_store_put_profile(s, "default", &a);
	gt_config_store_set_profile_string(s, "DEFAULT", "macros_file", "/m.ini");
	g_assert_true(gt_config_store_get_profile(s, "default", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/ttyP");
	g_assert_cmpstr(lu.macros_file, ==, "/m.ini");
	gt_config_store_set_profile_string(s, "default", "macros_file", NULL);
	g_assert_true(gt_config_store_get_profile(s, "default", &lu));
	g_assert_null(lu.macros_file);
	gt_profile_clear(&a);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_ecriture_partielle_profil_absent(void)
{
	gchar *p = chemin("absent-partiel.rc");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };

	gt_config_store_set_profile_string(s, "neuf", "macros_file", "/n.ini");
	g_assert_true(gt_config_store_get_profile(s, "neuf", &lu));
	g_assert_cmpstr(lu.macros_file, ==, "/n.ini");
	g_assert_cmpstr(lu.port, ==, "/dev/ttyS0");
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_reglages_globaux(void)
{
	gchar *p = chemin("general.rc");
	GtConfigStore *s = charger(p);
	gchar *v;

	v = gt_config_store_get_general_string(s, "update_url", "defaut");
	g_assert_cmpstr(v, ==, "defaut");
	g_free(v);
	g_assert_true(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));
	gt_config_store_set_general_string(s, "update_url", "https://exemple.invalid/x.git");
	gt_config_store_set_general_boolean(s, "update_check_startup", FALSE);
	enregistrer(s);
	gt_config_store_free(s);

	s = charger(p);
	v = gt_config_store_get_general_string(s, "update_url", "defaut");
	g_assert_cmpstr(v, ==, "https://exemple.invalid/x.git");
	g_free(v);
	g_assert_false(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));
	gt_config_store_free(s);
	g_free(p);
}

static void
test_enregistrement_droits_et_dossier(void)
{
	gchar *sous = g_build_filename(dossier, "a", "b", NULL);
	gchar *p = g_build_filename(sous, ".gtktermrc", NULL);
	GtConfigStore *s = charger(p);
	GtProfile a;
	struct stat st;

	gt_profile_init_defaults(&a);
	gt_config_store_put_profile(s, "default", &a);
	enregistrer(s);
	g_assert_cmpint(g_stat(p, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);
	gt_profile_clear(&a);
	gt_config_store_free(s);
	g_unlink(p);
	g_rmdir(sous);
	g_free(p);
	g_free(sous);
	sous = g_build_filename(dossier, "a", NULL);
	g_rmdir(sous);
	g_free(sous);
}

static void
test_booleens_tolerants(void)
{
	static const struct { const gchar *texte; gboolean ok; gboolean valeur; } cas[] = {
		{ "true", TRUE, TRUE }, { "True", TRUE, TRUE }, { "TRUE", TRUE, TRUE }, { "yes", TRUE, TRUE }, { "on", TRUE, TRUE }, { "1", TRUE, TRUE },
		{ "false", TRUE, FALSE }, { "False", TRUE, FALSE }, { "no", TRUE, FALSE }, { "off", TRUE, FALSE }, { "0", TRUE, FALSE },
		{ "peut-etre", FALSE, FALSE }, { "", FALSE, FALSE },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		gboolean v = 42;

		g_assert_cmpint(gt_config_parse_boolean(cas[i].texte, &v), ==, cas[i].ok);
		if(cas[i].ok)
			g_assert_cmpint(v, ==, cas[i].valeur);
		else
			g_assert_cmpint(v, ==, 42);
	}
	g_assert_false(gt_config_parse_boolean(NULL, NULL));
}

static void
nettoyer(void)
{
	GDir *d = g_dir_open(dossier, 0, NULL);
	const gchar *nom;

	while(d != NULL && (nom = g_dir_read_name(d)) != NULL)
	{
		gchar *p = chemin(nom);

		g_chmod(p, 0600);
		g_unlink(p);
		g_free(p);
	}
	if(d != NULL)
		g_dir_close(d);
	g_rmdir(dossier);
}

int
main(int argc, char **argv)
{
	gint r;

	g_test_init(&argc, &argv, NULL);
	dossier = g_dir_make_tmp("gtkterm-store-XXXXXX", NULL);
	g_assert_nonnull(dossier);
	g_test_add_func("/config_store/fichier-absent", test_fichier_absent);
	g_test_add_func("/config_store/fichier-vide", test_fichier_vide);
	g_test_add_func("/config_store/illisible-lecture-seule", test_illisible_lecture_seule);
	g_test_add_func("/config_store/aller-retour", test_aller_retour);
	g_test_add_func("/config_store/cles-absentes-defauts", test_cles_absentes_defauts);
	g_test_add_func("/config_store/valeurs-fautives-ignorees", test_valeurs_fautives_ignorees);
	g_test_add_func("/config_store/casse-des-noms", test_casse_des_noms);
	g_test_add_func("/config_store/liste-triee", test_liste_triee);
	g_test_add_func("/config_store/groupes-conserves", test_groupes_conserves);
	g_test_add_func("/config_store/ecriture-partielle", test_ecriture_partielle);
	g_test_add_func("/config_store/ecriture-partielle-profil-absent", test_ecriture_partielle_profil_absent);
	g_test_add_func("/config_store/reglages-globaux", test_reglages_globaux);
	g_test_add_func("/config_store/enregistrement-droits-et-dossier", test_enregistrement_droits_et_dossier);
	g_test_add_func("/config_store/booleens-tolerants", test_booleens_tolerants);
	r = g_test_run();
	nettoyer();
	g_free(dossier);
	return r;
}
```

`tests/meson.build`, `unit_tests` : ajouter `'test_config_store' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | grep -m3 error` — Expected : `config_store.h: No such file or directory`.

- [ ] **Step 3 : écrire `config_store.h`**

```c
/* Magasin de configuration (sous-projet 3) : le seul code qui lit et écrit .gtktermrc.
 * Groupes : [general], [profile:nom], réservés [window] [colors] [layout] [session] (propriétaires : état de la fenêtre,
 * panneau de scripts), groupes inconnus conservés. Les anciens fichiers parsecfg sont migrés en mémoire au chargement. */
#ifndef CONFIG_STORE_H_
#define CONFIG_STORE_H_

#include <glib.h>

#include "profile.h"

typedef struct _GtConfigStore GtConfigStore;

/* Jamais NULL. Fichier absent ou vide : magasin vide inscriptible. Fichier illisible : GError et magasin vide en lecture seule. */
GtConfigStore *gt_config_store_load(const gchar *path, GError **error);
void           gt_config_store_free(GtConfigStore *s);
gboolean       gt_config_store_is_read_only(GtConfigStore *s);
gboolean       gt_config_store_was_migrated(GtConfigStore *s);
const gchar   *gt_config_store_get_path(GtConfigStore *s);

/* Profils ; noms comparés sans tenir compte de la casse (g_utf8_casefold), casse d'origine conservée */
gchar        **gt_config_store_list_profiles(GtConfigStore *s);
gboolean       gt_config_store_has_profile(GtConfigStore *s, const gchar *name);
/* out initialisé ou mis à zéro ; vidé puis rempli (défauts pour les clés absentes ou illisibles). FALSE si le profil n'existe pas (out inchangé). */
gboolean       gt_config_store_get_profile(GtConfigStore *s, const gchar *name, GtProfile *out);
void           gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p);
gboolean       gt_config_store_delete_profile(GtConfigStore *s, const gchar *name);
/* Écriture d'une seule clé (value NULL : clé retirée) ; crée le profil s'il n'existe pas */
void           gt_config_store_set_profile_string(GtConfigStore *s, const gchar *name, const gchar *key, const gchar *value);

/* Réglages globaux [general] */
gchar         *gt_config_store_get_general_string(GtConfigStore *s, const gchar *key, const gchar *fallback);
gboolean       gt_config_store_get_general_boolean(GtConfigStore *s, const gchar *key, gboolean fallback);
void           gt_config_store_set_general_string(GtConfigStore *s, const gchar *key, const gchar *value);
void           gt_config_store_set_general_boolean(GtConfigStore *s, const gchar *key, gboolean value);

/* Accès direct, pour les propriétaires des groupes réservés ; ne pas toucher aux groupes [profile:…] ni [general] par ce biais */
GKeyFile      *gt_config_store_get_key_file(GtConfigStore *s);

/* Écriture atomique en 0600, dossier créé ; refusée en lecture seule ; copie de sauvegarde unique avant la première écriture d'un fichier migré */
gboolean       gt_config_store_save(GtConfigStore *s, GError **error);

/* true/yes/on/1 et false/no/off/0, casse ignorée ; out inchangé si le texte n'est pas reconnu */
gboolean       gt_config_parse_boolean(const gchar *s, gboolean *out);

#endif
```

- [ ] **Step 4 : écrire `config_store.c`**

```c
/***********************************************************************/
/* config_store.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      Magasin de configuration : seul lecteur et écrivain de         */
/*      .gtktermrc (GKeyFile), profils, réglages globaux (sous-projet 3)*/
/*                                                                     */
/***********************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "config_file.h"
#include "config_store.h"

#define PROFILE_PREFIX "profile:"
#define GENERAL_GROUP "general"

struct _GtConfigStore
{
	gchar *path;
	GKeyFile *kf;
	gboolean read_only;
	gboolean migrated;
	gboolean backup_done;
	gchar *original_contents;	/* fichier d'origine d'un magasin migré : copie de sauvegarde */
};

gboolean gt_config_parse_boolean(const gchar *s, gboolean *out)
{
	static const gchar * const vrai[] = { "true", "yes", "on", "1", NULL };
	static const gchar * const faux[] = { "false", "no", "off", "0", NULL };

	if(s == NULL)
		return FALSE;
	for(gint i = 0; vrai[i] != NULL; i++)
	{
		if(g_ascii_strcasecmp(s, vrai[i]) == 0)
		{
			*out = TRUE;
			return TRUE;
		}
		if(g_ascii_strcasecmp(s, faux[i]) == 0)
		{
			*out = FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---- Noms de groupes ---------------------------------------------------- */

static gboolean same_name(const gchar *a, const gchar *b)
{
	gchar *fa = g_utf8_casefold(a, -1);
	gchar *fb = g_utf8_casefold(b, -1);
	gboolean same = (g_strcmp0(fa, fb) == 0);

	g_free(fa);
	g_free(fb);
	return same;
}

/* Groupe « préfixe + nom » existant, nom comparé sans casse ; NULL sinon ; à libérer */
static gchar *find_prefixed_group(GtConfigStore *s, const gchar *prefix, const gchar *name)
{
	gchar **groups = g_key_file_get_groups(s->kf, NULL);
	gchar *found = NULL;
	gsize plen = strlen(prefix);

	for(gint i = 0; groups[i] != NULL && found == NULL; i++)
		if(g_str_has_prefix(groups[i], prefix) && same_name(groups[i] + plen, name))
			found = g_strdup(groups[i]);
	g_strfreev(groups);
	return found;
}

/* Groupe du profil : l'existant (casse d'origine) ou un nouveau nom ; à libérer */
static gchar *profile_group_for_write(GtConfigStore *s, const gchar *name)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);

	return group != NULL ? group : g_strconcat(PROFILE_PREFIX, name, NULL);
}

static gint compare_names(gconstpointer a, gconstpointer b)
{
	const gchar *na = *(const gchar * const *)a;
	const gchar *nb = *(const gchar * const *)b;
	gchar *fa = g_utf8_casefold(na, -1);
	gchar *fb = g_utf8_casefold(nb, -1);
	gint r = g_utf8_collate(fa, fb);

	g_free(fa);
	g_free(fb);
	return r;
}

/* ---- Lecture tolérante d'un profil -------------------------------------- */

static void read_string(GKeyFile *kf, const gchar *group, const gchar *key, gchar **field)
{
	gchar *v = g_key_file_get_string(kf, group, key, NULL);

	if(v != NULL)
	{
		g_free(*field);
		*field = v;
	}
}

static void read_int(GKeyFile *kf, const gchar *group, const gchar *key, gint *field)
{
	gchar *v = g_key_file_get_value(kf, group, key, NULL);
	gchar *end = NULL;
	gint64 n;

	if(v == NULL)
		return;
	n = g_ascii_strtoll(g_strstrip(v), &end, 10);
	if(end != v && *end == '\0' && n >= G_MININT && n <= G_MAXINT)
		*field = (gint)n;
	g_free(v);
}

static void read_uint(GKeyFile *kf, const gchar *group, const gchar *key, guint *field)
{
	gint n = -1;

	read_int(kf, group, key, &n);
	if(n > 0)
		*field = (guint)n;
}

static void read_bool(GKeyFile *kf, const gchar *group, const gchar *key, gboolean *field)
{
	gchar *v = g_key_file_get_value(kf, group, key, NULL);

	if(v != NULL)
		gt_config_parse_boolean(g_strstrip(v), field);
	g_free(v);
}

static void read_color(GKeyFile *kf, const gchar *group, const gchar *key, GtColor *c)
{
	gsize n = 0;
	gdouble *v = g_key_file_get_double_list(kf, group, key, &n, NULL);

	if(v != NULL && n == 4)
		*c = (GtColor){ v[0], v[1], v[2], v[3] };
	g_free(v);
}

static void read_profile(GKeyFile *kf, const gchar *g, GtProfile *p)
{
	gchar *s = NULL;

	read_string(kf, g, "transport", &s);
	gt_transport_from_string(s, &p->transport);
	g_clear_pointer(&s, g_free);
	read_string(kf, g, "port", &p->port);
	read_uint(kf, g, "speed", &p->baudrate);
	read_int(kf, g, "bits", &p->bits);
	read_int(kf, g, "stopbits", &p->stopbits);
	read_string(kf, g, "parity", &s);
	gt_parity_from_string(s, &p->parity);
	g_clear_pointer(&s, g_free);
	read_string(kf, g, "flow", &s);
	gt_flow_from_string(s, &p->flow);
	g_clear_pointer(&s, g_free);
	read_int(kf, g, "wait_delay", &p->wait_delay_ms);
	if(g_key_file_has_key(kf, g, "wait_char", NULL))
	{
		gint w = -1;

		read_int(kf, g, "wait_char", &w);
		p->wait_char = config_wait_char_from_file(w);
	}
	read_int(kf, g, "rs485_rts_time_before_tx", &p->rs485_before_ms);
	read_int(kf, g, "rs485_rts_time_after_tx", &p->rs485_after_ms);
	read_string(kf, g, "socket_host", &p->host);
	read_string(kf, g, "socket_port", &p->tcp_port);
	read_bool(kf, g, "echo", &p->echo);
	read_bool(kf, g, "crlfauto", &p->crlf_auto);
	read_bool(kf, g, "autoreconnect_enabled", &p->auto_reconnect);
	read_bool(kf, g, "esc_clear_screen", &p->esc_clear_screen);
	read_bool(kf, g, "timestamp", &p->timestamp);
	read_string(kf, g, "font", &p->font);
	read_bool(kf, g, "term_block_cursor", &p->block_cursor);
	read_int(kf, g, "term_columns", &p->columns);
	read_int(kf, g, "term_rows", &p->rows);
	read_int(kf, g, "term_scrollback", &p->scrollback);
	read_bool(kf, g, "term_visual_bell", &p->visual_bell);
	read_color(kf, g, "term_foreground", &p->foreground);
	read_color(kf, g, "term_background", &p->background);
	read_string(kf, g, "macros_file", &p->macros_file);
}

static void write_color(GKeyFile *kf, const gchar *group, const gchar *key, const GtColor *c)
{
	gdouble v[4] = { c->red, c->green, c->blue, c->alpha };

	g_key_file_set_double_list(kf, group, key, v, 4);
}

static void write_profile(GKeyFile *kf, const gchar *g, const GtProfile *p)
{
	g_key_file_set_string(kf, g, "transport", gt_transport_to_string(p->transport));
	g_key_file_set_string(kf, g, "port", p->port != NULL ? p->port : "");
	g_key_file_set_integer(kf, g, "speed", (gint)p->baudrate);
	g_key_file_set_integer(kf, g, "bits", p->bits);
	g_key_file_set_integer(kf, g, "stopbits", p->stopbits);
	g_key_file_set_string(kf, g, "parity", gt_parity_to_string(p->parity));
	g_key_file_set_string(kf, g, "flow", gt_flow_to_string(p->flow));
	g_key_file_set_integer(kf, g, "wait_delay", p->wait_delay_ms);
	g_key_file_set_integer(kf, g, "wait_char", p->wait_char);
	g_key_file_set_integer(kf, g, "rs485_rts_time_before_tx", p->rs485_before_ms);
	g_key_file_set_integer(kf, g, "rs485_rts_time_after_tx", p->rs485_after_ms);
	g_key_file_set_string(kf, g, "socket_host", p->host != NULL ? p->host : "");
	g_key_file_set_string(kf, g, "socket_port", p->tcp_port != NULL ? p->tcp_port : "");
	g_key_file_set_boolean(kf, g, "echo", p->echo);
	g_key_file_set_boolean(kf, g, "crlfauto", p->crlf_auto);
	g_key_file_set_boolean(kf, g, "autoreconnect_enabled", p->auto_reconnect);
	g_key_file_set_boolean(kf, g, "esc_clear_screen", p->esc_clear_screen);
	g_key_file_set_boolean(kf, g, "timestamp", p->timestamp);
	g_key_file_set_string(kf, g, "font", p->font != NULL ? p->font : GT_PROFILE_DEFAULT_FONT);
	g_key_file_set_boolean(kf, g, "term_block_cursor", p->block_cursor);
	g_key_file_set_integer(kf, g, "term_columns", p->columns);
	g_key_file_set_integer(kf, g, "term_rows", p->rows);
	g_key_file_set_integer(kf, g, "term_scrollback", p->scrollback);
	g_key_file_set_boolean(kf, g, "term_visual_bell", p->visual_bell);
	write_color(kf, g, "term_foreground", &p->foreground);
	write_color(kf, g, "term_background", &p->background);
	if(p->macros_file != NULL)
		g_key_file_set_string(kf, g, "macros_file", p->macros_file);
	else
		g_key_file_remove_key(kf, g, "macros_file", NULL);
}

/* ---- Interface publique ------------------------------------------------- */

/* Migration de l'ancien format (tâche 3) : FALSE tant qu'elle n'est pas écrite */
static gboolean store_migrate(GtConfigStore *s)
{
	(void)s;
	return FALSE;
}

GtConfigStore *gt_config_store_load(const gchar *path, GError **error)
{
	GtConfigStore *s = g_new0(GtConfigStore, 1);
	GError *local = NULL;
	gchar *contents = NULL;
	gsize len = 0;

	s->path = g_strdup(path);
	s->kf = g_key_file_new();

	if(!g_file_get_contents(path, &contents, &len, &local))
	{
		if(g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		{
			g_error_free(local);
			return s;
		}
		s->read_only = TRUE;
		g_propagate_error(error, local);
		return s;
	}

	if(len > 0 && !g_key_file_load_from_data(s->kf, contents, len, G_KEY_FILE_KEEP_COMMENTS, &local))
	{
		g_key_file_free(s->kf);
		s->kf = g_key_file_new();
		s->read_only = TRUE;
		g_propagate_prefixed_error(error, local, "%s: ", path);
		g_free(contents);
		return s;
	}

	if(store_migrate(s))
	{
		s->migrated = TRUE;
		s->original_contents = contents;
	}
	else
		g_free(contents);
	return s;
}

void gt_config_store_free(GtConfigStore *s)
{
	if(s == NULL)
		return;
	g_key_file_free(s->kf);
	g_free(s->original_contents);
	g_free(s->path);
	g_free(s);
}

gboolean gt_config_store_is_read_only(GtConfigStore *s)
{
	return s->read_only;
}

gboolean gt_config_store_was_migrated(GtConfigStore *s)
{
	return s->migrated;
}

const gchar *gt_config_store_get_path(GtConfigStore *s)
{
	return s->path;
}

gchar **gt_config_store_list_profiles(GtConfigStore *s)
{
	gchar **groups = g_key_file_get_groups(s->kf, NULL);
	GPtrArray *names = g_ptr_array_new();

	for(gint i = 0; groups[i] != NULL; i++)
		if(g_str_has_prefix(groups[i], PROFILE_PREFIX))
			g_ptr_array_add(names, g_strdup(groups[i] + strlen(PROFILE_PREFIX)));
	g_strfreev(groups);
	g_ptr_array_sort(names, compare_names);
	g_ptr_array_add(names, NULL);
	return (gchar **)g_ptr_array_free(names, FALSE);
}

gboolean gt_config_store_has_profile(GtConfigStore *s, const gchar *name)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);
	gboolean found = (group != NULL);

	g_free(group);
	return found;
}

gboolean gt_config_store_get_profile(GtConfigStore *s, const gchar *name, GtProfile *out)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);

	if(group == NULL)
		return FALSE;
	gt_profile_clear(out);
	gt_profile_init_defaults(out);
	read_profile(s->kf, group, out);
	g_free(group);
	return TRUE;
}

void gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p)
{
	gchar *group = profile_group_for_write(s, name);

	write_profile(s->kf, group, p);
	g_free(group);
}

gboolean gt_config_store_delete_profile(GtConfigStore *s, const gchar *name)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);
	gboolean done = FALSE;

	if(group != NULL)
		done = g_key_file_remove_group(s->kf, group, NULL);
	g_free(group);
	return done;
}

void gt_config_store_set_profile_string(GtConfigStore *s, const gchar *name, const gchar *key, const gchar *value)
{
	gchar *group = profile_group_for_write(s, name);

	if(value != NULL)
		g_key_file_set_string(s->kf, group, key, value);
	else if(g_key_file_has_group(s->kf, group))
		g_key_file_remove_key(s->kf, group, key, NULL);
	g_free(group);
}

gchar *gt_config_store_get_general_string(GtConfigStore *s, const gchar *key, const gchar *fallback)
{
	gchar *v = g_key_file_get_string(s->kf, GENERAL_GROUP, key, NULL);

	return v != NULL ? v : g_strdup(fallback);
}

gboolean gt_config_store_get_general_boolean(GtConfigStore *s, const gchar *key, gboolean fallback)
{
	gboolean v = fallback;

	read_bool(s->kf, GENERAL_GROUP, key, &v);
	return v;
}

void gt_config_store_set_general_string(GtConfigStore *s, const gchar *key, const gchar *value)
{
	g_key_file_set_string(s->kf, GENERAL_GROUP, key, value != NULL ? value : "");
}

void gt_config_store_set_general_boolean(GtConfigStore *s, const gchar *key, gboolean value)
{
	g_key_file_set_boolean(s->kf, GENERAL_GROUP, key, value);
}

GKeyFile *gt_config_store_get_key_file(GtConfigStore *s)
{
	return s->kf;
}

gboolean gt_config_store_save(GtConfigStore *s, GError **error)
{
	gchar *dir, *data;
	gsize len;
	gboolean ok;

	if(s->read_only)
	{
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM, _("The configuration file %s could not be read: nothing has been written to it."), s->path);
		return FALSE;
	}

	dir = g_path_get_dirname(s->path);
	if(g_mkdir_with_parents(dir, 0700) != 0)
	{
		gint err = errno;

		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Cannot create directory %s: %s"), dir, g_strerror(err));
		g_free(dir);
		return FALSE;
	}
	g_free(dir);

	if(s->migrated && !s->backup_done)
	{
		gchar *backup = g_strconcat(s->path, ".avant-migration", NULL);

		/* Une seule copie : jamais écrasée si elle existe déjà */
		if(!g_file_test(backup, G_FILE_TEST_EXISTS)
		   && !g_file_set_contents_full(backup, s->original_contents, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error))
		{
			g_free(backup);
			return FALSE;
		}
		g_free(backup);
		s->backup_done = TRUE;
	}

	data = g_key_file_to_data(s->kf, &len, NULL);
	ok = g_file_set_contents_full(s->path, data, (gssize)len, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
	g_free(data);
	return ok;
}
```

`src/meson.build`, `core_sources` : ajouter `'config_store.c',` et `'config_store.h',`.

Remarque : `store_migrate` est une coquille vide à cette tâche ; la tâche 3 l'écrit. Le test `/config_store/groupes-conserves` passe déjà car le groupe `[extension]` n'est pas un ancien profil pour une coquille vide ; il restera vrai après la tâche 3 (la règle de la tâche 3 traite `[extension]` comme ancien profil seulement s'il contient une clé de profil — voir tâche 3).

- [ ] **Step 5 : constater la réussite**

Run : `ninja -C build && meson test -C build test_config_store --print-errorlogs` — Expected : OK (14 cas).

- [ ] **Step 6 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/config_store.c src/config_store.h src/meson.build tests/test_config_store.c tests/meson.build
git commit -m "cœur : magasin de configuration GKeyFile (profils, réglages globaux, enregistrement atomique)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3 : migration de l'ancien format

**Files :**
- Modify : `src/config_store.c` (fonction `store_migrate` et ses aides), `tests/test_config_store.c` (nouveaux cas)

**Interfaces :**
- Consumes : tâche 2.
- Produces : `gt_config_store_load` migre un ancien fichier ; `gt_config_store_was_migrated` vrai ; copie `.gtktermrc.avant-migration` à la première écriture.

Règle de reconnaissance d'un **ancien profil** (affine la spec pour ne pas avaler les groupes inconnus) : un groupe de premier niveau qui n'est ni réservé (`window`, `colors`, `layout`, `session`, casse ignorée), ni préfixé (`profile:` ou `project:`), et qui contient au moins une clé de profil connue (`port`, `speed`, `transport_type`, `socket_host`, `font`, `term_rows`). `[general]` suit la même règle (décision 5 : il n'est un ancien profil que s'il contient `port`, `speed` ou `transport_type`). Les autres groupes sont conservés tels quels.

Conversion d'un ancien profil (spec §4) : nom sans guillemets ; groupe `[profile:nom]` (si un profil de même nom à la casse près existe déjà, `nom (2)`, `nom (3)`…) ; pour chaque clé, valeur brute (`g_key_file_get_value`) débarrassée de guillemets simples ou doubles appariés, puis :
- `transport_type` (0/1/2) → `transport` (`serial`/`tcp-client`/`tcp-server`) ;
- `term_rows` → `term_columns` et `term_columns` → `term_rows` (valeurs inversées dans l'ancien format) ;
- `term_foreground_red|green|blue|alpha` et `term_background_*` → `term_foreground` / `term_background` (liste de 4 réels ; composante absente : celle du défaut) ;
- booléens (`echo`, `crlfauto`, `autoreconnect_enabled`, `esc_clear_screen`, `timestamp`, `term_block_cursor`, `term_visual_bell`) → `true`/`false` (valeur non reconnue : clé omise) ;
- `wait_char` → `config_wait_char_from_file(atoi(valeur))` ;
- `show_rxtx` → ignorée ;
- `update_url`, `update_prefix`, `update_check_startup` → `[general]` (seulement depuis le profil source : `default` à la casse près, sinon le premier ancien profil ; seulement si `[general]` n'a pas déjà la clé ; booléen normalisé), et retirées de tous les profils ;
- autres clés : recopiées telles quelles (valeur sans guillemets, par `g_key_file_set_string`).

- [ ] **Step 1 : ajouter les tests**

Ajouter à `tests/test_config_store.c`, avant `nettoyer` :

```c
/* Fichier tel que l'écrivait parsecfg (cfgDump) puis complété par GKeyFile (groupes réservés) */
static const gchar ancien_fichier[] =
	"[default]\n"
	"port\t= /dev/ttyUSB0\n"
	"speed\t= 115200\n"
	"bits\t= 8\n"
	"stopbits\t= 1\n"
	"parity\t= none\n"
	"flow\t= none\n"
	"wait_delay\t= 0\n"
	"wait_char\t= -23\n"
	"rs485_rts_time_before_tx\t= 30\n"
	"rs485_rts_time_after_tx\t= 30\n"
	"echo\t= True\n"
	"crlfauto\t= False\n"
	"autoreconnect_enabled\t= True\n"
	"esc_clear_screen\t= False\n"
	"timestamp\t= False\n"
	"show_rxtx\t= False\n"
	"font\t= \"Monospace 12\"\n"
	"term_block_cursor\t= True\n"
	"term_rows\t= 80\n"
	"term_columns\t= 25\n"
	"term_scrollback\t= 10000\n"
	"term_visual_bell\t= True\n"
	"term_foreground_red\t= 0.000000\n"
	"term_foreground_blue\t= 0.000000\n"
	"term_foreground_green\t= 0.000000\n"
	"term_foreground_alpha\t= 0.000000\n"
	"term_background_red\t= 1.000000\n"
	"term_background_blue\t= 0.500000\n"
	"term_background_green\t= 1.000000\n"
	"term_background_alpha\t= 1.000000\n"
	"macros_file\t= \"/home/u/mes macros.ini\"\n"
	"transport_type\t= 0\n"
	"socket_host\t= \n"
	"socket_port\t= \n"
	"update_url\t= https://exemple.invalid/fork.git\n"
	"update_check_startup\t= False\n"
	"update_prefix\t= /opt/gtkterm\n"
	"\n"
	"[atelier]\n"
	"port\t= /dev/ttyACM0\n"
	"speed\t= 9600\n"
	"parity\t= even\n"
	"flow\t= rs485\n"
	"transport_type\t= 1\n"
	"socket_host\t= 192.168.1.10\n"
	"socket_port\t= 2000\n"
	"update_url\t= https://autre.invalid/x.git\n"
	"\n"
	"[\"mon profil\"]\n"
	"port\t= '/dev/serial/by-id/usb-\"x\"'\n"
	"speed\t= 57600\n"
	"\n"
	"[Atelier]\n"
	"port\t= /dev/ttyACM9\n"
	"\n"
	"[window]\n"
	"x=10\n"
	"width=750\n"
	"\n"
	"[session]\n"
	"script_path=/home/u/mes scripts/a.lua\n";

static void
test_migration_ancien_format(void)
{
	gchar *p = ecrire("ancien.rc", ancien_fichier);
	GtConfigStore *s = charger(p);
	GtProfile d = { 0 }, a = { 0 }, m = { 0 }, a2 = { 0 };
	gchar **noms;
	gchar *v;
	GKeyFile *kf;

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_false(gt_config_store_is_read_only(s));
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpuint(g_strv_length(noms), ==, 4);
	g_assert_cmpstr(noms[0], ==, "atelier");
	g_assert_cmpstr(noms[1], ==, "Atelier (2)");
	g_assert_cmpstr(noms[2], ==, "default");
	g_assert_cmpstr(noms[3], ==, "mon profil");
	g_strfreev(noms);

	g_assert_true(gt_config_store_get_profile(s, "default", &d));
	g_assert_cmpint(d.transport, ==, TRANSPORT_SERIAL);
	g_assert_cmpstr(d.port, ==, "/dev/ttyUSB0");
	g_assert_cmpuint(d.baudrate, ==, 115200);
	g_assert_true(d.echo);
	g_assert_false(d.crlf_auto);
	g_assert_true(d.auto_reconnect);
	g_assert_cmpint(d.wait_char, ==, 233);
	g_assert_cmpstr(d.font, ==, "Monospace 12");
	g_assert_true(d.block_cursor);
	g_assert_cmpint(d.columns, ==, 80);
	g_assert_cmpint(d.rows, ==, 25);
	g_assert_cmpint(d.scrollback, ==, 10000);
	g_assert_true(d.visual_bell);
	g_assert_cmpfloat(d.foreground.alpha, ==, 0.0);
	g_assert_cmpfloat(d.background.blue, ==, 0.5);
	g_assert_cmpfloat(d.background.green, ==, 1.0);
	g_assert_cmpstr(d.macros_file, ==, "/home/u/mes macros.ini");
	g_assert_cmpstr(d.host, ==, "");

	g_assert_true(gt_config_store_get_profile(s, "atelier", &a));
	g_assert_cmpint(a.transport, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(a.host, ==, "192.168.1.10");
	g_assert_cmpstr(a.tcp_port, ==, "2000");
	g_assert_cmpint(a.parity, ==, GT_PARITY_EVEN);
	g_assert_cmpint(a.flow, ==, GT_FLOW_RS485);

	g_assert_true(gt_config_store_get_profile(s, "mon profil", &m));
	g_assert_cmpstr(m.port, ==, "/dev/serial/by-id/usb-\"x\"");
	g_assert_true(gt_config_store_get_profile(s, "Atelier (2)", &a2));
	g_assert_cmpstr(a2.port, ==, "/dev/ttyACM9");

	v = gt_config_store_get_general_string(s, "update_url", NULL);
	g_assert_cmpstr(v, ==, "https://exemple.invalid/fork.git");
	g_free(v);
	v = gt_config_store_get_general_string(s, "update_prefix", NULL);
	g_assert_cmpstr(v, ==, "/opt/gtkterm");
	g_free(v);
	g_assert_false(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));

	kf = gt_config_store_get_key_file(s);
	g_assert_false(g_key_file_has_key(kf, "profile:default", "update_url", NULL));
	g_assert_false(g_key_file_has_key(kf, "profile:atelier", "update_url", NULL));
	g_assert_false(g_key_file_has_key(kf, "profile:default", "show_rxtx", NULL));
	g_assert_false(g_key_file_has_key(kf, "profile:default", "term_foreground_red", NULL));
	g_assert_false(g_key_file_has_group(kf, "default"));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 750);
	v = g_key_file_get_string(kf, "session", "script_path", NULL);
	g_assert_cmpstr(v, ==, "/home/u/mes scripts/a.lua");
	g_free(v);

	gt_profile_clear(&d);
	gt_profile_clear(&a);
	gt_profile_clear(&m);
	gt_profile_clear(&a2);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_migration_copie_de_sauvegarde(void)
{
	gchar *p = ecrire("sauvegarde.rc", ancien_fichier);
	gchar *copie = g_strconcat(p, ".avant-migration", NULL);
	GtConfigStore *s = charger(p);
	gchar *c;

	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	enregistrer(s);
	c = lire(copie);
	g_assert_cmpstr(c, ==, ancien_fichier);
	g_free(c);
	/* Une seule copie : un second enregistrement ne la réécrit pas */
	g_assert_true(g_file_set_contents(copie, "deja la", -1, NULL));
	enregistrer(s);
	c = lire(copie);
	g_assert_cmpstr(c, ==, "deja la");
	g_free(c);
	gt_config_store_free(s);

	/* Le fichier réécrit n'est plus à migrer */
	s = charger(p);
	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_has_profile(s, "default"));
	c = lire(p);
	g_assert_nonnull(strstr(c, "[profile:default]"));
	g_assert_nonnull(strstr(c, "font=Monospace 12"));
	g_assert_nonnull(strstr(c, "[general]"));
	g_free(c);
	gt_config_store_free(s);
	g_unlink(copie);
	g_free(copie);
	g_free(p);
}

static void
test_migration_style_mixte(void)
{
	gchar *p = ecrire("mixte.rc", "[default]\nport=/dev/ttyUSB1\necho\t= true\nfont=\"Fira Code 10\"\n\n[colors]\nbg=rgb(0,0,0)\n");
	GtConfigStore *s = charger(p);
	GtProfile d = { 0 };

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "default", &d));
	g_assert_cmpstr(d.port, ==, "/dev/ttyUSB1");
	g_assert_true(d.echo);
	g_assert_cmpstr(d.font, ==, "Fira Code 10");
	g_assert_true(g_key_file_has_group(gt_config_store_get_key_file(s), "colors"));
	gt_profile_clear(&d);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_migration_profil_nomme_general(void)
{
	gchar *p = ecrire("general-ancien.rc", "[general]\nport=/dev/ttyUSB2\nspeed=19200\n");
	GtConfigStore *s = charger(p);
	GtProfile g = { 0 };

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "general", &g));
	g_assert_cmpuint(g.baudrate, ==, 19200);
	gt_profile_clear(&g);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_nouveau_format_non_migre(void)
{
	gchar *p = ecrire("nouveau.rc", "[general]\nupdate_url=https://x.invalid/y.git\n\n[profile:default]\nport=/dev/ttyUSB3\n\n[extension]\nfoo=bar\n");
	GtConfigStore *s = charger(p);

	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_true(g_key_file_has_group(gt_config_store_get_key_file(s), "extension"));
	gt_config_store_free(s);
	g_free(p);
}
```

et les enregistrer dans `main` :

```c
	g_test_add_func("/config_store/migration-ancien-format", test_migration_ancien_format);
	g_test_add_func("/config_store/migration-copie-de-sauvegarde", test_migration_copie_de_sauvegarde);
	g_test_add_func("/config_store/migration-style-mixte", test_migration_style_mixte);
	g_test_add_func("/config_store/migration-profil-nomme-general", test_migration_profil_nomme_general);
	g_test_add_func("/config_store/nouveau-format-non-migre", test_nouveau_format_non_migre);
```

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build && meson test -C build test_config_store --print-errorlogs` — Expected : échec de `/config_store/migration-ancien-format` (`was_migrated` faux).

- [ ] **Step 3 : écrire la migration**

Dans `src/config_store.c`, remplacer la coquille `store_migrate` par :

```c
static const gchar * const reserved_groups[] = { "window", "colors", "layout", "session", NULL };
static const gchar * const legacy_marker_keys[] = { "port", "speed", "transport_type", "socket_host", "font", "term_rows", NULL };
static const gchar * const general_marker_keys[] = { "port", "speed", "transport_type", NULL };
static const gchar * const legacy_bool_keys[] = { "echo", "crlfauto", "autoreconnect_enabled", "esc_clear_screen", "timestamp", "term_block_cursor",
                                                  "term_visual_bell", NULL };
static const gchar * const update_keys[] = { "update_url", "update_prefix", "update_check_startup", NULL };

static gboolean in_list(const gchar * const *list, const gchar *s)
{
	for(gint i = 0; list[i] != NULL; i++)
		if(g_strcmp0(list[i], s) == 0)
			return TRUE;
	return FALSE;
}

static gboolean is_reserved_group(const gchar *g)
{
	for(gint i = 0; reserved_groups[i] != NULL; i++)
		if(g_ascii_strcasecmp(g, reserved_groups[i]) == 0)
			return TRUE;
	return FALSE;
}

static gboolean has_any_key(GKeyFile *kf, const gchar *g, const gchar * const *keys)
{
	for(gint i = 0; keys[i] != NULL; i++)
		if(g_key_file_has_key(kf, g, keys[i], NULL))
			return TRUE;
	return FALSE;
}

static gboolean is_legacy_profile(GKeyFile *kf, const gchar *g)
{
	if(is_reserved_group(g) || g_str_has_prefix(g, PROFILE_PREFIX) || g_str_has_prefix(g, "project:"))
		return FALSE;
	if(g_strcmp0(g, GENERAL_GROUP) == 0)
		return has_any_key(kf, g, general_marker_keys);
	return has_any_key(kf, g, legacy_marker_keys);
}

/* Retire une paire de guillemets simples ou doubles (écriture parsecfg) ; à libérer */
static gchar *unquote(const gchar *raw)
{
	gchar *v = g_strstrip(g_strdup(raw != NULL ? raw : ""));
	gsize n = strlen(v);

	if(n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0])
	{
		memmove(v, v + 1, n - 2);
		v[n - 2] = '\0';
	}
	return v;
}

/* Nom de profil libre (« nom », sinon « nom (2) »…) ; à libérer */
static gchar *unique_profile_name(GtConfigStore *s, const gchar *name)
{
	gchar *candidate = g_strdup(name);

	for(gint n = 2; gt_config_store_has_profile(s, candidate); n++)
	{
		g_free(candidate);
		candidate = g_strdup_printf("%s (%d)", name, n);
	}
	return candidate;
}

static void migrate_colors(GKeyFile *kf, const gchar *old, const gchar *group, const gchar *prefix, const gchar *key, GtColor def)
{
	static const gchar * const parts[] = { "red", "green", "blue", "alpha" };
	gdouble v[4] = { def.red, def.green, def.blue, def.alpha };
	gboolean any = FALSE;

	for(gint i = 0; i < 4; i++)
	{
		gchar *k = g_strconcat(prefix, parts[i], NULL);
		gchar *raw = g_key_file_get_value(kf, old, k, NULL);

		if(raw != NULL)
		{
			gchar *val = unquote(raw);
			gchar *end = NULL;
			gdouble d = g_ascii_strtod(val, &end);

			if(end != val)
			{
				v[i] = d;
				any = TRUE;
			}
			g_free(val);
			g_free(raw);
		}
		g_free(k);
	}
	if(any)
		g_key_file_set_double_list(kf, group, key, v, 4);
}

static void migrate_profile(GtConfigStore *s, const gchar *old, gboolean update_source)
{
	GKeyFile *kf = s->kf;
	gchar *name = unquote(old);
	gchar *unique = unique_profile_name(s, name);
	gchar *group = g_strconcat(PROFILE_PREFIX, unique, NULL);
	gchar **keys = g_key_file_get_keys(kf, old, NULL, NULL);
	GtProfile def;

	gt_profile_init_defaults(&def);
	for(gint i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		const gchar *k = keys[i];
		gchar *raw = g_key_file_get_value(kf, old, k, NULL);
		gchar *val = unquote(raw);
		gboolean b;

		if(g_str_has_prefix(k, "term_foreground_") || g_str_has_prefix(k, "term_background_") || g_strcmp0(k, "show_rxtx") == 0)
			;	/* couleurs traitées ensemble ci-dessous ; show_rxtx abandonnée */
		else if(in_list(update_keys, k))
		{
			if(update_source && !g_key_file_has_key(kf, GENERAL_GROUP, k, NULL))
			{
				if(g_strcmp0(k, "update_check_startup") == 0)
				{
					if(gt_config_parse_boolean(val, &b))
						g_key_file_set_boolean(kf, GENERAL_GROUP, k, b);
				}
				else
					g_key_file_set_string(kf, GENERAL_GROUP, k, val);
			}
		}
		else if(g_strcmp0(k, "transport_type") == 0)
		{
			gint t = atoi(val);

			if(t >= TRANSPORT_SERIAL && t <= TRANSPORT_TCP_SERVER)
				g_key_file_set_string(kf, group, "transport", gt_transport_to_string((TransportType)t));
		}
		else if(g_strcmp0(k, "term_rows") == 0)
			g_key_file_set_string(kf, group, "term_columns", val);	/* l'ancien format inversait lignes et colonnes (M14) */
		else if(g_strcmp0(k, "term_columns") == 0)
			g_key_file_set_string(kf, group, "term_rows", val);
		else if(in_list(legacy_bool_keys, k))
		{
			if(gt_config_parse_boolean(val, &b))
				g_key_file_set_boolean(kf, group, k, b);
		}
		else if(g_strcmp0(k, "wait_char") == 0)
			g_key_file_set_integer(kf, group, k, config_wait_char_from_file(atoi(val)));
		else
			g_key_file_set_string(kf, group, k, val);
		g_free(val);
		g_free(raw);
	}
	migrate_colors(kf, old, group, "term_foreground_", "term_foreground", def.foreground);
	migrate_colors(kf, old, group, "term_background_", "term_background", def.background);
	g_key_file_remove_group(kf, old, NULL);

	gt_profile_clear(&def);
	g_strfreev(keys);
	g_free(group);
	g_free(unique);
	g_free(name);
}

/* Migre en mémoire les profils de l'ancien format (parsecfg) ; TRUE si au moins un profil a été migré */
static gboolean store_migrate(GtConfigStore *s)
{
	gchar **groups = g_key_file_get_groups(s->kf, NULL);
	GPtrArray *legacy = g_ptr_array_new_with_free_func(g_free);
	const gchar *source = NULL;

	for(gint i = 0; groups[i] != NULL; i++)
		if(is_legacy_profile(s->kf, groups[i]))
			g_ptr_array_add(legacy, g_strdup(groups[i]));
	g_strfreev(groups);

	/* Réglages de mise à jour : ceux de « default », sinon du premier ancien profil */
	for(guint i = 0; i < legacy->len && source == NULL; i++)
	{
		gchar *n = unquote(g_ptr_array_index(legacy, i));

		if(g_ascii_strcasecmp(n, "default") == 0)
			source = g_ptr_array_index(legacy, i);
		g_free(n);
	}
	if(source == NULL && legacy->len > 0)
		source = g_ptr_array_index(legacy, 0);

	for(guint i = 0; i < legacy->len; i++)
		migrate_profile(s, g_ptr_array_index(legacy, i), g_ptr_array_index(legacy, i) == source);

	gboolean migrated = legacy->len > 0;
	g_ptr_array_unref(legacy);
	return migrated;
}
```

La valeur `socket_host\t= ` (vide) est recopiée vide ; `[general]` créé par la migration n'est pas un ancien profil (il ne contient pas `port`/`speed`/`transport_type`).

- [ ] **Step 4 : constater la réussite**

Run : `ninja -C build && meson test -C build test_config_store --print-errorlogs` — Expected : OK (19 cas).

- [ ] **Step 5 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/config_store.c tests/test_config_store.c
git commit -m "cœur : migration des anciens .gtktermrc (parsecfg) vers les groupes [profile:…] et [general]

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4 : le profil en cours devient un `GtProfile` (persistance parsecfg conservée)

**Files :**
- Modify : `src/term_config.h`, `src/term_config.c`, `src/cmdline.c`, `src/files.c`, `src/interface.c`, `src/update.c`, `src/script_panel.c`, `src/gtkterm.c`, `tests/stubs/stub_ui.c`

**Interfaces :**
- Consumes : `GtProfile`, `gt_profile_*` (tâche 1).
- Produces (utilisés par la tâche 5) :
  ```c
  /* term_config.h */
  #include "profile.h"
  extern GtProfile config;                       /* profil en cours */
  typedef struct { gchar *update_url; gchar *update_prefix; gboolean update_check_startup; } GtkTermSettings;
  extern GtkTermSettings settings;               /* réglages globaux */
  void config_color_to_rgba(const GtColor *c, GdkRGBA *out);
  void config_apply_to_terminal(void);           /* police, couleurs, taille, défilement, curseur */
  void Verify_configuration(void);               /* gt_profile_validate + une seule boîte */
  ```
  Supprimés : `struct configuration_port`, `display_config_t`, `term_conf`, `config_to_transport_params` (remplacé par `gt_profile_to_transport_params`), macros `DEFAULT_*` de `term_config.h` (s'il en reste un utilisateur, le remplacer par la valeur de `gt_profile_init_defaults`).

Table de correspondance des champs (à appliquer dans **tous** les fichiers de la liste) :

| Ancien | Nouveau |
|---|---|
| `config.transport_type` | `config.transport` |
| `config.port` (tableau) | `config.port` (`gchar *`) |
| `config.vitesse` | `config.baudrate` |
| `config.bits` | `config.bits` |
| `config.stops` | `config.stopbits` |
| `config.parite` | `config.parity` |
| `config.flux` | `config.flow` |
| `config.delai` | `config.wait_delay_ms` |
| `config.car` | `config.wait_char` |
| `config.rs485_rts_time_before_transmit` / `_after_transmit` | `config.rs485_before_ms` / `config.rs485_after_ms` |
| `config.socket_host` / `config.socket_port` (tableaux) | `config.host` / `config.tcp_port` (`gchar *`) |
| `config.echo`, `config.esc_clear_screen`, `config.timestamp` | inchangés |
| `config.crlfauto` | `config.crlf_auto` |
| `config.autoreconnect_enabled` | `config.auto_reconnect` |
| `config.disable_port_lock = TRUE` | `config.lock_port = FALSE` (et inverse) |
| `config.update_url`, `config.update_prefix`, `config.update_check_startup` | `settings.update_url`, `settings.update_prefix`, `settings.update_check_startup` |
| `term_conf.font`, `.block_cursor`, `.scrollback`, `.visual_bell` | `config.font`, `config.block_cursor`, `config.scrollback`, `config.visual_bell` |
| `term_conf.rows` (valait 80 : en réalité des colonnes) | `config.columns` |
| `term_conf.columns` (valait 25 : en réalité des lignes) | `config.rows` |
| `term_conf.foreground_color` / `background_color` (`GdkRGBA`) | `config.foreground` / `config.background` (`GtColor`) |

Règles :
- Chaînes : `g_strlcpy(config.X, v, sizeof(config.X))` → `gt_profile_set_string(&config.X, v)` ; `config.X[0] != '\0'` → `config.X != NULL && config.X[0] != '\0'` ; `g_strlcpy(settings.update_url, …)` → `gt_profile_set_string(&settings.update_url, …)`.
- Couleurs : partout où une API GTK/VTE attend un `GdkRGBA`, passer par `config_color_to_rgba` (et l'inverse pour `gtk_color_chooser_get_rgba` : variable `GdkRGBA` locale puis copie des quatre composantes dans `config.foreground`/`background`).
- Déclarations `extern struct configuration_port config;` dans `files.c`, `cmdline.c`, `interface.c`, `update.c` : supprimées (inclure `term_config.h`, qui déclare `config` et `settings`).

- [ ] **Step 1 : `term_config.h`**

Remplacer la définition de `struct configuration_port`, de `display_config_t`, `extern display_config_t term_conf;`, les macros `DEFAULT_*` et la déclaration de `config_to_transport_params` par :

```c
#include "profile.h"

/* Profil en cours : profil chargé, modifié par les options de ligne de commande et les dialogues (jamais enregistré sans action explicite) */
extern GtProfile config;

/* Réglages globaux (groupe [general] du fichier de configuration) */
typedef struct
{
	gchar *update_url;
	gchar *update_prefix;
	gboolean update_check_startup;
} GtkTermSettings;

extern GtkTermSettings settings;

#define GTKTERM_DEFAULT_UPDATE_URL "https://github.com/Mula-Gabriel/gtkterm.git"
#define GTKTERM_DEFAULT_UPDATE_PREFIX "/usr/local"

void config_color_to_rgba(const GtColor *c, GdkRGBA *out);
/* Applique au terminal la police, les couleurs, la taille (colonnes × lignes), le défilement et la forme du curseur du profil en cours */
void config_apply_to_terminal(void);
```

(garder les autres déclarations existantes ; `#include "transport.h"` peut rester.)

- [ ] **Step 2 : `term_config.c`**

1. Définitions globales : remplacer `struct configuration_port config;` et `display_config_t term_conf;` par `GtProfile config;` et `GtkTermSettings settings;`.
2. Supprimer `config_to_transport_params` ; ses appelants (`interface.c`, `interface_open_port`) utilisent `gt_profile_to_transport_params(&config, &params)`.
3. Ajouter :

```c
void config_color_to_rgba(const GtColor *c, GdkRGBA *out)
{
	out->red = c->red;
	out->green = c->green;
	out->blue = c->blue;
	out->alpha = c->alpha;
}

static void config_color_from_rgba(GtColor *c, const GdkRGBA *in)
{
	c->red = in->red;
	c->green = in->green;
	c->blue = in->blue;
	c->alpha = in->alpha;
}

void config_apply_to_terminal(void)
{
	PangoFontDescription *font_desc;
	GdkRGBA fg, bg;

	if(display == NULL)
		return;
	font_desc = pango_font_description_from_string(config.font != NULL ? config.font : GT_PROFILE_DEFAULT_FONT);
	vte_terminal_set_font(VTE_TERMINAL(display), font_desc);
	pango_font_description_free(font_desc);
	vte_terminal_set_size(VTE_TERMINAL(display), config.columns, config.rows);
	vte_terminal_set_scrollback_lines(VTE_TERMINAL(display), config.scrollback);
	config_color_to_rgba(&config.foreground, &fg);
	config_color_to_rgba(&config.background, &bg);
	vte_terminal_set_color_foreground(VTE_TERMINAL(display), &fg);
	vte_terminal_set_color_background(VTE_TERMINAL(display), &bg);
	vte_terminal_set_cursor_shape(VTE_TERMINAL(display), config.block_cursor ? VTE_CURSOR_SHAPE_BLOCK : VTE_CURSOR_SHAPE_IBEAM);
	gtk_widget_queue_draw(display);
}
```

4. `Hard_default_configuration` :

```c
void Hard_default_configuration(void)
{
	gt_profile_clear(&config);
	gt_profile_init_defaults(&config);
	gt_profile_set_string(&settings.update_url, GTKTERM_DEFAULT_UPDATE_URL);
	gt_profile_set_string(&settings.update_prefix, GTKTERM_DEFAULT_UPDATE_PREFIX);
	settings.update_check_startup = TRUE;
	config_apply_to_terminal();
}
```

5. `Verify_configuration` :

```c
void Verify_configuration(void)
{
	GPtrArray *warnings = g_ptr_array_new_with_free_func(g_free);

	if(!gt_profile_validate(&config, warnings))
	{
		gchar *text;

		g_ptr_array_add(warnings, NULL);
		text = g_strjoinv("\n", (gchar **)warnings->pdata);
		show_message(text, MSG_ERR);
		g_free(text);
	}
	g_ptr_array_unref(warnings);
}
```

6. `Load_configuration_from_file` (encore sur parsecfg à cette tâche) : remplacer `Hard_default_configuration()` du début par `gt_profile_clear(&config); gt_profile_init_defaults(&config);` (les réglages globaux ne sont pas réinitialisés par un chargement de profil), appliquer la table aux affectations, en lisant `rows[i]` dans `config.columns` et `columns[i]` dans `config.rows` (sens réel), les couleurs dans `config.foreground`/`background`, `update_*` dans `settings` (avec les mêmes valeurs par défaut que `Hard_default_configuration`), et remplacer tout le bloc final (police, `vte_terminal_set_size`, couleurs, curseur, `gtk_widget_queue_draw`) par `config_apply_to_terminal();`. Le cas « rows ou columns vaut 0 » se réduit à : `if(rows[i] == 0 || columns[i] == 0) { config.columns = 80; config.rows = 25; }` (les autres réglages du terminal gardent leur valeur lue ; la cloche visuelle reste celle lue).
7. `Copy_configuration` : appliquer la table ; écrire `term_rows` avec `config.columns` et `term_columns` avec `config.rows` (l'ancien format garde son sens inversé jusqu'à la tâche 5), les huit flottants depuis `config.foreground`/`background`, `update_*` depuis `settings`.
8. Dialogues : appliquer la table dans `Config_Port_Fenetre`, `Lis_Config`, `Config_Terminal`, `read_font_button`, `cursor_block`, `config_fg_color`, `config_bg_color`, `scrollback_set`. Dans `Config_Terminal` :

```c
	GdkRGBA rgba;
	...
	config_color_to_rgba(&config.foreground, &rgba);
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cfg_text_color), &rgba);
	...
	config_color_to_rgba(&config.background, &rgba);
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cfg_background_color), &rgba);
```

et

```c
void config_fg_color(GtkWidget *button, gpointer data)
{
	GdkRGBA rgba;

	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
	config_color_from_rgba(&config.foreground, &rgba);
	vte_terminal_set_color_foreground(VTE_TERMINAL(display), &rgba);
	gtk_widget_queue_draw(display);
}
```

(idem `config_bg_color` avec `background`). `Selec_couleur` devient inutile : le supprimer.
9. `configure_crlfauto` / `configure_autoreconnect_enable` : `config.crlf_auto` / `config.auto_reconnect`. `ConfigFlags` : `Set_crlfauto(config.crlf_auto)`, `Set_autoreconnect_enabled(config.auto_reconnect)`.

- [ ] **Step 3 : autres fichiers**

- `cmdline.c` : table ; `-p`, `-H`, `-P` par `gt_profile_set_string` ; `-L` → `config.lock_port = FALSE;` ; `-r` → `config.wait_char = (guchar)*optarg;`.
- `files.c` : `config.wait_delay_ms`, `config.wait_char`.
- `interface.c` : `gt_profile_to_transport_params(&config, &params)` dans `interface_open_port` ; `config.transport` dans `control_signals_read` ; dialogue À propos : `gtk_entry_set_text(GTK_ENTRY(url_entry), settings.update_url != NULL ? settings.update_url : "");` et `gt_profile_set_string(&settings.update_url, gtk_entry_get_text(GTK_ENTRY(url_entry)));` (l'appel suivant `save_config_silent()` reste à cette tâche).
- `update.c` : `settings.update_url`, `settings.update_prefix`, `settings.update_check_startup`.
- `script_panel.c` : `sc.bg = term_conf.background_color;` → `config_color_to_rgba(&config.background, &sc.bg);` (et `fg`), aux deux endroits.
- `gtkterm.c` : avant `create_main_window()`, initialiser `gt_profile_init_defaults(&config);` (le profil n'est plus une structure à zéro implicite pour les chaînes).
- `tests/stubs/stub_ui.c` : remplacer `struct configuration_port config;` et `display_config_t term_conf;` par `GtProfile config;` et `GtkTermSettings settings;`.

- [ ] **Step 4 : construction, suite, vérifications**

Run : `ninja -C build 2>&1 | grep -E 'error|warning' ; meson test -C build --print-errorlogs`
Expected : aucune erreur ni avertissement nouveau ; aucun échec.
Run : `grep -rnE 'configuration_port|display_config_t|term_conf|config\.(vitesse|parite|flux|stops|delai|car|socket_|transport_type|crlfauto|autoreconnect|disable_port_lock|update_)|config_to_transport_params' src tests`
Expected : aucune sortie.

- [ ] **Step 5 : non-régression de l'interface**

Run : `meson test -C build --suite gui --print-errorlogs 2>&1 | tail -4`
Expected : `Ok: 11`, `Fail: 0` (les scénarios écrivent un `.gtktermrc` à l'ancien format : port, écho, CR/LF, horodatage, reconnexion, fichier de macros sont relus par le code renommé).

- [ ] **Step 6 : commit**

```bash
git add -A src tests
git commit -m "configuration : le profil en cours devient un GtProfile (champs nommés, lignes et colonnes remises à l'endroit)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5 : persistance par le magasin, parsecfg supprimé

**Files :**
- Delete : `src/parsecfg.c`, `src/parsecfg.h`, `tests/test_parsecfg.c`, `tests/lsan.supp`
- Modify : `src/term_config.c`, `src/term_config.h`, `src/config_file.c`, `src/config_file.h`, `tests/test_config_file.c`, `src/script_panel.c`, `src/macro_panel.c`, `src/interface.c`, `src/gtkterm.c`, `src/cmdline.c`, `src/meson.build`, `tests/meson.build`, `po/POTFILES.in`

**Interfaces :**
- Consumes : `GtConfigStore` (tâches 2-3), profil en cours et réglages (tâche 4).
- Produces :
  ```c
  /* term_config.h */
  void           config_file_init(void);                 /* prépare le chemin et charge le magasin */
  GtConfigStore *config_get_store(void);
  gboolean       config_save_store(gboolean interactive); /* erreur : boîte si interactive, sinon g_printerr */
  void           config_remember_macros_file(void);      /* écriture partielle : macros_file du profil actif */
  void           config_save_settings(void);             /* écriture partielle : [general] */
  gint           Load_configuration_from_file(const gchar *name);
  gint           Check_configuration_file(void);
  ```
  Supprimés : `save_config_silent`, `get_config_file_path`, `Copy_configuration`, `cfg[]` et ses tableaux, `config_file_parse_profiles`, `config_file_find_profile`, `config_file_is_reserved_name`, `config_file_remove_section`, `config_file_strip_reserved_groups`, `config_file_restore_reserved_groups`.

- [ ] **Step 1 : réduire `config_file`**

`src/config_file.h` ne garde que :

```c
/* Fichier de configuration .gtktermrc : préparation du chemin et décodage du caractère d'attente (sans GTK). */
#ifndef CONFIG_FILE_H_
#define CONFIG_FILE_H_

#include <glib.h>

/* Caractère d'attente lu dans .gtktermrc : 1-255 tel quel ; les anciennes valeurs négatives (char signé, ex. -23 pour 0xE9)
 * sont ramenées à 0-255 ; 0 et -1 signifient « aucun » (-1). */
gint config_wait_char_from_file(gint valeur);

/* crée config_dir, migre home_dir/.gtktermrc, renvoie le chemin (à libérer) */
gchar *config_file_prepare(const gchar *config_dir, const gchar *home_dir);

#endif
```

Dans `src/config_file.c`, supprimer tout le reste (`reserved_names`, `config_file_is_reserved_name`, `section_name`, `split_reserved`, profils parsecfg, `remove_section`, strip/restore) et l'inclusion de `parsecfg.h`. Dans `tests/test_config_file.c`, ne garder que les cas qui portent sur `config_wait_char_from_file` et `config_file_prepare` (`/config_file/preparation-cree-le-repertoire-et-migre`, le cas du caractère d'attente s'il y est) ; supprimer les autres (leur comportement est couvert par `test_config_store`).

- [ ] **Step 2 : `term_config.c` sur le magasin**

1. Supprimer `#include "parsecfg.h"`, `cfgStruct cfg[]`, tous les tableaux parsecfg (`port`, `speed`, … `update_prefix_cfg`), `GFile *config_file`, `Copy_configuration`, `save_config_silent`, `get_config_file_path`, `Save_config_file` garde son nom (dialogue). Ajouter `#include "config_store.h"`.
2. État :

```c
static GtConfigStore *store = NULL;
static gchar *store_load_error = NULL;	/* message à afficher une fois la fenêtre créée */
static gboolean store_file_existed = FALSE;
static gchar *active_config_name = NULL;	/* nom du profil chargé ; « default » par défaut */

static const gchar *active_name(void)
{
	return active_config_name != NULL ? active_config_name : "default";
}
```

3. Chargement :

```c
void config_file_init(void)
{
	/* $XDG_CONFIG_HOME/.gtktermrc ; ancien $HOME/.gtktermrc migré (config_file_prepare) */
	gchar *path = config_file_prepare(g_get_user_config_dir(), g_get_home_dir());
	GError *error = NULL;

	store_file_existed = g_file_test(path, G_FILE_TEST_EXISTS);
	store = gt_config_store_load(path, &error);
	if(error != NULL)
	{
		store_load_error = g_strdup_printf(_("Cannot read configuration file %s: %s\nThe default configuration is used and nothing will be written to this file."),
		                                   path, error->message);
		g_error_free(error);
	}
	g_free(path);
}

GtConfigStore *config_get_store(void)
{
	return store;
}

gboolean config_save_store(gboolean interactive)
{
	GError *error = NULL;

	if(gt_config_store_save(store, &error))
		return TRUE;
	if(interactive)
		show_message(error->message, MSG_ERR);
	else
		g_printerr("%s\n", error->message);
	g_error_free(error);
	return FALSE;
}

static void settings_load(void)
{
	g_free(settings.update_url);
	settings.update_url = gt_config_store_get_general_string(store, "update_url", GTKTERM_DEFAULT_UPDATE_URL);
	g_free(settings.update_prefix);
	settings.update_prefix = gt_config_store_get_general_string(store, "update_prefix", GTKTERM_DEFAULT_UPDATE_PREFIX);
	settings.update_check_startup = gt_config_store_get_general_boolean(store, "update_check_startup", TRUE);
}

void config_save_settings(void)
{
	gt_config_store_set_general_string(store, "update_url", settings.update_url);
	gt_config_store_set_general_string(store, "update_prefix", settings.update_prefix);
	gt_config_store_set_general_boolean(store, "update_check_startup", settings.update_check_startup);
	config_save_store(TRUE);
}

void config_remember_macros_file(void)
{
	gt_profile_set_string(&config.macros_file, macros_file_get_path());
	gt_config_store_set_profile_string(store, active_name(), "macros_file", config.macros_file);
	config_save_store(TRUE);
}
```

4. Démarrage (appelé en tête de `read_command_line`) :

```c
gint Check_configuration_file(void)
{
	Hard_default_configuration();
	settings_load();

	if(store_load_error != NULL)
	{
		show_message(store_load_error, MSG_ERR);
		g_clear_pointer(&store_load_error, g_free);
		return -1;
	}

	if(!store_file_existed)
	{
		gchar *string = g_strdup_printf(_("Configuration file (%s) with\n[default] configuration has been created.\n"), gt_config_store_get_path(store));

		gt_config_store_put_profile(store, "default", &config);
		if(config_save_store(TRUE))
			show_message(string, MSG_WRN);
		g_free(string);
		store_file_existed = TRUE;
		return 0;
	}

	return Load_configuration_from_file("default") == 0 ? 0 : -1;
}
```

5. Chargement d'un profil :

```c
gint Load_configuration_from_file(const gchar *config_name)
{
	GtProfile loaded = { 0 };

	if(!gt_config_store_get_profile(store, config_name, &loaded))
	{
		gchar *string = g_strdup_printf(_("No section \"%s\" in configuration file\n"), config_name);

		show_message(string, MSG_ERR);
		g_free(string);
		return -1;
	}

	gt_profile_copy(&config, &loaded);
	gt_profile_clear(&loaded);
	g_free(active_config_name);
	active_config_name = g_strdup(config_name);

	/* Fichier de macros du profil ; sans fichier, celui en cours est gardé */
	if(config.macros_file != NULL)
		macros_file_set_path(config.macros_file);
	macros_file_load(NULL);
	add_shortcuts();
	rebuild_macro_buttons();

	config_apply_to_terminal();
	return 0;
}
```

6. Dialogues de profils :
- `Select_config` : remplacer l'appel à `config_file_parse_profiles` et la boucle `cfgSectionNumberToName` par :

```c
	gchar **names = gt_config_store_list_profiles(store);
	...
	for(i = 0; names[i] != NULL; i++)
	{
		gtk_list_store_append(Modele_Liste, &iter_Liste);
		gtk_list_store_set(Modele_Liste, &iter_Liste, N_texte, names[i], -1);
	}
	g_strfreev(names);
```

  (supprimer la branche d'erreur « Cannot read configuration file! » de cette fonction et les variables `max`).
- `save_config` : supprimer la lecture parsecfg et la vérification des noms réservés ; la confirmation de remplacement devient `if(gt_config_store_has_profile(store, config_name))` ; message de confirmation inchangé.
- `really_save_config` :

```c
void really_save_config(GtkDialog *Fenetre, gint id, gpointer data)
{
	const gchar *name = data;

	if(id != GTK_RESPONSE_ACCEPT)
	{
		Save_config_file();
		return;
	}
	/* Le profil enregistré prend le fichier de macros en cours */
	gt_profile_set_string(&config.macros_file, macros_file_get_path());
	gt_config_store_put_profile(store, name, &config);
	if(config_save_store(TRUE))
	{
		g_free(active_config_name);
		active_config_name = g_strdup(name);
	}
}
```

- `load_config` : inchangé (appelle `Load_configuration_from_file(txt)`, `Verify_configuration()`, `interface_open_port()`, `ConfigFlags()`, `add_shortcuts()`) ; libérer `txt` après usage (`g_free(txt)`).
- `delete_config` :

```c
			gtk_tree_model_get(GTK_TREE_MODEL(Modele), &iter, 0, &txt, -1);
			if(!gt_config_store_delete_profile(store, txt))
				show_message(_("Cannot delete section!"), MSG_ERR);
			else
				config_save_store(TRUE);
			g_free(txt);
```

7. Fichiers de macros (`load_macros_file_callback`, `save_macros_file_callback`) : remplacer `save_config_silent();` par `config_remember_macros_file();`.
8. État de la fenêtre :

```c
void save_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean script_visible)
{
	GKeyFile *kf = gt_config_store_get_key_file(store);
	gint x, y, w, h;

	gtk_window_get_position(GTK_WINDOW(window), &x, &y);
	gtk_window_get_size(GTK_WINDOW(window), &w, &h);
	g_key_file_set_integer(kf, "window", "x", x);
	g_key_file_set_integer(kf, "window", "y", y);
	g_key_file_set_integer(kf, "window", "width", w);
	g_key_file_set_integer(kf, "window", "height", h);
	g_key_file_set_integer(kf, "window", "paned_position", gtk_paned_get_position(GTK_PANED(paned)));
	g_key_file_set_boolean(kf, "window", "script_panel_visible", script_visible);
	if(script_paned)
		g_key_file_set_integer(kf, "window", "v_paned_position", gtk_paned_get_position(GTK_PANED(script_paned)));
	if(!gt_config_store_is_read_only(store))
		config_save_store(FALSE);
}
```

  et dans `load_window_state`, remplacer le chargement du fichier par `GKeyFile *kf = gt_config_store_get_key_file(store);` (sans `g_key_file_free` à la fin).
9. `gtkterm.c` : `config_file_init()` reste en tête de `main` (avant `gtk_init` : il ne fait pas d'interface) ; à la sortie, rien à faire (le magasin vit jusqu'à la fin du processus).

- [ ] **Step 3 : autres appelants**

- `interface.c`, dialogue À propos : remplacer `save_config_silent();` par `config_save_settings();`.
- `macro_panel.c`, `save_arg_from_widget` : remplacer `save_config_silent();` par `config_remember_macros_file();`.
- `script_panel.c` : `load_script_colors` lit `GKeyFile *kf = gt_config_store_get_key_file(config_get_store());` (plus de chargement ni de `g_key_file_free`) ; `save_script_colors` écrit dans ce même `kf` puis, si `!gt_config_store_is_read_only(config_get_store())`, appelle `config_save_store(FALSE)` (supprimer le chargement, la détection M12 locale, `g_key_file_to_data`, `g_file_set_contents` et `g_key_file_free`). Le comportement « couleurs de session » (`sc_has_custom_bg/fg`) est conservé.
- `cmdline.c` : `Check_configuration_file()` et `Load_configuration_from_file(optarg)` gardent leur place.

- [ ] **Step 4 : supprimer parsecfg**

```bash
git rm src/parsecfg.c src/parsecfg.h tests/test_parsecfg.c tests/lsan.supp
```

- `src/meson.build` : retirer `'parsecfg.c'`, `'parsecfg.h'` de `core_sources`.
- `tests/meson.build` : retirer `'test_parsecfg' : [],` ; retirer la ligne `test_env.set('LSAN_OPTIONS', …)` et son commentaire (plus de suppression nécessaire).
- `po/POTFILES.in` : retirer `src/parsecfg.c`, ajouter `src/config_store.c` et `src/profile.c` (ordre alphabétique).
- `tests/README.md` : retirer la mention de `lsan.supp` s'il y en a une.

- [ ] **Step 5 : construction, suites, vérifications**

Run : `ninja -C build 2>&1 | grep -E 'error|warning' ; meson test -C build --print-errorlogs`
Expected : aucune erreur ; aucun échec.
Run : `grep -rn 'parsecfg\|cfgParse\|cfgDump\|cfgStruct\|save_config_silent\|get_config_file_path' src tests po meson.build`
Expected : aucune sortie.
Run : `grep -nE 'g_key_file_load_from_file|g_key_file_save_to_file|g_file_set_contents' src/*.c`
Expected : seules restent les lignes de `src/config_store.c` et celles qui écrivent **d'autres** fichiers (`macros.c` pour le fichier de macros, `script_panel.c` pour le schéma de couleurs et les scripts, `files.c`/`logging.c` s'il y en a) — aucune sur `.gtktermrc`.
Run : `test -d build-asan || meson setup build-asan -Db_sanitize=address,undefined ; meson test -C build-asan --print-errorlogs 2>&1 | tail -4`
Expected : `Fail: 0`, aucun rapport de fuite (la suppression LSan de parsecfg n'existe plus).
Run : `meson test -C build --suite gui --print-errorlogs 2>&1 | tail -4`
Expected : `Ok: 11`, `Fail: 0` (chaque scénario démarre sur un `.gtktermrc` à l'ancien format : migration au démarrage).

- [ ] **Step 6 : commit**

```bash
git add -A src tests po
git commit -m "configuration : persistance par le magasin GKeyFile, parsecfg supprimé

Profils [profile:nom], réglages [general], migration des anciens fichiers avec copie de sauvegarde, magasin en lecture
seule si le fichier est illisible ; état de la fenêtre et panneau de scripts passent par le magasin ; écritures
partielles pour le fichier de macros et l'URL de mise à jour (les options de ligne de commande ne sont plus enregistrées).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6 : scénarios graphiques, traductions, documentation, acceptation

**Files :**
- Create : `tests/gui/test_config_migration.py`, `tests/gui/test_config_profils.py`, `tests/gui/test_config_ligne_commande.py`
- Modify : `tests/gui/meson.build` (`gui_scenarios`), `po/fr.po`, `CLAUDE.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md`

**Interfaces :**
- Consumes : application complète (tâches 4-5), harnais `tests/gui/gtktest.py` (`Env`, `write_rc`, `profile`, `write_macros`, `start`, `menu`, `menu_key`, `menu_click`, `file_dialog`, `focus_window`, `wait_window`, `find1`, `act`, `set_text`, `check`, `wait_until`, `status_text`, `finish`, `conclude`, `Broadway`).

Les dialogues « Save configuration », « Load configuration », « Delete configuration » sont des `GtkDialog` à réponse par rappel (non modaux) ; la confirmation de remplacement passe par `gtk_dialog_run` : pour la déclencher, cliquer le bouton OK par un vrai clic broadway (`bw.click_obj`), comme le fait `menu_click` dans le harnais. La liste des profils est un `GtkTreeView` (rôle AT-SPI `tree table` ou `table`) : sélectionner une ligne par `Atspi.Table.add_row_selection`, comme `.superpowers/banc-gui/em6_18.py` le faisait.

- [ ] **Step 1 : scénarios**

`tests/gui/test_config_migration.py` :

```python
# Migration d'un .gtktermrc à l'ancien format (parsecfg) : profil relu sans perte, fichier réécrit au nouveau format, copie de sauvegarde.
from gtktest import *

e = Env('config_migration')
ma = os.path.join(e.root, 'mes macros.ini')
e.write_macros(ma, [('MIG', 'None', 'MIG', '', '', 0, 1000)])
ancien = ('[default]\nport\t= %s\nspeed\t= 115200\nbits\t= 8\nstopbits\t= 1\nparity\t= none\nflow\t= none\nwait_delay\t= 0\nwait_char\t= -1\n'
          'echo\t= True\ncrlfauto\t= False\nautoreconnect_enabled\t= False\nesc_clear_screen\t= False\ntimestamp\t= False\n'
          'font\t= "Monospace 13"\nterm_rows\t= 80\nterm_columns\t= 25\nmacros_file\t= "%s"\ntransport_type\t= 0\n'
          'update_url\t= https://invalid.example/none.git\nupdate_check_startup\t= False\nupdate_prefix\t= %s/prefix\n\n'
          '[window]\nwidth=760\n') % (e.pty, ma, e.root)
e.write_rc(ancien)
e.start(force_port=False); time.sleep(2)
check(wait_until(lambda: status_text(e.app) == '%s  115200-8-N-1' % e.pty, 5), 'port du profil migre', status_text(e.app))
check(wait_until(lambda: find(e.app, 'push button', '^MIG$', showing=True), 5), 'macros chargees depuis un chemin avec espace')
act(macro_button(e.app, 'MIG')); time.sleep(0.5)
check(e.read_pty(1.0) == b'MIG', 'macro envoyee')
check(wait_until(lambda: '> MIG' in terminal_text(e.app), 3), 'echo du profil migre actif')
# l'URL de mise à jour (dialogue A propos) declenche une ecriture : le fichier passe au nouveau format
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
menu_key(bw, e.app, 'Help', 'About', focus_obj=term)
about = wait_window(e.app, 'About')
closeb = [b for b in find(about, 'push button', '^Close$')]
act(closeb[-1]); time.sleep(1.0)
contenu = open(e.rc).read()
check('[profile:default]' in contenu and '[general]' in contenu, 'fichier reecrit au nouveau format', contenu[:300])
check('font=Monospace 13' in contenu, 'police sans guillemets')
check('macros_file=%s' % ma in contenu, 'fichier de macros conserve')
check('term_columns=80' in contenu and 'term_rows=25' in contenu, 'colonnes et lignes remises a l endroit')
check('[window]' in contenu, 'groupe [window] conserve')
check(os.path.exists(e.rc + '.avant-migration') and open(e.rc + '.avant-migration').read() == ancien, 'copie de sauvegarde identique a l original')
finish(e)
sys.exit(conclude())
```

`tests/gui/test_config_profils.py` :

```python
# Profils par les dialogues : enregistrer sous un nouveau nom, recharger, supprimer.
from gtktest import *

def ouvrir_liste(e, bw, term, titre):
    menu_key(bw, e.app, 'Configuration', titre, focus_obj=term)
    w = wait_window(e.app, titre)
    time.sleep(0.5)
    return w

def lignes(w):
    tbl = [t for t in find(w, None) if role(t) in ('tree table', 'table')][0]
    return tbl, [name(Atspi.Table.get_accessible_at(tbl, r, 0)) or get_text(Atspi.Table.get_accessible_at(tbl, r, 0)) for r in range(Atspi.Table.get_n_rows(tbl))]

e = Env('config_profils')
e.write_rc(e.profile(echo='True'))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')

# 1. enregistrer sous « atelier »
menu_key(bw, e.app, 'Configuration', 'Save configuration', focus_obj=term)
w = wait_window(e.app, 'Save configuration')
ent = find1(w, 'text', showing=True)
set_text(ent, 'atelier'); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.0)
contenu = open(e.rc).read()
check('[profile:atelier]' in contenu, 'profil atelier enregistre', contenu[:400])
check('[profile:default]' in contenu, 'profil default conserve')

# 2. recharger « atelier »
w = ouvrir_liste(e, bw, term, 'Load configuration')
tbl, noms = lignes(w)
check('atelier' in noms and 'default' in noms, 'liste des profils', noms)
Atspi.Table.add_row_selection(tbl, noms.index('atelier')); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.5)
check(wait_until(lambda: status_text(e.app) == '%s  115200-8-N-1' % e.pty, 5), 'profil atelier recharge', status_text(e.app))

# 3. supprimer « atelier »
w = ouvrir_liste(e, bw, term, 'Delete configuration')
tbl, noms = lignes(w)
Atspi.Table.add_row_selection(tbl, noms.index('atelier')); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.0)
contenu = open(e.rc).read()
check('[profile:atelier]' not in contenu and '[profile:default]' in contenu, 'profil atelier supprime', contenu[:400])
check(alerts(e.app) == [], 'aucune boite d erreur')
finish(e)
sys.exit(conclude())
```

`tests/gui/test_config_ligne_commande.py` :

```python
# Les options de ligne de commande ne sont pas enregistrées par une écriture partielle (fin de M10).
from gtktest import *

e = Env('config_ligne_commande')
autre = os.path.join(e.root, 'port-du-profil')
os.symlink(e.pty, autre)
ma = os.path.join(e.root, 'macros2.ini')
e.write_macros(ma, [('M2', 'None', 'M2', '', '', 0, 1000)])
e.write_rc(e.profile(port=autre))
e.start(args=['--port', e.pty], force_port=False); time.sleep(2)
check(wait_until(lambda: status_text(e.app).startswith(e.pty), 5), 'port de la ligne de commande utilise', status_text(e.app))
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
menu_key(bw, e.app, 'Configuration', 'Load macros file...', focus_obj=term)
file_dialog(bw, e.app, 'Load macros file', ma)
check(wait_until(lambda: find(e.app, 'push button', '^M2$', showing=True), 5), 'fichier de macros charge')
contenu = open(e.rc).read()
check('port=%s' % autre in contenu, 'le profil garde son port (pas celui de --port)', contenu[:400])
check('macros_file=%s' % ma in contenu, 'chemin du fichier de macros enregistre')
finish(e)
sys.exit(conclude())
```

Ajouter les trois noms à `gui_scenarios` dans `tests/gui/meson.build`. Si un pilotage (titre exact d'un dialogue, rôle AT-SPI de la liste, nom du bouton « OK » avec mnémonique) diffère, l'ajuster sans affaiblir ce que le scénario vérifie, et le dire dans le rapport.

- [ ] **Step 2 : lancer la suite graphique**

Run : `ninja -C build && meson test -C build --suite gui --print-errorlogs 2>&1 | tail -6`
Expected : `Ok: 14`, `Fail: 0`. En cas d'échec, lire `build/tests/gui/runs/<scénario>/result.txt` et `gtkterm.stderr` ; corriger le produit si c'est lui (avec un test unitaire qui échoue d'abord quand c'est possible), sinon le pilotage.

- [ ] **Step 3 : traductions**

Ajouter à `po/fr.po` (vérifier d'abord avec `grep -n` que chaque identifiant est exactement celui des sources, et qu'il n'existe pas déjà) :

```po
#: ../src/config_store.c
#, c-format
msgid "The configuration file %s could not be read: nothing has been written to it."
msgstr "Le fichier de configuration %s n'a pas pu être lu : rien n'y a été écrit."

#: ../src/config_store.c
#, c-format
msgid "Cannot create directory %s: %s"
msgstr "Impossible de créer le dossier %s : %s"

#: ../src/term_config.c
#, c-format
msgid ""
"Cannot read configuration file %s: %s\n"
"The default configuration is used and nothing will be written to this file."
msgstr ""
"Impossible de lire le fichier de configuration %s : %s\n"
"La configuration par défaut est utilisée et rien ne sera écrit dans ce fichier."
```

Run : `msgfmt --check -o /dev/null po/fr.po` — Expected : aucune sortie.

- [ ] **Step 4 : documentation**

- `CLAUDE.md`, paragraphe **Config** : remplacer la description parsecfg / groupes retirés et remis par : « **Config (`config_store.c` + `profile.c` + `term_config.c`):** `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`) is read and written only by `GtConfigStore` (GKeyFile): `[general]` (update settings), `[profile:NAME]` (one `GtProfile` each), and the owner groups `[window]` (window state, `term_config.c`), `[colors]`, `[layout]`, `[session]` (`script_panel.c`), which access their groups through `gt_config_store_get_key_file()` then `config_save_store()`. Old parsecfg files are migrated in memory on load, with a one-time backup `.gtktermrc.avant-migration`. The running profile is the global `GtProfile config` (command-line options apply to it only); only explicit saves write a whole profile, and loading a macros file or editing the update URL write a single key. An unreadable file makes the store read-only: nothing is ever written over it. » ; retirer `parsecfg.c` de la liste des membres de `gtkterm_core` et ajouter `profile.c`, `config_store.c` ; retirer la phrase sur les défauts C5/C13 « sub-project 3 replaces parsecfg ».
- `tests/README.md` : ajouter `test_profile` et `test_config_store` à la liste des tests unitaires, retirer `test_parsecfg` ; retirer toute mention de `lsan.supp` ; ajouter les trois scénarios graphiques à la liste de la suite graphique s'il y en a une.
- Revue (`docs/superpowers/reviews/2026-10-06-revue-code.md`) : ajouter sous C5, C6, C7, C13, M9, M10, M11, M12, M13, M14 la mention « Corrigé par le sous-projet 3 (`feature/refonte-configuration`) » ; dans l'annexe du sous-projet 1, marquer traitées les remarques de la tâche 6 (points 1 à 7) et, dans les tâches 14/15, « autres écrivains de .gtktermrc » et « get_config_file_path fuit ».

- [ ] **Step 5 : critères d'acceptation**

Run :

```bash
grep -rn 'parsecfg' src tests po meson.build CLAUDE.md
grep -nE '#include *[<"](gtk/|vte/|interface\.h|term_config\.h)' src/profile.c src/config_store.c
git diff master -- src/cmdline.c | grep -E '^[-+].*\{ *"[a-z-]+", *[0-9]'
meson test -C build --print-errorlogs 2>&1 | tail -4
meson test -C build-asan --print-errorlogs 2>&1 | tail -4
meson test -C build --suite gui --print-errorlogs 2>&1 | tail -4
```

Expected : les trois premières commandes sans sortie (aucune option de ligne de commande changée) ; les trois suites `Fail: 0` (gui : `Ok: 14`).

- [ ] **Step 6 : commit**

```bash
git add tests/gui po/fr.po CLAUDE.md tests/README.md docs/superpowers/reviews/2026-10-06-revue-code.md
git commit -m "tests : scénarios graphiques de la configuration (migration, profils, ligne de commande) ; traductions ; documentation

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
