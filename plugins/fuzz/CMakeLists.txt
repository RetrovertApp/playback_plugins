# Fuzz targets for playback plugin parsers
#
# Uses libFuzzer (built into clang) with AddressSanitizer.
# Build:  cmake -B build_fuzz -G Ninja -DENABLE_FUZZ=ON && cmake --build build_fuzz
# Run:    ./build_fuzz/fuzz/fuzz_aon fuzz/corpus/art_of_noise -max_len=1048576
#
# Each fuzz target links the plugin's parser code directly (no shared lib loading).

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Fuzzing requires Clang (libFuzzer). Set CC=clang CXX=clang++")
endif()

set(FUZZ_SANITIZER_FLAGS -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)

# Helper: create a fuzz target that compiles plugin source directly
function(add_fuzz_target name)
    cmake_parse_arguments(FUZZ "" "" "SOURCES;INCLUDE_DIRS" ${ARGN})

    add_executable(${name} ${FUZZ_SOURCES})
    target_compile_options(${name} PRIVATE ${FUZZ_SANITIZER_FLAGS} -Wall -Wextra)
    target_link_options(${name} PRIVATE ${FUZZ_SANITIZER_FLAGS})
    target_include_directories(${name} PRIVATE ${FUZZ_INCLUDE_DIRS})

    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fuzz"
    )
endfunction()

# -- Art of Noise fuzz target --
add_fuzz_target(fuzz_aon
    SOURCES
        fuzz_aon.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../art_of_noise/aon_player.c
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/../art_of_noise
)
