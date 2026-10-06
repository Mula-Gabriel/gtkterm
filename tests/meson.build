# Tests unitaires : un exécutable GTest par module du cœur (src/ : gtkterm_core).
# meson les construit aussi avec « ninja » (prérequis de « meson test ») ; -Dtests=false les écarte (voir meson_options.txt).

# Dépendances supplémentaires par test ; les tâches suivantes complètent ce dictionnaire.
# openpty() : dans la libc depuis glibc 2.34, dans libutil avant.
util_dep = cc.find_library('util', required : false)

unit_tests = {
	'test_macros_list' : [],
	'test_macros_format' : [],
	'test_baudrates' : [util_dep],
	'test_parsecfg' : [],
}

# Sans effet hors construction avec -Db_sanitize=address ; voir tests/README.md.
test_env = environment()
test_env.set('LSAN_OPTIONS', 'suppressions=' + meson.current_source_dir() / 'lsan.supp')
# UBSan ne fait qu'afficher par défaut : on veut qu'un comportement indéfini fasse échouer le test.
test_env.set('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')

foreach name, extra_deps : unit_tests
	exe = executable(name, name + '.c',
		dependencies : [gtkterm_core_dep] + extra_deps)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env)
endforeach

# Syntaxe des exemples Lua (luac -p : analyse seule, aucun fichier produit).
luac = find_program('luac5.4', 'luac', required : false)
if luac.found()
	lua_examples = files(
		'../scripts/01_hello.lua',
		'../scripts/02_at_commands.lua',
		'../scripts/03_wait_for_demo.lua',
		'../scripts/04_csv_logger.lua',
		'../scripts/05_macros_demo.lua',
		'../scripts/06_hex_protocol.lua',
		'../scripts/07_tcp_client.lua',
		'../scripts/08_stress_loop.lua',
		'../scripts/09_multi_port.lua',
		'../scripts/10_firmware_update.lua',
	)
	test('lua-syntax', luac, args : ['-p'] + lua_examples, suite : 'lint')
endif
