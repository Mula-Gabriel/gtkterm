# Tests unitaires : un exécutable GTest par module du cœur (src/ : gtkterm_core).
# meson les construit aussi avec « ninja » (prérequis de « meson test ») ; -Dtests=false les écarte (voir meson_options.txt).

# openpty() : dans la libc depuis glibc 2.34, dans libutil avant.
util_dep = cc.find_library('util', required : false)

# Dépendances supplémentaires par test.
unit_tests = {
	'test_macros_list' : [],
	'test_macros_format' : [],
	'test_macro_arg' : [],
	'test_macro_set' : [],
	'test_macro_file' : [],
	'test_project_file' : [],
	'test_macro_poller' : [],
	'test_macro_view' : [],
	'test_baudrates' : [util_dep],
	'test_serial_termios' : [],
	'test_config_file' : [],
	'test_history' : [],
	'test_rx_transform' : [],
	'test_device_match' : [],
	'test_view_render' : [],
	'test_profile' : [],
	'test_profile_keyfile' : [],
	'test_config_store' : [],
	'test_file_stamp' : [],
	'test_rx_queue' : [],
	'test_script_port' : [util_dep],
	'test_term_options' : [gio_dep],
	'test_action_relay' : [gio_dep],
	'test_hex_input' : [],
	'test_capture' : [],
	'test_file_sender' : [],
	'test_rx_logger' : [],
	'test_update_check' : [],
	'test_update_versions' : [],
	'test_update_fetch' : [],
	'test_serial_ports' : [util_dep],
	'test_port_form' : [],
	'test_port_activity' : [],
}

test_env = environment()
# UBSan ne fait qu'afficher par défaut : on veut qu'un comportement indéfini fasse échouer le test.
test_env.set('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
# Catalogues .mo compilés par la construction (po/<langue>/gtkterm.mo) : test des messages traduits du thread Lua.
test_env.set('GTKTERM_PO_DIR', meson.project_build_root() / 'po')
# Jamais l'écran réel : aucun test de cette suite n'ouvre d'affichage (test_macro_accel lie GTK 4 sans gtk_init) ; si l'un venait à appeler
# gtk_init, GDK se rabattrait sur $XDG_RUNTIME_DIR/wayland-0 (le compositeur de la session) même sans DISPLAY ni WAYLAND_DISPLAY. Un backend
# inconnu le fait échouer à la place ; seule la suite graphique isolée (broadway privé) ouvre un affichage.
# Variables vidées plutôt que retirées : environment().unset() n'existe qu'à partir de meson 1.4.
test_env.set('DISPLAY', '')
test_env.set('WAYLAND_DISPLAY', '')
test_env.set('GDK_BACKEND', 'aucun-affichage')

foreach name, extra_deps : unit_tests
	exe = executable(name, name + '.c',
		dependencies : [gtkterm_core_dep] + extra_deps)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env)
endforeach

# Script de mise à jour et outil de publication (sh, git, meson, ninja ; aucun réseau ni root) : dépôts et préfixes jetables.
sh_prog = find_program('sh')
test('update_script', sh_prog, args : [files('test_update_script.sh'), files('../data/gtkterm-update.sh')], protocol : 'tap', suite : 'unit',
	env : test_env, timeout : 300)
test('publier_version', sh_prog, args : [files('test_publier_version.sh'), files('../tools/publier-version.sh')], protocol : 'tap', suite : 'unit',
	env : test_env, timeout : 60)

# Tests d'intégration : vrais modules de la couche application (src/) compilés avec des bouchons (tests/stubs/) ou leur propre table
# GtAppUi, sans GTK ni fenêtre.
stub_journal = files('stubs/stub_journal.c')
stub_link = files('stubs/stub_link.c')
stubs_inc = include_directories('stubs')

integration_tests = {
	'test_link' : {
		'sources' : files('../src/link.c'),
		'deps' : [gudev_deps, util_dep],
		'link_args' : [],
	},
	'test_transport' : {
		'sources' : [],
		'deps' : [util_dep],
		'link_args' : ['-Wl,--wrap=ioctl', '-Wl,--wrap=getaddrinfo'],
	},
	'test_macros_send' : {
		'sources' : files('../src/macros.c') + stub_journal + stub_link,
		'deps' : [gobject_dep],
		'link_args' : [],
	},
	'test_script_engine' : {
		'sources' : files('../src/script_engine.c', 'stubs/stub_macros.c') + stub_journal + stub_link,
		'deps' : [gobject_dep, lua_dep, util_dep],
		'link_args' : ['-Wl,--wrap=ioctl', '-Wl,--wrap=getaddrinfo'],
	},
}

integration_exes = {}
foreach name, t : integration_tests
	exe = executable(name, [name + '.c'] + t['sources'],
		include_directories : stubs_inc,
		dependencies : [gtkterm_core_dep] + t['deps'],
		link_args : t['link_args'])
	integration_exes += {name : exe}
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env, timeout : 60)
endforeach

# Tests de la couche application complète (gtkterm_app : sessions, configuration, ligne de commande), sans GTK ni fenêtre ; HOME et XDG privés.
app_tests = {
	'test_session' : [util_dep],
	'test_project' : [util_dep],
	'test_session_manager' : [],
	'test_app_config' : [],
	'test_cmdline' : [],
}
foreach name, extra_deps : app_tests
	exe = executable(name, name + '.c', dependencies : [gtkterm_app_dep] + extra_deps)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env, timeout : 60)
endforeach

# Logique sans affichage de l'interface GTK 4 (éditeur de macros) : ni gtk_init ni fenêtre (GDK_BACKEND invalide de test_env). Mêmes drapeaux de version que
# src/ui/ (ui_c_args, variable globale de src/ui/meson.build, lu avant tests/).
ui_logic_tests = {
	'test_macro_row' : {'sources' : files('../src/ui/macro_row.c'), 'deps' : [gio_dep]},
	'test_macro_accel' : {'sources' : files('../src/ui/macro_accel.c'), 'deps' : [gtk4_dep]},
}
foreach name, t : ui_logic_tests
	exe = executable(name, [name + '.c'] + t['sources'],
		c_args : ui_c_args,
		include_directories : include_directories('../src/ui'),
		dependencies : [gtkterm_core_dep] + t['deps'])
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env)
endforeach

# Syntaxe de tous les exemples Lua de scripts/ au moment du test ; sauté (code 77) si luac est absent.
test('lua-syntax', find_program('sh'), args : [files('lua-syntax.sh'), meson.project_source_root() / 'scripts'], suite : 'lint')

# Suite graphique GTK 4 : seulement si le crochet de test est construit (-Dtest_hooks, défaut true)
if get_option('test_hooks')
	subdir('gui')
endif

# La suite graphique ne tourne que sur demande : meson test -C build --suite gui
add_test_setup('default', exclude_suites : ['gui'], is_default : true)
