# Suite graphique : scénarios Python pilotant GTKTerm par AT-SPI sur un affichage broadway privé (voir tests/README.md).
# Lancée par « meson test -C build --suite gui » ; exclue de « meson test -C build » (add_test_setup dans tests/meson.build).
gui_guard = shared_module('gtkterm_guard', 'guard.c',
	name_prefix : '',
	dependencies : [cc.find_library('dl', required : false)])

gui_env = environment()
gui_env.set('GTKTERM_BIN', gtkterm_exe.full_path())
gui_env.set('GTKTERM_GUARD', gui_guard.full_path())
gui_env.set('GTKTERM_GUI_RUNS', meson.current_build_dir() / 'runs')

gui_scenarios = [
	'test_lien_perdu',
	'test_echo_vues',
	'test_hex_index',
	'test_journal',
	'test_horodatage',
	'test_sauvegarde_brute',
	'test_echo_clavier',
	'test_envoi_fichier',
	'test_raccourcis_macros',
	'test_polling',
	'test_fermeture_fenetre',
	'test_config_migration',
	'test_config_profils',
	'test_config_ligne_commande',
	'test_config_verrou',
	'test_config_fichier_vide',
	'test_config_valeurs_corrigees',
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
