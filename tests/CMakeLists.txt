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
    test_matcher.cpp
    test_reconstruct.cpp
    test_fba.cpp
    test_sim.cpp
    test_diff.cpp
    test_spsc.cpp
    test_replay.cpp
    test_histogram.cpp
    test_diff_liquibook.cpp
)

target_link_libraries(test_lazerbook PRIVATE
    lazerbook doctest::doctest
    lazerbook_warnings lazerbook_sanitizers lazerbook_coverage)
find_package(Threads REQUIRED)
target_link_libraries(test_lazerbook PRIVATE Threads::Threads)

# test_diff_liquibook.cpp includes <book/order_book.h>; without this the gated
# cases never compile, which is why they had never run.
if(LAZERBOOK_WITH_LIQUIBOOK)
    target_link_libraries(test_lazerbook PRIVATE liquibook_iface)
endif()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(test_lazerbook)
