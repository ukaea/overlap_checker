cmake_minimum_required(VERSION 3.10)

project(overlap_checker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

include(CheckSymbolExists)
check_symbol_exists(argp_parse "argp.h" HAVE_ARGP)
if(NOT (HAVE_ARGP))
  message(SEND_ERROR "GNU argp is required for command line parsing")
endif()

# OpenCascade stuff
find_path(OCC_INC "Standard_Version.hxx" HINTS "/usr/include/opencascade" REQUIRED)
include_directories(SYSTEM ${OCC_INC})
link_libraries(
  TKernel
  TKBO TKBRep TKPrim
  TKG3d TKLCAF TKMath
  TKTopAlgo TKXCAF TKSTEP
  TKXSBase TKXDESTEP TKShHealing)

# pull in our external header-only libraries
include_directories(SYSTEM "include")

#find_package(Catch2 REQUIRED)
#add_executable(test_runner src/test_runner.cpp src/document.cpp src/utils.cpp)
#target_link_libraries(test_runner Catch2::Catch2)
#target_compile_definitions(test_runner PUBLIC -DINCLUDE_TESTS)

add_library(shared OBJECT src/utils.cpp src/document.cpp)

add_executable(step_to_brep src/step_to_brep.cpp $<TARGET_OBJECTS:shared>)

add_executable(overlap_checker src/overlap_checker.cpp $<TARGET_OBJECTS:shared>)
target_link_libraries(overlap_checker pthread)

add_executable(overlap_collecter src/overlap_collecter.cpp $<TARGET_OBJECTS:shared>)

add_executable(imprint_solids src/imprint_solids.cpp $<TARGET_OBJECTS:shared>)

