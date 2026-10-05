# Règle udev des ports CDC Metro et son script de rang : dans /etc (qui prime sur /usr/lib), quel que soit le préfixe.
udev_config = configuration_data()
udev_config.set('BINDIR', join_paths(prefix, get_option('bindir')))

configure_file(
	input: '99-metro-cdc.rules.in',
	output: '99-metro-cdc.rules',
	configuration: udev_config,
	install_dir: '/etc/udev/rules.d'
)

install_data('metro-tty-index',
	install_dir: get_option('bindir'),
	install_mode: 'rwxr-xr-x'
)

meson.add_install_script('udev-reload.sh')
