cmake_minimum_required(VERSION 3.16)
project(mortar C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(mortar_core STATIC
    src/util.c
    src/arena.c
    src/mem.c
    src/htab.c
    src/store.c
)
target_include_directories(mortar_core PUBLIC src)
target_compile_definitions(mortar_core PUBLIC _POSIX_C_SOURCE=200809L)

add_executable(mortar src/main.c)
target_link_libraries(mortar PRIVATE mortar_core)

add_executable(mortar_bench bench/bench.c)
target_link_libraries(mortar_bench PRIVATE mortar_core)

enable_testing()
foreach(t arena mem htab store)
    add_executable(test_${t} tests/test_${t}.c)
    target_link_libraries(test_${t} PRIVATE mortar_core)
    target_include_directories(test_${t} PRIVATE tests)
    add_test(NAME test_${t} COMMAND test_${t})
endforeach()

option(MORTAR_SANITIZERS "Build everything with ASan + UBSan" OFF)
if(MORTAR_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
endif()
