add_languages('cpp', required: true)

sdk_root = meson.current_source_dir()
arch = 'x86_64-linux-gnu'
doca_bundle_libdir = sdk_root / 'lib' / arch

src_inc = include_directories('src')
sdk_inc = include_directories('include')

cpp = meson.get_compiler('cpp')

cpp_args = [
  '-O2',
  '-g',
  '-Wall',
  '-Wextra',
  '-Wno-error=deprecated-declarations',
]

doca_runtime_link_args = [
  '-L' + doca_bundle_libdir,
  '-Wl,-rpath,' + doca_bundle_libdir,
  '-ldoca_flow',
  '-ldoca_dpdk_bridge',
  '-ldoca_common',
  '-lnvhws',
]

doca_runtime_dep = declare_dependency(
  include_directories: sdk_inc,
  link_args: doca_runtime_link_args,
)

doca_has_aso = cpp.has_header(
  'doca_flow_external_action_array.h',
  include_directories: sdk_inc,
  required: true,
)

pipeline_sources = files(
  'src/doca_pipelines.cpp',
  'src/doca_pipelines/pipe_builder.cpp',
  'src/doca_pipelines/pipeline_graph.cpp',
  'src/doca_pipelines/doca_pipelines_meta_rr.cpp',
  'src/doca_pipelines_shim.cpp',
)

doca_pipelines_meta_rr_lib = static_library(
  'doca_pipelines_meta_rr',
  pipeline_sources,
  include_directories: [src_inc, sdk_inc],
  cpp_args: cpp_args,
  install: false,
)

doca_pipelines_meta_rr_dep = declare_dependency(
  link_with: doca_pipelines_meta_rr_lib,
  include_directories: [src_inc, sdk_inc],
  dependencies: [doca_runtime_dep],
)
