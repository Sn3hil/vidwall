project('vidwall', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=false'
  ])

# Dependencies
gtk4 = dependency('gtk4')
gtk4_layer_shell = dependency('gtk4-layer-shell-0')
mpv = dependency('mpv')
epoxy = dependency('epoxy')
threads = dependency('threads')

# Source files
sources = files(
  'src/main.cpp',
  'src/hyprland_ipc.cpp',
  'src/cli_args.cpp' 
)

# Include directories
inc = include_directories('include')

# Executable
executable('vidwall',
  sources,
  include_directories: inc,
  dependencies: [gtk4, gtk4_layer_shell, mpv, epoxy, threads],
  install: true)
