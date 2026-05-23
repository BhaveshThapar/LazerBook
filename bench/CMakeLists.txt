add_library(bench_flags INTERFACE)
target_compile_options(bench_flags INTERFACE -O3)

function(lazerbook_add_bench name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE lazerbook lazerbook_warnings bench_flags)
endfunction()

lazerbook_add_bench(bench_matcher)
lazerbook_add_bench(bench_power)
