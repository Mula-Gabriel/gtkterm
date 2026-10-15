# Suite graphique GTK 4 : scénarios Python qui pilotent GTKTerm sur un gtk4-broadwayd privé par les actions D-Bus app.* (org.gtk.Actions), le
# crochet de test (src/ui/test_hooks.c, GTKTERM_TEST_HOOKS=1) et un faux navigateur (broadway.py), voir tests/README.md.
# Lancée par « meson test -C build --suite gui » ; exclue de « meson test -C build » (add_test_setup dans tests/meson.build).
gui_guard = shared_module('gtkterm_guard', 'guard.c',
	name_prefix : '',
	dependencies : [cc.find_library('dl', required : false)])

gui_env = environment()
gui_env.set('GTKTERM_BIN', gtkterm_exe.full_path())
gui_env.set('GTKTERM_GUARD', gui_guard.full_path())
gui_env.set('GTKTERM_GUI_RUNS', meson.current_build_dir() / 'runs')
# Révision construite (GIT_REVISION) : les scénarios de mise à jour font répondre le faux git « à jour » avec elle.
gui_env.set('GTKTERM_GIT_REVISION_H', git_revision_h.full_path())
gui_env.set('GTKTERM_GIT_DESCRIBE_H', git_describe_h.full_path())

gui_scenarios = [
	# squelette, fermeture, messages de démarrage
	'test_fumee',
	'test_quit_etat',
	'test_messages_demarrage',
	# clavier et fermeture par la décoration
	'test_echo_clavier',
	'test_config_fichier_vide',
	'test_fermeture_fenetre',
	'test_clavier_ctrl_b',
	'test_clavier_echap',
	# actions, menus, vues
	'test_menus_actions',
	'test_echo_vues',
	'test_hex_index',
	'test_horodatage',
	# barres, liaison
	'test_lien_perdu',
	'test_activite_ports',
	'test_config_verrou',
	'test_config_valeurs_corrigees',
	'test_status_bar',
	'test_search',
	'test_hex_send',
	# profils et fichiers de macros
	'test_profils',
	'test_config_ligne_commande',
	'test_config_migration',
	'test_options_profil',
	'test_fermeture_dialogue',
	# dialogues Port et Terminal
	'test_port_dialog',
	'test_terminal_dialog',
	# fichiers et journal
	'test_envoi_fichier',
	'test_sauvegarde_brute',
	'test_journal',
	'test_sauvegarde_ascii',
	'test_journal_pause',
	# raccourcis et panneau de macros
	'test_polling',
	'test_raccourcis_macros',
	'test_macros_arguments',
	'test_macros_limites',
	'test_macros_facultatifs',
	'test_macros_conflit',
	'test_focus_terminal',
	'test_raccourci_non_envoyable',
	'test_echo_macro',
	'test_macro_argument_long',
	# éditeur de macros
	'test_macros_edition',
	'test_macros_migration',
	'test_macro_editor_raccourci',
	'test_macro_editor_ordre',
	# panneau de script
	'test_script_arret',
	'test_fermeture_script',
	'test_fermeture_script_macro',
	'test_script_couleurs',
	'test_script_panneau',
	# mise à jour et À propos
	'test_update_window',
	'test_about',
	# onglets (sous-projet 6b)
	'test_onglets',
	'test_onglets_actions',
	'test_onglets_fermeture',
	'test_onglets_verrou',
	'test_onglets_sigusr',
	'test_onglets_terminal',
	'test_onglets_ordre',
	'test_onglets_macros_fichier',
	'test_onglets_macros',
	'test_onglets_lua',
	# projets (sous-projet 6c)
	'test_projet',
	'test_projet_fermeture',
	'test_projet_demarrage',
	'test_projet_macros',
	'test_projet_conflit',
	'test_projet_operations',
	'test_projet_nom',
	'test_macros_modele',
	'test_macro_editor_colonnes',
	'test_macro_editor_filtre',
]

foreach s : gui_scenarios
	test(s, find_program('sh'),
		args : [files('run-gui-test.sh'), files(s + '.py')],
		suite : 'gui',
		env : gui_env,
		depends : [gtkterm_exe, gui_guard],
		timeout : 300,
		is_parallel : false)
endforeach
