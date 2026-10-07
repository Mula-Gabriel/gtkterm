prog_sh = find_program('sh')
baudrates_h = custom_target(
	    'baudrates.h',
	    output : 'baudrates.h',
	    input : 'baudrates.sh',
	    command : [prog_sh, '@INPUT@', '@OUTPUT@'])

# Cœur sans GTK : compilé une fois, lié par l'exécutable et par les tests (tests/).
core_sources = [
	'baud.c',
	'baudrates.c',
	baudrates_h,
	'i18n.c',
	'i18n.h',
	'macros_format.c',
	'macros_format.h',
	'macros_list.c',
	'macros_list.h',
	'macro_set.c',
	'macro_set.h',
	'macro_file.c',
	'macro_file.h',
	'macro_poller.c',
	'macro_poller.h',
	'serial_termios.c',
	'serial_termios.h',
	'config_file.c',
	'config_file.h',
	'history.c',
	'history.h',
	'rx_transform.c',
	'rx_transform.h',
	'device_match.c',
	'device_match.h',
	'view_render.c',
	'view_render.h',
	'transport.c',
	'transport.h',
	'profile.c',
	'profile.h',
	'config_store.c',
	'config_store.h',
]

gtkterm_core = static_library(
	'gtkterm_core', core_sources,
	dependencies : [glib_dep, config]
)

gtkterm_core_dep = declare_dependency(
	link_with : gtkterm_core,
	include_directories : include_directories('.'),
	dependencies : [glib_dep, config]
)

# Révision git recalculée à chaque construction (le fichier n'est réécrit que si elle change).
# safe.directory=* : l'installation (pkexec/sudo ninja install) relance ceci en root dans un dépôt
# appartenant à l'utilisateur ; sans cette option, git refuse et la révision deviendrait « unknown ».
git_revision_h = vcs_tag(
	command : ['git', '-c', 'safe.directory=*', 'rev-parse', 'HEAD'],
	fallback : 'unknown',
	input : 'git_revision.h.in',
	output : 'git_revision.h',
	replace_string : '@GIT_REVISION@'
)

sources = [
	'cmdline.c',
	'cmdline.h',
	'files.c',
	'files.h',
	'gtkterm.c',
	'interface.c',
	'interface.h',
	'logging.c',
	'logging.h',
	'macros.c',
	'macros.h',
	'terminal_display.c',
	'terminal_display.h',
	'macro_panel.c',
	'macro_panel.h',
	'search.c',
	'search.h',
	'link.c',
	'link.h',
	'term_config.c',
	'term_config.h',
	'update.c',
	'update.h',
	'user_signals.c',
	'user_signals.h',
	'script_engine.c',
	'script_engine.h',
	'script_panel.c',
	'script_panel.h',
	git_revision_h,
	gresources
]

gtkterm_exe = executable(
	'gtkterm', sources,
	export_dynamic : true,
	dependencies : [
		gtk_deps,
		vte_deps,
		config,
		gudev_deps,
		lua_dep,
		gsv_dep,
		gtkterm_core_dep
	],
	install : true
)
