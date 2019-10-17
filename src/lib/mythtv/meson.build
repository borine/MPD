if not is_linux
  mythtv_dep = dependency('', required: false)
  subdir_done()
endif

cpp = meson.get_compiler('cpp')
mysqlpp = cpp.find_library('mysqlpp', required: 'mythtv')

if not mysqlpp.found()
  mythtv_dep = dependency('', required: false)
  subdir_done()
endif

mysqlclient_dep = dependency('mysqlclient', required: false)

if not mysqlclient_dep.found()
  mythtv_dep = dependency('', required: false)
  subdir_done()
endif

conf.set('ENABLE_MYTHTV', true)

mythtv = static_library(
  'mythtv',
  'MythtvClient.cxx',
  'MythtvInstance.cxx',
  include_directories: inc,
  dependencies: [
    mysqlpp,
	mysqlclient_dep,
  ],
)

mythtv_dep = declare_dependency(
  link_with: mythtv,
  dependencies: [
    mysqlpp,
	mysqlclient_dep,
  ],
)
