cmake_minimum_required(VERSION 3.13)

project(overlap_checker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include(CheckSymbolExists)
include(CTest)

# Code Coverage Configuration
add_library(coverage_config INTERFACE)

option(CODE_COVERAGE "Enable coverage reporting" OFF)
if(CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Add required flags (GCC & LLVM/Clang)
  target_compile_options(coverage_config INTERFACE
    -g -O0        # debug, no optimization
    -fprofile-instr-generate -fcoverage-mapping
    )
  target_link_options(coverage_config INTERFACE
    -fprofile-instr-generate -fcoverage-mapping)
elseif(CODE_COVERAGE)
  message(SEND_ERROR "Clang is required for coverage at the moment")
endif()

option(WERROR "Enable -Werror compile flag" OFF)
if(WERROR)
  add_compile_options(-Werror)
endif()

check_symbol_exists(argp_parse "argp.h" HAVE_ARGP)
if(NOT HAVE_ARGP)
  message(SEND_ERROR "GNU argp is required for command line parsing")
endif()

# OpenCascade stuff, should be using proper cmake file but not sure
# which one(s)
find_path(OCC_INC "Standard_Version.hxx" HINTS "/usr/include/opencascade" REQUIRED)
include_directories(SYSTEM ${OCC_INC})

# pull in our external libraries
include_directories(SYSTEM "aixlog/include")
include_directories(SYSTEM "cxx_argp")

if (BUILD_TESTING)
  add_subdirectory(Catch2)
endif()

# place binaries in build rather than build/src
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_subdirectory(src)
