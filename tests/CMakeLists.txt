find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(engine_matching_test engine_matching_test.cpp)
target_include_directories(engine_matching_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(engine_matching_test PRIVATE engine GTest::gtest_main)

gtest_discover_tests(engine_matching_test)
