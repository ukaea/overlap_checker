project('parallel-preprocessor', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++17'])

config = configuration_data()
cxx = meson.get_compiler('cpp')

conan_pkgs= {
    'fmt': 'fmt/8.0.1@', # <- Must contain @, otherwise Conan will think it is a path
    'spdlog': 'spdlog/1.9.2@',
}

conan = find_program('conan')
conan_buildtype = config.get('buildtype', get_option('debug') ? 'Debug' : 'Release')

deps=[]
foreach pkg_name, conan_ref : conan_pkgs
    module_path = meson.current_build_dir() / 'conan-cmake' / pkg_name
    run_command(conan, 'install', conan_ref,
        '-if', module_path,
        '-g', 'cmake_find_package',
        '-s:b', 'build_type='+conan_buildtype,
        check: true
    )
    deps += dependency(pkg_name, method: 'cmake', cmake_module_path: module_path)
endforeach

deps += declare_dependency(
  dependencies : [
    cxx.find_library('pthread')
  ],
)

deps += declare_dependency(
  dependencies : [
    cxx.find_library('TKernel'),
    cxx.find_library('TKMath'),
    cxx.find_library('TKBRep'),
    cxx.find_library('TKTopAlgo'),
    cxx.find_library('TKLCAF'),
    cxx.find_library('TKXCAF'),
    cxx.find_library('TKXDESTEP'),
  ],
  include_directories : ['/usr/include/opencascade'],
)

exe = executable('parallel_preprocessor', ['src/parallel_preprocessor.cpp'],
  dependencies : deps,
  install : true,
)

test('basic', exe)
