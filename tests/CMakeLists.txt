find_package(GTest REQUIRED)
include(GoogleTest)

find_package(sol2 CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)

set(DLL_SEARCH_PATHS "${CMAKE_BINARY_DIR}}/tests" "${CMAKE_BINARY_DIR}")
string(REPLACE ";" "\\;" TEST_PATH_ENV "${DLL_SEARCH_PATHS}")
set(TEST_PATH_ENV "${TEST_PATH_ENV};$ENV{PATH}")

add_executable(
  unit_tests
  test_generator.cpp
  test_utils/MockLuaContext.cpp)

target_link_libraries(unit_tests PRIVATE teal-api-gen
                                         yaml-cpp::yaml-cpp
                                         lua
                                         sol2::sol2
                                         GTest::gtest
                                         GTest::gtest_main)

target_include_directories(unit_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_utils)

add_test(NAME unit_tests COMMAND unit_tests)
if(WIN32)
  set_tests_properties(unit_tests PROPERTIES ENVIRONMENT "PATH=${TEST_PATH_ENV}")
else()
  set_tests_properties(unit_tests PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:unit_tests>/..:$<TARGET_FILE_DIR:unit_tests>:$ENV{PATH}")
endif()
