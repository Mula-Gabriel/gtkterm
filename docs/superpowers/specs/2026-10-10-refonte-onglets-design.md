# Sous-projet 6b — Onglets : une instance complète de GTKTerm par onglet

- **Date** : 2026-10-10
- **Branche** : `feature/refonte-onglets` (fusionnée `--no-ff` dans `master`, écrasée en 3 commits thématiques, puis supprimée en local et sur Maison)
- **Entrées** : spec 6a `docs/superpowers/specs/2026-10-09-refonte-gtk4-design.md` (§3.2-4 : « en 6b, les actions viseront l'onglet actif ») ; revue
  `docs/superpowers/reviews/2026-10-06-revue-code.md`, annexe « Remarques mineures reportées par le sous-projet 6 » ; master au merge `a48d0d8`.
- **Décisions** : celles de l'utilisateur au cadrage (§3.1) ; les autres prises en autonomie (consigne de l'utilisateur pour le chantier), §3.2.

## Contexte

6a a basculé GTKTerm sur GTK 4 avec un état d'application unique : une liaison par défaut (`gt_link_get_default()`), un profil en cours global
(`GtProfile config`), un `GtTermOptions`, un jeu de macros (`macros.c`, état statique), un moteur Lua (`script_engine.c`, 25 variables statiques, un seul
thread), un journal, un envoi de fichier, et une fenêtre dont les actions `app.*` visent ces singletons. 6b ajoute les onglets ; 6c ajoutera les projets.

## Objectif

