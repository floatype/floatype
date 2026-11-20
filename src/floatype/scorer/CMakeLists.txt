cmake_minimum_required(VERSION 3.21)
# Same as Mozc's MACOS_MIN_OS_VER in config.bzl.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
project(floatype_scorer C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only libllama and ggml are built: no common library, tools, server or model downloader.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)

# CPU only. On an M4 Pro with AJIMEE-Bench, 4 CPU threads match Metal's latency
# (p50 26ms vs 30ms) with a smaller footprint (95MB vs 138MB), give the same
# output for any thread count, and leave the GPU to the user's apps.
# Pass -DGGML_METAL=ON to compare.
set(GGML_METAL OFF CACHE BOOL "ggml: use Metal")
# Accelerate's BLAS (cblas_sgemm) needs macOS 13.3, and on AJIMEE-Bench the
# scorer gives the same output at the same speed without it.
set(GGML_BLAS OFF CACHE BOOL "ggml: use BLAS")

# The SHA-256 of the repacked weights, which llama.cpp at the tag below
# writes for the model below.
set(ZENZ_REPACKED_SHA256 32cd2437d6b3d998e737d2eb4d1b433b25410c3ec7519a060af0b9e1ad35e5b6)

include(FetchContent)
# llama.cpp v1.0.0 (commit b1b770d90d94cad9e9ea5b993bdbc383a522fb28).
# SOURCE_SUBDIR names no directory, so this only fetches: the patch goes on
# before the build reads the sources, in a tree fetched earlier too.
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG v1.0.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR fetched-only
)
FetchContent_MakeAvailable(llama_cpp)
# Lets the scorer say where the repacked weights live (--repacked).
execute_process(
  COMMAND ${CMAKE_COMMAND}
    -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/llama_repack_memory.patch
    -DDIR=${llama_cpp_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/apply_patch.cmake
  COMMAND_ERROR_IS_FATAL ANY)
add_subdirectory(${llama_cpp_SOURCE_DIR} ${llama_cpp_BINARY_DIR})

# The zenz model bundled with the scorer (--define=floatype_zenz=bundle in
# BUILD.bazel). Its license is CC BY-SA 4.0.
set(ZENZ_MODEL ${CMAKE_BINARY_DIR}/zenz-v3.2-small-Q5_K_M.gguf)
if(NOT EXISTS ${ZENZ_MODEL})
  file(DOWNLOAD
    https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/resolve/ef3e66e3665fe6a19598e6578e9b88a994b647fd/ggml-model-Q5_K_M.gguf
    ${ZENZ_MODEL}
    EXPECTED_HASH SHA256=29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673
    STATUS ZENZ_MODEL_STATUS)
  list(GET ZENZ_MODEL_STATUS 0 ZENZ_MODEL_ERROR)
  if(NOT ZENZ_MODEL_ERROR EQUAL 0)
    file(REMOVE ${ZENZ_MODEL})
    message(FATAL_ERROR "Cannot download the zenz model: ${ZENZ_MODEL_STATUS}")
  endif()
endif()

add_library(zenz_text STATIC zenz.cc protocol.cc)

add_executable(floatype_scorer scorer_main.cc)
target_link_libraries(floatype_scorer PRIVATE zenz_text llama)

# The model's weights as llama.cpp repacks them for the CPU's fast kernels,
# bundled with the scorer. The scorer maps the file instead of repacking into
# memory of its own when it starts: the same answers at the same speed, with
# a footprint of about 30MB instead of 96MB on an M4 Pro. The layout is the same on
# every Apple Silicon Mac, since the build fixes which kernels are used.
set(ZENZ_REPACKED ${CMAKE_BINARY_DIR}/zenz-v3.2-small-Q5_K_M.repacked)
add_custom_command(
  OUTPUT ${ZENZ_REPACKED}
  COMMAND floatype_scorer --model ${ZENZ_MODEL} --write_repacked ${ZENZ_REPACKED}
  COMMAND ${CMAKE_COMMAND} -DFILE=${ZENZ_REPACKED}
    -DEXPECTED=${ZENZ_REPACKED_SHA256}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_sha256.cmake
  DEPENDS floatype_scorer ${ZENZ_MODEL}
  VERBATIM)
add_custom_target(zenz_repacked ALL DEPENDS ${ZENZ_REPACKED})

enable_testing()
foreach(test zenz_test protocol_test)
  add_executable(${test} ${test}.cc)
  target_link_libraries(${test} PRIVATE zenz_text)
  add_test(NAME ${test} COMMAND ${test})
endforeach()
# Runs the scorer with the model.
add_executable(scorer_test scorer_test.cc)
target_link_libraries(scorer_test PRIVATE zenz_text)
add_test(NAME scorer_test
  COMMAND scorer_test $<TARGET_FILE:floatype_scorer> ${ZENZ_MODEL}
    ${ZENZ_REPACKED})
