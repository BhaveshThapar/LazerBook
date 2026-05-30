add_library(bench_flags INTERFACE)
target_compile_options(bench_flags INTERFACE -O3)

function(lazerbook_add_bench name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE lazerbook lazerbook_warnings bench_flags)
endfunction()

lazerbook_add_bench(bench_matcher)
lazerbook_add_bench(bench_power)
lazerbook_add_bench(fba_compare)
lazerbook_add_bench(fba_sweep)
lazerbook_add_bench(replay_validate)

if(LAZERBOOK_WITH_LIQUIBOOK)
    add_executable(diff_validate diff_validate.cpp)
    target_link_libraries(diff_validate PRIVATE lazerbook lazerbook_warnings bench_flags
        liquibook_iface)
endif()

# libFuzzer harness + corpus seeder (clang only).
if(LAZERBOOK_BUILD_FUZZ)
    add_executable(fuzz_itch fuzz_itch.cpp)
    target_compile_options(fuzz_itch PRIVATE -O1 -g -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_itch PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_itch PRIVATE lazerbook)

    add_executable(seed_fuzz_corpus ${CMAKE_SOURCE_DIR}/tools/seed_fuzz_corpus.cpp)
    target_link_libraries(seed_fuzz_corpus PRIVATE lazerbook bench_flags)
endif()
