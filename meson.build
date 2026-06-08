project(
	'gtkterm',
	'c',
	version: '1.3.1',
	meson_version : '>= 1.0.0'
)

# Find dependencies
gtk_deps = dependency('gtk+-3.0', version : '>= 3.0.0')
vte_deps = dependency('vte-2.91', version : '>= 0.28.0')
gudev_deps = dependency('gudev-1.0', version: '>= 230')
lua_dep = dependency('lua5.4', required: true)
gsv_dep = dependency('gtksourceview-4', required: true)

# Find install paths
prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
localedir = join_paths(prefix, get_option('localedir'))

# Needed on some platforms for <termios.h>
add_project_arguments('-D_GNU_SOURCE=1',
		      '-D_XOPEN_SOURCE=700',
                      language : ['c'])

# Configure GTKTerm
conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE', meson.project_name())
conf.set_quoted('RELEASE_DATE', 'May 2024')

# Bake the current git commit so the app can compare against the remote HEAD.
git_prog = find_program('git', required : false)
git_revision = 'unknown'
if git_prog.found()
	git_rev_result = run_command(git_prog, 'rev-parse', 'HEAD', check : false)
	if git_rev_result.returncode() == 0
		git_revision = git_rev_result.stdout().strip()
	endif
endif
conf.set_quoted('GIT_REVISION', git_revision)

# Path where the update script is installed (used by src/update.c).
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
