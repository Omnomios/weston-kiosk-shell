project('weston-kiosk', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.63.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'b_lundef=true'])

# Weston changes its plugin ABI between major releases.
libweston = dependency('libweston-14', version: ['>= 14.0.0', '< 15.0.0'])
weston = dependency('weston', version: ['>= 14.0.0', '< 15.0.0'])
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

protocol_test = executable('layer-shell-client-test',
  'tests/layer-shell-client.c', layer_client, layer_code, xdg_client, xdg_code,
  dependencies: dependency('wayland-client'))

# Weston 14's headless backend has no --fake-seat option. Keep the synthetic
# input seat in a test-only module so focus behavior can still be exercised.
test_seat = shared_module('kiosk-test-seat', 'tests/test-seat.c',
  dependencies: libweston, name_prefix: '', install: false)

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

weston_bin = find_program(get_option('weston_binary'), required: false)
if weston_bin.found()
  python = import('python').find_installation('python3')
  test_env = environment()
  test_env.prepend('LD_LIBRARY_PATH', libweston.get_variable(pkgconfig: 'libdir'))
  test('headless-shell-load', python,
    args: [files('scripts/smoke-test.py'), weston_bin.full_path(), shell.full_path()],
    env: test_env, timeout: 15)
  test('layer-shell-integration', python,
    args: [files('tests/run-layer-shell-tests.py'), weston_bin.full_path(),
           shell.full_path(), protocol_test.full_path(), test_seat.full_path()],
    env: test_env, timeout: 45)
endif
