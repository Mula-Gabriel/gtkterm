project(
	'gtkterm',
	'c',
	version: '2.2.0',
	meson_version : '>= 1.0.0',
	default_options : ['c_std=gnu11', 'warning_level=2']
)

# Dépendances : pile GTK 4 seule, sans libadwaita. Minimum = versions d'Ubuntu 24.04 (GTK 4.14, VTE 0.76, GtkSourceView 5.12), exigé aussi à
# la compilation par les macros de version de src/ui/meson.build.
gtk4_dep = dependency('gtk4', version : '>= 4.14')
vte4_dep = dependency('vte-2.91-gtk4', version : '>= 0.76')
gsv5_dep = dependency('gtksourceview-5', version : '>= 5.12')	# panneau de script de l'interface GTK 4 (src/ui/)
gudev_deps = dependency('gudev-1.0', version: '>= 230')
lua_dep = dependency('lua5.4', required: true)
glib_dep = dependency('glib-2.0', version : '>= 2.70')	# g_test_incomplete_printf / g_test_fail_printf
gobject_dep = dependency('gobject-2.0', version : '>= 2.70')	# GObject du cœur (GtTermOptions, GtFileSender) et bouchon de la liaison des tests
gio_dep = dependency('gio-2.0', version : '>= 2.70')	# GPropertyAction (test_term_options), G_IO_ERROR (GtFileSender), GSubprocess (update_check.c)

# Find install paths
prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
localedir = join_paths(prefix, get_option('localedir'))

# Needed on some platforms for <termios.h>
add_project_arguments('-D_GNU_SOURCE=1',
		      '-D_XOPEN_SOURCE=700',
		      # Les rappels GTK ont des paramètres imposés : les avertissements « unused parameter » seraient du bruit.
		      '-Wno-unused-parameter',
		      '-Werror=format-security',
		      '-Werror=implicit-function-declaration',
                      language : ['c'])

# Configure GTKTerm
conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE', meson.project_name())

# Dossier où le script de mise à jour est installé (lancé par la fenêtre « Updates », src/ui/update_window.c).
conf.set_quoted('PACKAGE_DATA_DIR', join_paths(datadir, 'gtkterm'))

conf.set_quoted('LOCALEDIR', localedir)

cc = meson.get_compiler('c')
if cc.has_function('cfsetobaud', prefix : [ '#define _GNU_SOURCE 1',
					    '#define _XOPEN_SOURCE 700',
					    '#include <termios.h>'])
  conf.set('HAVE_CFSETOBAUD', '1')
endif
if cc.has_header('linux/termios.h')
  conf.set('HAVE_LINUX_TERMIOS_H', '1')
endif
if cc.has_header('sys/ttycom.h')
  conf.set('HAVE_SYS_TTYCOM_H', '1')
endif

if cc.has_header('sys/sysmacros.h')
  conf.set('HAVE_SYS_SYSMACROS_H', '1')
endif

configure_file(output : 'config.h', configuration : conf)
config = declare_dependency(include_directories : include_directories('.'))

subdir('data')
subdir('src')
subdir('po')
if get_option('tests')
	subdir('tests')
endif
