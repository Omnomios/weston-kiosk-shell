project('weston-kiosk', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.63.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'b_lundef=true'])

# Weston changes its plugin ABI between major releases.
libweston = dependency('libweston-15', version: ['>= 15.0.1', '< 16.0.0'])
weston = dependency('weston', version: ['>= 15.0.1', '< 16.0.0'])
wayland = dependency('wayland-server')
pixman = dependency('pixman-1')
xkbcommon = dependency('xkbcommon')
cc = meson.get_compiler('c')
# Upstream callbacks intentionally leave some parameters unused.
add_project_arguments(cc.get_supported_arguments('-Wno-unused-parameter'), language: 'c')

module_dir = get_option('weston_module_dir')
if module_dir == ''
  module_dir = libweston.get_variable(pkgconfig: 'libdir') / 'weston'
endif
frontend = cc.find_library('exec_weston', dirs: [module_dir])

# The imported helper header includes config.h. No Weston feature flags are
# needed by the kiosk shell; generate the header locally.
configure_file(output: 'config.h', configuration: configuration_data())

scanner = find_program('wayland-scanner')
layer_xml = files('protocols/wlr-layer-shell-unstable-v1.xml')
layer_server = custom_target('layer-shell-server-header', input: layer_xml,
  output: 'wlr-layer-shell-unstable-v1-server-protocol.h',
  command: [scanner, 'server-header', '@INPUT@', '@OUTPUT@'])
layer_code = custom_target('layer-shell-protocol-code', input: layer_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
layer_client = custom_target('layer-shell-client-header', input: layer_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
# The layer-shell get_popup signature references xdg_popup_interface.
protocols = dependency('wayland-protocols')
xdg_xml = protocols.get_variable(pkgconfig: 'pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
xdg_code = custom_target('xdg-shell-protocol-code', input: xdg_xml,
  output: 'xdg-shell-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
xdg_client = custom_target('xdg-shell-client-header', input: xdg_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

timing_xml = protocols.get_variable(pkgconfig: 'pkgdatadir') / 'staging/commit-timing/commit-timing-v1.xml'
timing_client = custom_target('commit-timing-client-header', input: timing_xml,
  output: 'commit-timing-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
timing_code = custom_target('commit-timing-protocol-code', input: timing_xml,
  output: 'commit-timing-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
presentation_xml = protocols.get_variable(pkgconfig: 'pkgdatadir') / 'stable/presentation-time/presentation-time.xml'
presentation_client = custom_target('presentation-time-client-header', input: presentation_xml,
  output: 'presentation-time-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
presentation_code = custom_target('presentation-time-protocol-code', input: presentation_xml,
  output: 'presentation-time-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

protocol_test = executable('layer-shell-client-test',
  'tests/layer-shell-client.c', layer_client, layer_code, xdg_client, xdg_code,
  timing_client, timing_code, presentation_client, presentation_code,
  dependencies: dependency('wayland-client'))

shell = shared_module('weston-kiosk-shell',
  'kiosk-shell/kiosk-shell.c',
  'kiosk-shell/kiosk-shell-grab.c',
  'kiosk-shell/layer-shell.c', layer_server, layer_code, xdg_code,
  include_directories: include_directories('.'),
  dependencies: [libweston, weston, wayland, pixman, xkbcommon, frontend],
  name_prefix: '',
  install: true,
  install_dir: module_dir,
  install_rpath: module_dir)

weston_bin = find_program('weston', required: false)
if weston_bin.found()
  python = import('python').find_installation('python3')
  test('headless-shell-load', python,
    args: [files('scripts/smoke-test.py'), weston_bin.full_path(), shell.full_path()],
    timeout: 15)
  test('layer-shell-integration', python,
    args: [files('tests/run-layer-shell-tests.py'), weston_bin.full_path(),
           shell.full_path(), protocol_test.full_path()],
    timeout: 45)
endif
