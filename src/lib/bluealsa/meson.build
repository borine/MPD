if not is_linux
  bluealsa_dep = dependency('', required: false)
  subdir_done()
endif

if not dbus_dep.found()
  if get_option('bluealsa').enabled()
    error('bluealsa requires D-Bus')
  endif
  enable_bluealsa = false
  conf.set('ENABLE_BLUEALSA', enable_bluealsa)

  subdir_done()
endif

conf.set('ENABLE_BLUEALSA', true)

bluealsa_sources = [
  'PCM.cxx',
  'PCMOutput.cxx',
# 'PCMInput.cxx',
]

bluealsa = static_library(
  'bluealsa',
  bluealsa_sources,
  include_directories: inc,
  dependencies: [
    dbus_dep,
  ],
)

bluealsa_dep = declare_dependency(
  link_with: bluealsa,
  dependencies: [
    dbus_dep,
  ],
)

