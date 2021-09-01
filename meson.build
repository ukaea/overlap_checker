project('parallel-preprocessor', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++17'])

# opencascade preprocessor macros result in lots of these
add_project_arguments('-Wno-extra-semi', language : 'cpp')

config = configuration_data()
cxx = meson.get_compiler('cpp')

# key should match "Find*.cmake" file, values are package references
conan_pkgs= {
    'fmt': 'fmt/8.0.1',
    'spdlog': 'spdlog/1.9.2',
    'CLI11': 'cli11/2.0.0',
}

conan = find_program('conan')
conan_buildtype = config.get('buildtype', get_option('debug') ? 'Debug' : 'Release')

deps=[
    dependency('Threads', method: 'cmake'),
]

foreach pkg_name, conan_ref : conan_pkgs
    module_path = meson.current_build_dir() / 'conan-cmake' / pkg_name
    run_command(conan, 'install', conan_ref+'@',
        '-if', module_path,
        '-g', 'cmake_find_package',
        '-s:b', 'build_type='+conan_buildtype,
        check: true
    )
    deps += dependency(pkg_name, method: 'cmake', cmake_module_path: module_path)
endforeach

# locate opencascade dependencies, can't see any CMake automatics for this :(
deps += declare_dependency(
  dependencies : [
    cxx.find_library('TKBO'),
    cxx.find_library('TKBRep'),
    cxx.find_library('TKG3d'),
    cxx.find_library('TKLCAF'),
    cxx.find_library('TKMath'),
    cxx.find_library('TKTopAlgo'),
    cxx.find_library('TKXCAF'),
    cxx.find_library('TKXDESTEP'),
    cxx.find_library('TKernel'),
  ],
  include_directories : ['/usr/include/opencascade'],
)

exe = executable('parallel_preprocessor', ['src/parallel_preprocessor.cpp', 'src/document.cpp'],
  dependencies : deps,
  install : true,
)

test('basic', exe)

executable('step_to_brep', ['src/step_to_brep.cpp', 'src/document.cpp'],
  dependencies : deps,
  install : true,
)
