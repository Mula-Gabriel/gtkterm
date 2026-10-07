# Tests unitaires : un exécutable GTest par module du cœur (src/ : gtkterm_core).
# meson les construit aussi avec « ninja » (prérequis de « meson test ») ; -Dtests=false les écarte (voir meson_options.txt).

# Dépendances supplémentaires par test ; les tâches suivantes complètent ce dictionnaire.
# openpty() : dans la libc depuis glibc 2.34, dans libutil avant.
util_dep = cc.find_library('util', required : false)

unit_tests = {
	'test_macros_list' : [],
	'test_macros_format' : [],
	'test_baudrates' : [util_dep],
	'test_serial_termios' : [],
	'test_config_file' : [],
	'test_history' : [],
	'test_rx_transform' : [],
	'test_device_match' : [],
	'test_view_render' : [],
	'test_profile' : [],
	'test_config_store' : [],
}

test_env = environment()
# UBSan ne fait qu'afficher par défaut : on veut qu'un comportement indéfini fasse échouer le test.
test_env.set('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

foreach name, extra_deps : unit_tests
	exe = executable(name, name + '.c',
		dependencies : [gtkterm_core_dep] + extra_deps)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env)
endforeach

# Tests d'intégration : vrais modules de src/ compilés avec des bouchons d'interface (tests/stubs/), sans fenêtre.
stub_journal = files('stubs/stub_journal.c')
stub_ui = files('stubs/stub_ui.c')
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
		'link_args' : ['-Wl,--wrap=ioctl'],
	},
	'test_macros_send' : {
		'sources' : files('../src/macros.c') + stub_journal + stub_ui + stub_link,
		'deps' : [gtk_deps],
		'link_args' : [],
	},
	'test_script_engine' : {
		'sources' : files('../src/script_engine.c', 'stubs/stub_macros.c') + stub_journal + stub_link,
		'deps' : [gtk_deps, lua_dep, util_dep],
		'link_args' : [],
	},
}

foreach name, t : integration_tests
	exe = executable(name, [name + '.c'] + t['sources'],
		include_directories : stubs_inc,
		dependencies : [gtkterm_core_dep] + t['deps'],
		link_args : t['link_args'])
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env, timeout : 60)
endforeach

# Syntaxe de tous les exemples Lua de scripts/ au moment du test ; sauté (code 77) si luac est absent.
test('lua-syntax', find_program('sh'), args : [files('lua-syntax.sh'), meson.project_source_root() / 'scripts'], suite : 'lint')

subdir('gui')

# La suite graphique ne tourne que sur demande : meson test -C build --suite gui
add_test_setup('default', exclude_suites : ['gui'], is_default : true)
