# all targets use OpenCascade so not much point using
# target_link_libraries
link_libraries(
  TKernel
  TKBO TKBRep TKPrim
  TKG2d TKG3d TKLCAF TKMath
  TKTopAlgo TKXCAF TKSTEP
  TKXSBase TKXDESTEP TKShHealing
  TKGeomAlgo)

add_library(shared OBJECT utils.cpp geometry.cpp thread_pool.cpp)

link_libraries(pthread)

add_executable(step_to_brep step_to_brep.cpp $<TARGET_OBJECTS:shared>)

add_executable(overlap_checker overlap_checker.cpp $<TARGET_OBJECTS:shared>)

add_executable(overlap_collecter overlap_collecter.cpp $<TARGET_OBJECTS:shared>)

add_executable(imprint_solids imprint_solids.cpp $<TARGET_OBJECTS:shared>)

add_executable(merge_solids merge_solids.cpp salome/geom_gluer.cpp $<TARGET_OBJECTS:shared>)

if(BUILD_TESTING)
  add_executable(test_runner geometry.cpp utils.cpp thread_pool.cpp)
  target_compile_definitions(test_runner PUBLIC -DINCLUDE_TESTS)
  target_link_libraries(test_runner Catch2WithMain)

  add_test(NAME unit-tests COMMAND test_runner)

  add_test(NAME shape-merge
    COMMAND bash ${PROJECT_SOURCE_DIR}/tests/test_merge_solids.sh
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
endif()
