project('fpga-demo', 'cpp', default_options: [
  'default_library=static',
  'cpp_std=c++20',
], version: '0.1.0')

cppc = meson.get_compiler('cpp')

include_directories = [
  'include',
]

sources = [
  'src/demo.cpp'
]

liborpheus_proj = subproject('orpheus')
liborpheus_dep = liborpheus_proj.get_variable('liborpheus_dep')

libsoundio_dep = cppc.find_library('soundio', required: true)

executable('fpgaudio-demo',
  sources,
  include_directories: include_directories,
  dependencies: [liborpheus_dep, libsoundio_dep]
)
