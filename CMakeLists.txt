cmake_minimum_required(VERSION 3.10)

project(overlap_checker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

include(CheckSymbolExists)
include(CTest)

check_symbol_exists(argp_parse "argp.h" HAVE_ARGP)
if(NOT (HAVE_ARGP))
  message(SEND_ERROR "GNU argp is required for command line parsing")
endif()

# OpenCascade stuff, should be using proper cmake file but not sure
# which one(s)
find_path(OCC_INC "Standard_Version.hxx" HINTS "/usr/include/opencascade" REQUIRED)
include_directories(SYSTEM ${OCC_INC})

# pull in our external header-only libraries
include_directories(SYSTEM "aixlog/include")
include_directories(SYSTEM "cxx_argp")
add_subdirectory(Catch2)

# place binaries in build rather than build/src
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_subdirectory(src)
