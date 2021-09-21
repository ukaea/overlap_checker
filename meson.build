project('parallel-preprocessor', 'cpp',
  version : '0.1',
  default_options : ['warning_level=2', 'cpp_std=c++17'])

# opencascade preprocessor macros result in lots of these
add_project_arguments(['-Wno-extra-semi', '-Wno-non-virtual-dtor'], language : 'cpp')

config = configuration_data()
cxx = meson.get_compiler('cpp')

# locate opencascade dependencies, can't see any CMake automatics for this :(
occt_libs = [
  'TKernel',
  'TKBO', 'TKBRep', 'TKPrim',
  'TKG3d', 'TKLCAF', 'TKMath',
  'TKTopAlgo', 'TKXCAF', 'TKSTEP',
  'TKXSBase', 'TKXDESTEP',
]

occt_deps = []
foreach name : occt_libs
  occt_deps += cxx.find_library(name)
endforeach

deps = [
  dependency('threads'),
  declare_dependency(dependencies: occt_deps)
]

if get_option('use_conan')
  conan = find_program('conan')
  conan_buildtype = config.get('buildtype', get_option('debug') ? 'Debug' : 'Release')

  # key should match "Find*.cmake" file, values are package references
  conan_pkgs = {
    'fmt': 'fmt/8.0.1',
    'spdlog': 'spdlog/1.9.2',
    'CLI11': 'cli11/2.0.0',
    'doctest': 'doctest/2.4.6',
  }

  foreach pkg_name, conan_ref : conan_pkgs
    module_path = meson.current_build_dir() / 'conan-cmake' / pkg_name
    run_command(
      conan, 'install', conan_ref+'@',
      '-if', module_path,
      '-g', 'cmake_find_package',
      '-s:b', 'build_type='+conan_buildtype,
      check: true
    )
    deps += dependency(pkg_name, method: 'cmake', cmake_module_path: module_path)
  endforeach
else
  deps += [
    dependency('fmt', method: 'cmake'),
    dependency('spdlog', method: 'cmake'),
    dependency('CLI11', method: 'cmake'),
    dependency('doctest', method: 'cmake'),
  ]
endif

lib_src = ['src/document.cpp', 'src/utils.cpp']

# including lib_src directly will cause test versions of them to be
# built, i.e. with preprocessor definion INCLUDE_DOCTESTS
exe = executable(
  'test_runner', 'src/test_runner.cpp', lib_src,
  dependencies : deps,
  cpp_args : '-DINCLUDE_DOCTESTS',
)

test('test', exe)

# compile non-test version of shared code once, use by all tools
lib = static_library('shared', lib_src, dependencies : deps)

shared_deps = deps + [
  declare_dependency(link_with : lib)
]

executable(
  'step_to_brep', 'src/step_to_brep.cpp',
  dependencies : shared_deps,
  install : true,
)

executable(
  'overlap_checker', 'src/overlap_checker.cpp',
  dependencies : shared_deps,
  install : true,
)

executable(
  'overlap_collecter', 'src/overlap_collecter.cpp',
  dependencies : shared_deps,
  install : true,
  link_with : lib,
)

executable(
  'imprint_solids', 'src/imprint_solids.cpp',
  dependencies : shared_deps,
  install : true,
)
