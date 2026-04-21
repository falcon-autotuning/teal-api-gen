cmake_minimum_required(VERSION 3.20)
project(teal_inst_api_generator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies via vcpkg
# ---------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Library (reusable core logic — also used by tests)
# ---------------------------------------------------------------------------
add_library(generator_lib STATIC
    src/generator.cpp
)
target_include_directories(generator_lib PUBLIC include)
target_link_libraries(generator_lib PUBLIC nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(generator src/main.cpp)
target_link_libraries(generator PRIVATE generator_lib)

# ---------------------------------------------------------------------------
# Unit tests (Google Test)
# ---------------------------------------------------------------------------
find_package(GTest CONFIG REQUIRED)

add_executable(generator_tests
    tests/test_generator.cpp
)
target_link_libraries(generator_tests
    PRIVATE
        generator_lib
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(generator_tests)
