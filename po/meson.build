i18n = import('i18n')

i18n.gettext(meson.project_name(),
	preset : 'glib',
	# --from-code : certaines chaînes des sources contiennent des caractères non ASCII (ex. « … », guillemets).
	args: ['--directory=' + meson.project_source_root(), '--from-code=UTF-8']
)
