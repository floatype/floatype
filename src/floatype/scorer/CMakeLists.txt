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

include(FetchContent)
# llama.cpp v1.0.0 (commit b1b770d90d94cad9e9ea5b993bdbc383a522fb28).
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG v1.0.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(llama_cpp)

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

enable_testing()
foreach(test zenz_test protocol_test)
  add_executable(${test} ${test}.cc)
  target_link_libraries(${test} PRIVATE zenz_text)
  add_test(NAME ${test} COMMAND ${test})
endforeach()
