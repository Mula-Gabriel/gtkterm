prog_sh = find_program('sh')
baudrates_h = custom_target(
	    'baudrates.h',
	    output : 'baudrates.h',
	    input : 'baudrates.sh',
	    command : [prog_sh, '@INPUT@', '@OUTPUT@'])

sources = [
	'baud.c',
	'baudrates.c',
	baudrates_h,
	'buffer.c',
	'buffer.h',
	'cmdline.c',
	'cmdline.h',
	'device_monitor.c',
	'device_monitor.h',
	'files.c',
	'files.h',
	'gtkterm.c',
	'i18n.c',
	'i18n.h',
	'interface.c',
	'interface.h',
	'logging.c',
	'logging.h',
	'macros.c',
	'macros.h',
	'macros_list.c',
	'macros_list.h',
	'macros_format.c',
	'macros_format.h',
	'terminal_display.c',
	'terminal_display.h',
	'macro_panel.c',
	'macro_panel.h',
	'parsecfg.c',
	'parsecfg.h',
	'search.c',
	'search.h',
	'serial.c',
	'serial.h',
	'term_config.c',
	'term_config.h',
	'transport.c',
	'transport.h',
	'user_signals.c',
	'user_signals.h',
	'script_engine.c',
	'script_engine.h',
	'script_panel.c',
	'script_panel.h',
	gresources
]

executable(
	'gtkterm', sources,
	export_dynamic : true,
	dependencies : [
		gtk_deps,
		vte_deps,
		config,
		gudev_deps,
		lua_dep,
		gsv_dep
	],
	install : true
)
