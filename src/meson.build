prog_sh = find_program('sh')
baudrates_h = custom_target(
	    'baudrates.h',
	    output : 'baudrates.h',
	    input : 'baudrates.sh',
	    command : [prog_sh, '@INPUT@', '@OUTPUT@'])

# Cœur sans GTK (GLib, GObject, GIO) : compilé une fois, lié par l'exécutable et par les tests (tests/).
core_sources = [
	'baud.c',
	'baudrates.c',
	baudrates_h,
	'i18n.c',
	'i18n.h',
	'macros_format.c',
	'macros_format.h',
	'macro_arg.c',
	'macro_arg.h',
	'macros_list.c',
	'macros_list.h',
	'macro_set.c',
	'macro_set.h',
	'macro_file.c',
	'macro_file.h',
	'project_file.c',
	'project_file.h',
	'macro_poller.c',
	'macro_poller.h',
	'macro_view.c',
	'macro_view.h',
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
	'serial_ports.c',
	'serial_ports.h',
	'port_form.c',
	'port_form.h',
	'port_activity.c',
	'port_activity.h',
	'config_store.c',
	'config_store.h',
	'profile_keyfile.c',
	'profile_keyfile.h',
	'file_stamp.c',
	'file_stamp.h',
	'window_state.h',
	'rx_queue.c',
	'rx_queue.h',
	'script_port.c',
	'script_port.h',
	'action_relay.c',
	'action_relay.h',
	'term_options.c',
	'term_options.h',
	'hex_input.c',
	'hex_input.h',
	'capture.c',
	'capture.h',
	'file_sender.c',
	'file_sender.h',
	'rx_logger.c',
	'rx_logger.h',
	'update_check.c',
	'update_check.h',
	'update_fetch.c',
	'update_fetch.h',
	'update_versions.c',
	'update_versions.h',
	'app_ui.c',
	'app_ui.h',
]

gtkterm_core = static_library(
	'gtkterm_core', core_sources,
	dependencies : [glib_dep, gobject_dep, gio_dep, config]
)

gtkterm_core_dep = declare_dependency(
	link_with : gtkterm_core,
	include_directories : include_directories('.'),
	dependencies : [glib_dep, gobject_dep, gio_dep, config]
)

# Révision git recalculée à chaque construction (le fichier n'est réécrit que si elle change).
# safe.directory=* : l'installation (pkexec meson install) peut relancer ceci en root dans un dépôt
# appartenant à l'utilisateur ; sans cette option, git refuse et la révision deviendrait « unknown ».
git_revision_h = vcs_tag(
	command : ['git', '-c', 'safe.directory=*', 'rev-parse', 'HEAD'],
	fallback : 'unknown',
	input : 'git_revision.h.in',
	output : 'git_revision.h',
	replace_string : '@GIT_REVISION@'
)

# Version installée (mise à jour par versions taguées) : tag vX.Y.Z exact, ou vX.Y.Z-N-g<hash> après lui. Sans tag joignable (dépôt sans
# tag, archive sans historique), la fenêtre affiche la version de meson.build plutôt que « unknown ».
git_describe_h = vcs_tag(
	command : ['git', '-c', 'safe.directory=*', 'describe', '--tags', '--match', 'v[0-9]*', '--abbrev=7'],
	fallback : meson.project_version(),
	input : 'git_describe.h.in',
	output : 'git_describe.h',
	replace_string : '@GIT_DESCRIBE@'
)

# Couche application (GLib, gudev, Lua ; aucun en-tête GTK, vérifié par grep, spec §14-3) : bibliothèque statique liée par l'interface
# GTK 4 (src/ui/). Compilée sans dépendance GTK : un en-tête GTK inclus par erreur casse la construction. Les tests d'intégration
# (tests/meson.build) compilent les mêmes fichiers avec leurs bouchons.
app_layer_sources = [
	'app_config.c',
	'app_config.h',
	'cmdline.c',
	'cmdline.h',
	'link.c',
	'link.h',
	'macros.c',
	'macros.h',
	'script_engine.c',
	'script_engine.h',
	'project.c',
	'project.h',
	'session.c',
	'session.h',
	'session_manager.c',
	'session_manager.h',
]

gtkterm_app = static_library('gtkterm_app', app_layer_sources,
	dependencies : [gtkterm_core_dep, gudev_deps, lua_dep, config])

gtkterm_app_dep = declare_dependency(
	link_with : gtkterm_app,
	dependencies : [gtkterm_core_dep, gudev_deps, lua_dep, config])

# Interface GTK 4 : l'exécutable gtkterm (gtkterm_exe).
subdir('ui')
