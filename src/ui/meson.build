# Interface GTK 4 : exécutable gtkterm, installé. Toute API dépréciée ou postérieure à GTK 4.14 (et à GtkSourceView 5.12) casse la construction,
# même avec des en-têtes plus récents (spec §7).
gnome = import('gnome')

ui_c_args = [
	'-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14',
	'-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14',
	'-Werror=deprecated-declarations',
	'-DGTK_SOURCE_VERSION_MIN_REQUIRED=GTK_SOURCE_VERSION_5_12',
	'-DGTK_SOURCE_VERSION_MAX_ALLOWED=GTK_SOURCE_VERSION_5_12',
]

# Préfixe /eu/mulagabriel/gtkterm/ui : menus.ui, terminal_dialog.ui, logo, icons/ (chemin d'icônes ajouté par app.c) ; rien sous …/gtk/,
# que GtkApplication chargerait d'office
ui_resources = gnome.compile_resources('gtkterm-resources', 'gtkterm.gresource.xml', source_dir : '.')

ui_deps = [gtk4_dep, vte4_dep, gsv5_dep, gtkterm_app_dep]

ui_sources = [
	'main.c',
	'app.c',
	'app.h',
	'app_actions.c',
	'app_actions.h',
	'main_window.c',
	'main_window.h',
	'session_view.c',
	'session_view.h',
	'activity_icon.c',
	'activity_icon.h',
	'tab_label.c',
	'tab_label.h',
	'terminal_view.c',
	'terminal_view.h',
	'dialogs.c',
	'dialogs.h',
	'status_bar.c',
	'status_bar.h',
	'search_bar.c',
	'search_bar.h',
	'hex_send_bar.c',
	'hex_send_bar.h',
	'profile_dialogs.c',
	'profile_dialogs.h',
	'port_dialog.c',
	'port_dialog.h',
	'terminal_dialog.c',
	'terminal_dialog.h',
	'file_actions.c',
	'file_actions.h',
	'file_send_window.c',
	'file_send_window.h',
	'macro_accel.c',
	'macro_accel.h',
	'macro_editor.c',
	'macro_editor.h',
	'macro_row.c',
	'macro_row.h',
	'macro_panel.c',
	'macro_panel.h',
	'macro_shortcuts.c',
	'macro_shortcuts.h',
	'script_panel.c',
	'script_panel.h',
	'update_window.c',
	'update_window.h',
	'about.c',
	'about.h',
	git_revision_h,
	ui_resources,
]

# Crochet de la suite graphique (tests/gui/) : compilé par défaut, actif seulement sous GTKTERM_TEST_HOOKS=1 ; -Dtest_hooks=false le retire.
if get_option('test_hooks')
	ui_sources += ['test_hooks.c', 'test_hooks.h']
	ui_c_args += '-DGTKTERM_WITH_TEST_HOOKS=1'
endif

gtkterm_exe = executable('gtkterm', ui_sources,
	c_args : ui_c_args,
	dependencies : ui_deps,
	install : true)
