project('tgt', 'c',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c11'])

add_project_arguments('-DG_LOG_DOMAIN="tgt"', language : 'c')
add_project_arguments('-DG_LOG_USE_STRUCTURED', language : 'c')


gtk_dep = dependency('gtk+-3.0')
vte_dep = dependency('vte-2.91')
glib_dep = dependency('glib-2.0')

sources = [
    'src/tgt.c',
]

exe = executable('tgt', sources,
  dependencies : [gtk_dep, vte_dep, glib_dep],
  install : true,
)

# .gitignore everything in the build directory
configure_file(
    output: '.gitignore',
    command: ['echo', '**/**',],
    capture: true,
    install: false,
)
