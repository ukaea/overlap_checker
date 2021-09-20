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
  # e.g. having installed dependencies via Conda:
  #
  #   apt-get update && apt-get install -y libgl1 cmake g++
  #   conda install ninja meson occt fmt spdlog cli11 doctest
  #
  # then set environment variables
  #
  #   CXXFLAGS=-I/opt/conda/include/opencascade
  #   LDFLAGS=-L/opt/conda/lib
  deps += [
    dependency('fmt', method: 'cmake'),
    dependency('spdlog', method: 'cmake'),
    dependency('CLI11', method: 'cmake'),
    dependency('doctest', method: 'cmake'),
  ]
endif

exe = executable('test_runner', ['src/test_runner.cpp', 'src/document.cpp', 'src/utils.cpp'],
  dependencies : deps,
  cpp_args : '-DINCLUDE_DOCTESTS',
)

test('test', exe)

executable('step_to_brep', ['src/step_to_brep.cpp', 'src/document.cpp', 'src/utils.cpp'],
  dependencies : deps,
  install : true,
)

executable('overlap_checker', ['src/overlap_checker.cpp', 'src/document.cpp', 'src/utils.cpp'],
  dependencies : deps,
  install : true,
)

executable('overlap_collecter', ['src/overlap_collecter.cpp', 'src/document.cpp', 'src/utils.cpp'],
  dependencies : deps,
  install : true,
)