**Un onglet est une instance complète et indépendante de GTKTerm, présentée dans un onglet de la fenêtre** (consigne de l'utilisateur) : son port et sa
liaison, son profil, ses options d'affichage, son terminal, son jeu de macros (avec son propre fichier), son panneau de macros et ses raccourcis, son moteur
Lua et son panneau de script, son journal, son envoi de fichier, ses barres (recherche, envoi hexadécimal, état). Seuls sont communs : la fenêtre et la barre
de menus, le magasin de configuration (`.gtktermrc`, `[general]`, `[window]`, `[colors]`), la vérification de mise à jour.

Les actions `app.*` (menus, raccourcis, D-Bus) visent l'onglet actif.

## Hors périmètre

- Rouvrir au démarrage les onglets de la dernière session (décision utilisateur, §3.1-4) : ce sera le rôle des projets (6c).
- Plusieurs fenêtres ; détacher un onglet dans une fenêtre ; glisser un onglet entre fenêtres.
- Partage d'un même fichier de macros entre onglets ou instances (deux onglets sur le même fichier : chacun son jeu, le dernier enregistrement gagne, comme
  deux instances aujourd'hui) : 6c.
- Les remarques reportées de la revue qui ne touchent pas aux onglets (annexes SP3/SP4 du magasin et de `macro_file.c` : 6c ; harnais, cœur, ligne de
  commande : au fil de l'eau).

## 3. Décisions

### 3.1 Prises par l'utilisateur au cadrage (2026-10-10)

1. **Un onglet = une instance parallèle du même logiciel.** Le panneau de macros dépend entièrement de son onglet ; chaque onglet peut avoir un fichier de
   macros différent.
2. **Polling** : une macro en polling envoie sur la liaison de son onglet, quel que soit l'onglet affiché ; fermer l'onglet arrête ses pollings.
3. **Lua** : le port 0 d'un script est la liaison de l'onglet dont le panneau a lancé le script, pour toute l'exécution ; chaque onglet a son moteur.
4. **Nouvel onglet** : « New tab » ouvre le dialogue Port prérempli avec les réglages de l'onglet actif ; Annuler ne crée rien. La barre d'onglets
   n'apparaît qu'à partir de deux onglets (avec un seul, GTKTerm ressemble à 6a). Le dernier onglet ne se ferme pas.
5. **Pas de persistance des onglets** en 6b : le démarrage ouvre un seul onglet, comme aujourd'hui.

### 3.2 Prises en autonomie

1. **Une seule branche** (pas de 6b-1/6b-2) : le plan découpe en deux phases, chacune gardant la suite verte — (A) couche application multi-instances avec
   un seul onglet, sans changement visible ; (B) interface à onglets.
2. **`GtSession`** (couche application, GLib) porte tout l'état d'un onglet ; **`GtSessionManager`** (couche application) les ordonne et tient l'onglet actif.
   Les singletons disparaissent : `gt_link_get_default()`/`gt_link_set_default()`, la globale `GtProfile config`, l'état statique de `macros.c` et de
   `script_engine.c`. Aucun code ne retrouve « l'onglet actif » par une globale cachée : il le reçoit ou le demande au gestionnaire.
3. **Actions par onglet** : chaque session possède un `GSimpleActionGroup` (« actions d'onglet ») qui contient toutes les actions dont l'effet dépend de
   l'onglet, sous leur nom actuel (`local-echo`, `open-port`, `copy`, `log-start`, `macros`, …). La couche application y met celles qui ne dépendent que de
   GLib (`GPropertyAction` sur `GtTermOptions` et sur la liaison, port, journal, envoi de fichier) ; l'interface y ajoute celles qui touchent des widgets de
   la page (copier, coller, tout sélectionner, rechercher, afficher les panneaux). L'application expose chaque nom en `app.<nom>` par un **relais**
   (`GtActionRelay`, GIO, §5.3) qui délègue à l'action de même nom de la session active et relaie état, type et sensibilité ; au changement d'onglet, le relais
   émet les changements d'état et de sensibilité. Les noms `app.*` de 6a sont tous conservés (menus, raccourcis, harnais D-Bus inchangés).
4. **Crochets `GtAppUi`** : chaque crochet reçoit la session concernée (`status_flash`, `macros_changed`, `macro_polling_changed`, `profile_applied`) ;
   `message` reste global (boîte sur la fenêtre). `gt_app_status_flash()` devient `gt_session_status_flash(session, …)`.
5. **Moteur Lua par instance** : `script_engine.c` devient l'objet `GtScriptEngine` (un thread Lua, ses files, ses ports 1-15, son journal, son drapeau
   d'arrêt) ; le contrat de 6a/SP5 est inchangé pour chaque moteur. Le thread principal reste unique (`run_on_main_sync` garde un seul « fil principal » noté
   par `script_engine_setup()`). Deux scripts de deux onglets tournent en parallèle ; un port série ouvert par l'un est refusé à l'autre (verrou exclusif du
   transport, comme aujourd'hui entre deux instances).
6. **Macros par instance** : `macros.c` devient l'objet `GtMacros` (jeu, chemin, ordonnanceur, dernier échec d'enregistrement, liaison cible) ;
   `macros_send()` et `macros_send_quiet()` prennent l'instance. L'ordonnanceur d'un onglet envoie sur la liaison de son onglet (§3.1-2).
7. **Profil par onglet** : `GtSession` possède son `GtProfile` et le nom de son profil actif. « Load/Save/Delete configuration » agissent sur l'onglet actif ;
   charger un profil dans un onglet charge son fichier de macros dans cet onglet seulement. Le `macros_file` mémorisé l'est dans le profil actif **de l'onglet**.
   Les options de ligne de commande s'appliquent au premier onglet seulement.
8. **Page d'onglet** (`src/ui/session_view.c`) : tout ce qui est aujourd'hui sous la barre de menus — barre de recherche, `GtkPaned` vertical [`GtkPaned`
   horizontal (terminal | panneau de macros) / panneau de script], barre d'envoi hexadécimal, barre d'état. La fenêtre principale ne garde que la barre de
   menus et un `GtkNotebook` (pas de libadwaita, §3.1-3 de 6a) dont la barre d'onglets n'est visible qu'à partir de deux pages.
9. **Étiquette d'onglet** : nom court de la liaison (nom de base du périphérique série, `hôte:port` en client TCP, `:port` en serveur TCP) + bouton de
   fermeture (désactivé s'il n'y a qu'un onglet) ; infobulle = texte d'état complet ; classe CSS selon l'état (`lost` en italique). Onglets réordonnables
   à la souris (`gtk_notebook_set_tab_reorderable`). Le titre de la fenêtre suit l'onglet actif (« GTKTerm - <texte d'état> »).
10. **Nouvelles actions** : `app.new-tab` (Ctrl+Maj+T), `app.close-tab` (Ctrl+Maj+W, désactivée avec un seul onglet), `app.next-tab` (Ctrl+Maj+Page suivante),
    `app.previous-tab` (Ctrl+Maj+Page précédente), `app.active-tab` (à état `i` : index de l'onglet actif, `SetState` change d'onglet ; pour le harnais et un
    futur menu). Entrées de menu : File › « New tab », « Close tab ». Aucun raccourci sans Maj (règle de 6a : Ctrl+lettre et Ctrl+Page part sur le port).
11. **Fermer un onglet** : demande confirmation (`gt_ui_confirm`) seulement si un script tourne ou si un fichier est en cours d'envoi dans cet onglet ; puis
    arrêt dans l'ordre de 6a pour cette session : envoi de fichier et journal, moteur Lua, panneau et ordonnanceur de macros, liaison. Fermer la fenêtre arrête
    toutes les sessions dans le même ordre, sans confirmation (comportement de 6a).
12. **Dialogues** : le dialogue Port, le dialogue Terminal, « Configure Macros » et les dialogues de profils agissent sur la session active **au moment de
    leur ouverture** et la gardent (référence faible) ; si cette session est fermée pendant que le dialogue est ouvert, le dialogue se ferme sans rien appliquer.
    « Configure Macros » : une instance par onglet, titre « Configure Macros — <nom court> ». Port et Terminal restent modaux, une seule instance (C12).
    « New tab » réutilise le dialogue Port en mode création : OK crée la session, applique les réglages, l'active et ouvre son port.
13. **État de fenêtre** (`[window]`) : taille et maximisation sont celles de la fenêtre ; positions des séparateurs et visibilité des panneaux (macros, script,
    barre hexadécimale) sont par page : la première page les lit dans `[window]`, une nouvelle page copie celles de la page active, la fermeture enregistre
    celles de la page active. **Taille par défaut** portée à 1000 × 600 (remarque reportée de 6a : à 750 px, une ligne hexadécimale de 16 octets se replie).
14. **Panneau de script** : `[colors]` reste commun (préférence d'apparence) ; `[layout]` et `[session] script_path` sont lus par la première page,
    copiés de la page active par une nouvelle page (disposition seulement, éditeur vide) et enregistrés depuis la page active à la fermeture.
15. **SIGUSR1/SIGUSR2** : ouvrent / ferment le port de **toutes** les sessions (le processus reçoit le signal, pas un onglet).
16. **Envoi de fichier, journal, captures** : par session ; la fenêtre de progression d'envoi porte le nom court de l'onglet dans son titre. Le menu Journal
    reflète le journal de l'onglet actif (via le relais).
17. **Crochet de test** : `app.test-dump` inchangé (tous les widgets visibles, avec leur drapeau `mapped` ; ceux des pages non affichées d'un `GtkNotebook`
    sont visibles mais non mappés) ; les recherches du harnais (`find`, `find1`) ignorent par défaut les widgets non mappés (option pour les inclure), si bien
    qu'un nom stable (« terminal », « macro-panel »…) désigne celui de l'onglet affiché ; `app.test-activate` n'agissait déjà que sur un widget mappé ;
    `app.test-terminal-text` et `app.test-terminal-props` visent l'onglet actif ; nouvel état `app.test-sessions` (liste des sessions : index, nom court,
    état de liaison, fichier de macros) pour vérifier l'indépendance sans dépendre de l'affichage.

## 4. Architecture

### 4.1 Couches

| Couche | Nouveaux fichiers / changements | Rôle |
|---|---|---|
| Cœur (`gtkterm_core`) | `action_relay.c` (`GtActionRelay`, GIO seul) | relais d'action vers un `GActionGroup` cible changeant |
| Application (`gtkterm_app`) | **`session.c`** (`GtSession`), **`session_manager.c`** (`GtSessionManager`) ; `macros.c` → `GtMacros` ; `script_engine.c` → `GtScriptEngine` ; `app_config.c` sans `config` global ; `app_ui.h` avec session ; `link.c` sans liaison par défaut ; `cmdline.c` remplit le profil de la première session | état par onglet, sans GTK |
| Interface (`src/ui/`) | **`session_view.c`** (page), **`tab_label.c`** ; `main_window.c` = menus + `GtkNotebook` ; `app.c` (relais, onglets, fermeture) ; modules de page rendus multi-instances (`status_bar.c`, `search_bar.c`, `hex_send_bar.c`, `macro_panel.c`, `macro_shortcuts.c`, `macro_editor.c`, `script_panel.c`, `file_actions.c`, `terminal_view.c`) | vues ; aucune globale d'onglet |

Règles de 6a inchangées (aucun `gtk/gtk.h` hors de `src/ui/`, version GTK imposée par le compilateur, pas de boucle imbriquée). Nouvelle règle : **aucune
variable statique ne porte d'état d'onglet** (vérifiée par relecture ; les seules statiques permises sont les états réellement uniques : magasin, réglages
`[general]`, file des messages, fil principal de `run_on_main_sync`, instance unique des dialogues modaux).

### 4.2 `GtSession` (couche application)

```c
#define GT_TYPE_SESSION (gt_session_get_type())
G_DECLARE_FINAL_TYPE(GtSession, gt_session, GT, SESSION, GObject)

/* Nouvelle session fermée : liaison, options, profil (copie de profile, ou défauts si NULL), macros (jeu vide), moteur Lua, journal, actions d'onglet.
 * Aucun port ouvert, aucun fichier de macros chargé : voir gt_session_apply_profile / gt_session_open_port. */
GtSession        *gt_session_new(const GtProfile *profile, const gchar *profile_name);
guint             gt_session_get_id(GtSession *s);	/* stable, jamais réutilisé dans le processus */
GtkTermLink      *gt_session_get_link(GtSession *s);
GtTermOptions    *gt_session_get_options(GtSession *s);
GtProfile        *gt_session_get_profile(GtSession *s);	/* profil en cours de l'onglet, modifiable par les dialogues */
const gchar      *gt_session_get_profile_name(GtSession *s);	/* « default » par défaut */
GtMacros         *gt_session_get_macros(GtSession *s);
GtScriptEngine   *gt_session_get_script_engine(GtSession *s);
GtRxLogger       *gt_session_get_logger(GtSession *s);
GActionGroup     *gt_session_get_actions(GtSession *s);	/* GSimpleActionGroup : l'interface y ajoute ses actions de page */
gchar            *gt_session_dup_short_name(GtSession *s);	/* « ttyUSB0 », « localhost:2323 », « :2323 » */
gboolean          gt_session_is_busy(GtSession *s);	/* script en cours ou envoi de fichier en cours : confirmation avant fermeture */
void              gt_session_apply_profile(GtSession *s);	/* options ← profil, auto-reconnect, crochet profile_applied ; ne rouvre pas le port */
void              gt_session_open_port(GtSession *s);	/* port du profil ; échec : open-failed de la liaison */
void              gt_session_close_port(GtSession *s);
void              gt_session_shutdown(GtSession *s);	/* arrêt ordonné (§3.2-11), idempotent ; la session reste un objet valide mais inerte */
/* Propriétés notifiées : « short-name », « status-text » (relais de la liaison), « busy ». Signal « closed » à la fin de gt_session_shutdown. */
```

Le profil chargé par `app_config_load_profile(session, name, …)` est copié dans la session ; `app_config_save_profile(session, name, …)` écrit le profil de
la session (`gt_term_options_store_profile` d'abord). `config_remember_macros_file(session)` écrit la clé `macros_file` du profil actif de la session.

### 4.3 `GtSessionManager` (couche application)

```c
/* Liste ordonnée des sessions (GListModel de GtSession) et session active. Fil principal seulement. */
GtSessionManager *gt_session_manager_new(void);
GListModel       *gt_session_manager_get_model(GtSessionManager *m);	/* ordre des onglets ; l'interface le suit (ajout, retrait, déplacement) */
void              gt_session_manager_add(GtSessionManager *m, GtSession *s, gint position);	/* -1 : à la fin */
void              gt_session_manager_remove(GtSessionManager *m, GtSession *s);	/* après gt_session_shutdown ; la session active passe à la voisine (droite, sinon gauche) */
void              gt_session_manager_move(GtSessionManager *m, GtSession *s, guint position);
GtSession        *gt_session_manager_get_active(GtSessionManager *m);	/* jamais NULL dès qu'une session existe */
void              gt_session_manager_set_active(GtSessionManager *m, GtSession *s);
/* Propriété notifiée « active ». */
```

L'application (`src/ui/app.c`) possède le gestionnaire ; le moteur Lua, les macros et les dialogues ne le consultent pas : ils reçoivent leur session.

### 4.4 `GtActionRelay` (cœur, GIO)

`GtActionRelay` implémente `GAction` : nom, type de paramètre et type d'état fixés à la création (pris sur le modèle, une action de la première session) ;
`activate` / `change_state` délèguent à l'action de même nom du groupe cible (`g_action_group_activate_action`, `g_action_group_change_action_state`) ;
`enabled` et `state` reflètent ceux du groupe cible (signaux `action-enabled-changed`, `action-state-changed`, `action-added`, `action-removed` du groupe) ;
sans cible, ou si la cible n'a pas l'action, le relais est désactivé et garde son dernier état. `gt_action_relay_set_target(relay, group)` change de cible
et notifie `enabled` et `state` s'ils changent. L'application installe un relais `app.<nom>` par action d'onglet et change leur cible quand la session active
change. Testé unitairement sans affichage (`test_action_relay`).

### 4.5 Moteur Lua par instance (`GtScriptEngine`)

```c
GtScriptEngine *gt_script_engine_new(GtkTermLink *main_link, GtMacros *macros);	/* port 0 = main_link ; send_macro → macros */
void            gt_script_engine_run(GtScriptEngine *e, const char *script_text);
void            gt_script_engine_stop(GtScriptEngine *e);
gboolean        gt_script_engine_is_running(GtScriptEngine *e);
void            gt_script_engine_shutdown(GtScriptEngine *e);	/* contrat de script_engine_shutdown, par moteur */
void            gt_script_engine_set_log_func(GtScriptEngine *e, ScriptLogFunc func, gpointer user_data);
void            gt_script_engine_set_done_func(GtScriptEngine *e, ScriptDoneFunc func, gpointer user_data);
void            script_engine_setup(void);	/* inchangé : note le fil principal, une fois */
```

Tout l'état statique de `script_engine.c` passe dans la structure du moteur, retrouvée depuis l'état Lua (`lua_getextraspace`) et depuis les rappels
`run_on_main_sync` (pointeur passé). La file de demandes vers le fil principal reste unique (un seul fil principal) mais chaque demande porte son moteur.
Le moteur garde une référence sur sa liaison ; fermer l'onglet arrête le script (`gt_session_shutdown`) avant de fermer la liaison.

### 4.6 Interface

- **`main_window.c`** : barre de menus + `GtkNotebook` nommé « tabs » (barre visible si ≥ 2 pages, `gtk_notebook_set_show_tabs`) ; suit le modèle du
  gestionnaire (ajout, retrait, déplacement) et met à jour l'onglet actif dans les deux sens (`switch-page` ↔ propriété `active`).
- **`session_view.c`** : construit la page d'une session (les emplacements de 6a deviennent ceux de la page : « search-bar », « macro-panel »,
  « script-panel », « hex-bar », « status-bar ») ; ajoute au groupe d'actions de la session les actions de page ; `gt_session_view_get_state()` /
  `gt_session_view_set_state()` pour séparateurs et visibilités ; `gt_session_view_focus_terminal()`.
- **`tab_label.c`** : étiquette (nom court lié à `short-name`, infobulle liée à `status-text`, classe CSS d'état, bouton de fermeture → `app.close-tab` sur
  cette session).
- Les modules de 6a qui gardaient un état statique de fenêtre (`app_actions.c`, `file_actions.c`, `status_bar.c`, `search_bar.c`, `hex_send_bar.c`,
  `macro_panel.c`, `macro_shortcuts.c`, `macro_editor.c`, `script_panel.c`) deviennent par page : leur état vit dans un objet attaché à la page ou à la session
  (`g_object_set_data_full`), plus dans une statique.
- **Raccourcis de macros** : un `GtkShortcutController` par page (portée locale à la page, phase de capture) : seules les macros de l'onglet affiché répondent.

## 5. Flux

- **Démarrage** : `main()` crée le gestionnaire et une première session (profil par défaut), lit la ligne de commande dans le profil de cette session
  (profil `-c`, options, fichier de macros du profil chargé dans ses macros), puis lance l'application. `activate` : fenêtre, page de la première session,
  profil appliqué, port ouvert, SIGUSR1/2, vérification de mise à jour.
- **New tab** : dialogue Port en mode création, prérempli avec une copie du profil de la session active (nom de profil actif copié, fichier de macros du
  profil chargé dans la nouvelle session) ; OK → `gt_session_new` + `gt_session_manager_add` (à droite de l'active) + page + active + `apply_profile` +
  `open_port`. Port déjà tenu par un autre onglet : l'ouverture échoue par le verrou du transport → boîte d'erreur `open-failed`, l'onglet reste (port fermé,
  modifiable par Configuration › Port), comme une ouverture refusée aujourd'hui.
- **Changement d'onglet** : `active` change → relais reciblés (états et sensibilités des menus à jour), titre de la fenêtre, focus clavier au terminal de
  la page.
- **Fermer un onglet** : confirmation si occupé (§3.2-11) → `gt_session_shutdown` → page détruite → `gt_session_manager_remove`.
- **Fermer la fenêtre** : état de fenêtre de la page active enregistré une fois (comme 6a), puis chaque session arrêtée, dans l'ordre des onglets.

## 6. Erreurs et cas limites

- Rappel asynchrone (sélecteur de fichiers, boîte de confirmation, fin de script, polling) qui arrive après la fermeture de sa session : il retrouve une
  session inerte (`gt_session_shutdown` fait) ou un pointeur faible nul, et ne fait rien.
- Ouverture d'un même périphérique série dans deux onglets : refusée par le verrou (sauf `--disable-port-lock`, choix explicite de l'utilisateur).
- Deux serveurs TCP sur le même port : le second échoue (`open-failed`), comme deux instances.
- Polling en cours dans un onglet non affiché : continue ; le clignotement de son bouton n'est simplement pas visible.
- Script qui envoie une macro (`send_macro`) : macros de son onglet.
- Fermer le dernier onglet : impossible (action désactivée, bouton de fermeture désactivé).

## 7. Tests

**Unitaires (cœur)** : `test_action_relay` (délégation d'activation et d'état ; reflet de l'état, de la sensibilité, ajout et retrait d'action dans la
cible ; changement de cible qui notifie seulement ce qui change ; sans cible = désactivé).

**Intégration (couche application, sans GTK)** : `test_session` (création, nom court selon le transport, application de profil, ouverture et fermeture
sur pty, arrêt idempotent, signal `closed`, deux sessions sur deux pty indépendantes : historique, options, macros) ; `test_session_manager` (ordre,
déplacement, active à la voisine après retrait) ; `test_macros_send` adapté (instances, polling qui envoie sur la liaison de son instance même quand une
autre est « active ») ; `test_script_engine` adapté (instances ; deux moteurs en parallèle sur deux liaisons, chacun voit son port 0 ; arrêt de l'un sans
toucher l'autre ; port série tenu par un moteur refusé à l'autre) ; `test_app_config` adapté (profil par session, `macros_file` mémorisé dans le profil de
la bonne session).

**Suite graphique** : les 47 scénarios de 6a passent inchangés dans leur intention (un seul onglet : barre d'onglets cachée). Nouveaux scénarios :
`test_onglets` (New tab par le dialogue Port, Annuler ne crée rien, barre d'onglets visible à 2, Close tab, dernier onglet non fermable, Ctrl+Maj+T/W/Page,
titre de fenêtre), `test_onglets_actions` (écho et vue propres à chaque onglet, menus cochés selon l'onglet actif, Open/Close port sur l'onglet actif
seulement), `test_onglets_macros` (fichier de macros différent par onglet, panneaux différents, raccourci d'une macro d'un onglet sans effet dans l'autre,
polling qui continue d'envoyer sur son port après changement d'onglet et s'arrête à la fermeture de son onglet), `test_onglets_lua` (port 0 = onglet de
lancement ; deux scripts en parallèle ; fermer un onglet pendant son script, sans avertissement), `test_onglets_fermeture` (confirmation si script ou envoi en
cours ; fermeture de la fenêtre avec plusieurs onglets actifs : code 0, aucun avertissement, `[window]` écrit une fois), `test_onglets_verrou` (même port
série dans un second onglet refusé), `test_onglets_sigusr` (SIGUSR2 ferme tous les ports, SIGUSR1 les rouvre).

## 8. Documentation et traductions

`CLAUDE.md` (architecture : `GtSession`, `GtSessionManager`, relais, moteur et macros par instance, page d'onglet, nouvelles actions et raccourcis, règle
« aucune statique d'onglet »), `tests/README.md` (nouveaux scénarios, `app.test-sessions`), revue (annexe des remarques reportées de 6b ; remarque « disposition
par défaut » de 6a traitée), `po/POTFILES.in`, `po/fr.po` (nouveaux libellés : « New tab », « Close tab », confirmations).

## 9. Critères d'acceptation

1. `meson test -C build`, `meson test -C build-asan` et `meson test -C build --suite gui` passent (instabilités connues tolérées, une relance).
2. `grep -rn 'gt_link_get_default\|gt_link_set_default\|extern GtProfile config' src/` est vide.
3. `grep -rn 'gtk/gtk.h' src/*.c src/*.h` est vide.
4. Deux onglets ouverts sur deux pty ont des historiques, options, macros, scripts et journaux indépendants (vérifié par `test_session` et la suite graphique).
5. Toutes les actions `app.*` de 6a existent encore, avec le même type de paramètre et d'état (vérifié par `test_menus_actions`).

## 10. Amendements issus du plan

Décisions du plan (`docs/superpowers/plans/2026-10-10-refonte-onglets.md`, « Décisions prises par ce plan ») et de l'exécution qui précisent ou modifient
cette spec ; elles font foi sur les sections citées.

1. **Journal et envoi de fichier dans la page** (plan, décision 1 ; amende §4.2 et §3.2-16) : le `GtRxLogger` et le `GtFileSender` restent dans le module
   d'interface `src/ui/file_actions.c`, rendu par page ; la session ne possède pas le journal (pas de `gt_session_get_logger`). Pour la confirmation de
   fermeture, la page tient l'état d'envoi de sa session (`gt_session_set_file_sending()`) ; `gt_session_is_busy()` = script en cours ou envoi en cours.
   La session n'a pas de propriété « busy » : seules « short-name » et « status-text » sont notifiées.
2. **Ordre d'arrêt** (plan, décision 3 ; précise §3.2-11 et §5) : (a) la page, `gt_session_view_shutdown()` : envoi de fichier et journal, panneau de
   macros, raccourcis, barre d'état détachée, actions de page retirées du groupe ; (b) la session, `gt_session_shutdown()` : moteur Lua, macros
   (ordonnanceur), actions désactivées, liaison fermée, puis signal `closed`. Toujours la page avant la session, pour un onglet comme pour la fenêtre (à la
   fermeture : toutes les pages, puis toutes les sessions dans l'ordre des onglets). Un crochet qui vise une session dont la page est arrêtée ne fait rien.
3. **Répartition des actions** (plan, décision 4 ; précise §3.2-3) : la session met dans son groupe `local-echo`, `crlf-auto`, `timestamp`,
   `esc-clear-screen`, `hex-index`, `view-mode`, `hex-bytes`, `auto-reconnect`, `clear-screen`, `open-port`, `close-port`, `send-break`, `toggle-dtr`,
   `toggle-rts` ; la page y ajoute `clear-scrollback`, `copy`, `paste`, `select-all`, `find`, `show-hex-bar`, `show-macro-panel`, `show-script-panel`,
   `script-layout`, `send-file`, `save-raw`, `save-ascii`, `log-start`, `log-paused`, `log-stop`, `log-clear`. Restent des actions d'application sans relais,
   qui lisent la session active à l'activation : `quit`, `about`, `updates`, `port`, `terminal`, `macros`, `load-profile`, `save-profile`, `delete-profile`,
   `load-macros`, `save-macros`, `new-tab`, `close-tab`, `next-tab`, `previous-tab`, `active-tab` (contrairement à §3.2-3, `macros` n'est pas une action
   d'onglet). Les raccourcis des actions relayées sont posés au démarrage sur les noms `app.*`.
4. **Ordre maître : l'interface** (plan, décision 5 ; précise §4.3 et §4.6) : le `GtkNotebook` est maître de l'ordre et de la page affichée ;
   `GtSessionManager` le suit (`gt_session_manager_add`, `remove`, et `page-reordered` → `gt_session_manager_move`) et l'interface n'écoute pas
   `items-changed` du modèle. `switch-page` est branché après le gestionnaire par défaut (la page courante est déjà la nouvelle quand la session active
   change). Les onglets sont réordonnables à la souris.
5. **Bouton de fermeture d'un onglet** (plan, décision 6 ; amende §4.6) : il appelle `gt_app_close_session(session)` (même chemin que `app.close-tab` pour
   la session active : confirmation si occupée), pas une action à paramètre.
6. **New tab** (plan, décision 7, et exécution ; précise §3.2-12 et §5) : `gt_port_dialog_show_new()` (dialogue Port en mode création) prérempli d'une copie
   du profil actif ; le dialogue se ferme **avant** d'appeler son rappel ; OK → `gt_app_add_session(profil, nom de profil)` : session créée, page insérée à
   droite de l'onglet actif et affichée, profil appliqué, port ouvert. Fichier de macros du nouvel onglet (chargement non interactif) : celui du profil
   (`macros_file`), sinon le chemin courant des macros de l'onglet actif, sinon aucun — jamais le fichier par défaut d'office.
7. **Couleurs du panneau de script** (plan, décision 12 ; précise §3.2-14) : `[colors]` est commun ; le dialogue « Colors… » d'une page enregistre puis
   rafraîchit les panneaux de toutes les pages (`gt_app_foreach_view`).
8. **Raccourcis de macros** (plan, décision 13 ; précise §4.6) : un `GtkShortcutController` par page, portée `GTK_SHORTCUT_SCOPE_LOCAL`, phase de capture,
   ajouté à la page : il ne voit que les touches dont la cible est dans la page affichée. Après un changement d'onglet, le focus clavier est rendu au terminal
   de la page (différé à l'inactivité : un clic sur une étiquette donne d'abord le focus au notebook).
9. **`gt_session_view_new(app, session, state, layout_from)`** (amende §4.6) : `layout_from` est la page dont le panneau de script donne sa disposition
   (nouvel onglet), NULL pour la première page ([layout] et [session] lus). Séparateurs et visibilités sont passés à la création (`state`) : il n'y a pas de
   `gt_session_view_set_state()`.
10. **`[session] script_path`** (exécution ; précise §3.2-14) : un panneau qui a un chemin l'écrit ; un panneau sans chemin (nouvel onglet à l'éditeur vide)
    ne retire jamais la clé, si bien qu'une fermeture sur un tel onglet garde le chemin du premier.
11. **API de session** (amende §4.2 et §3.2-4) : nom court par `gt_session_get_short_name()` (propriété « short-name ») et `gt_session_short_name_for(profil)`
    au lieu de `gt_session_dup_short_name()` ; `gt_session_apply_profile()` n'appelle aucun crochet (le crochet `profile_applied(session)` est appelé par
    `app_config_load_profile`) ; le message temporaire garde le nom `gt_app_status_flash(session, ms, fmt, …)`.
12. **Harnais** (exécution ; précise §7) : `Broadway.drag_in_surface` (glisser réel, réordonnancement des onglets) ; un scénario dont l'échec s'accompagne
    d'une assertion interne de `gtk4-broadwayd` dans son journal (défaut de GTK 4.24) sort en SKIP (77), pas en FAIL. La suite graphique compte 57 scénarios
    (47 de 6a et 10 d'onglets : `test_onglets`, `_actions`, `_fermeture`, `_verrou`, `_sigusr`, `_terminal`, `_ordre`, `_macros`, `_macros_fichier`, `_lua`).
