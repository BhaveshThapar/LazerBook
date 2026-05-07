include(FetchContent)

# doctest predates CMake 4's policy floor; relax it for FetchContent.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

add_executable(test_lazerbook
    test_main.cpp
    test_types.cpp
    test_itch.cpp
    test_pool.cpp
    test_book.cpp
)

target_link_libraries(test_lazerbook PRIVATE
    lazerbook doctest::doctest
    lazerbook_warnings lazerbook_sanitizers lazerbook_coverage)
find_package(Threads REQUIRED)
target_link_libraries(test_lazerbook PRIVATE Threads::Threads)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(test_lazerbook)
