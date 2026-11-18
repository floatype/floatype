cmake_minimum_required(VERSION 3.21)
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

include(FetchContent)
# llama.cpp v1.0.0 (commit b1b770d90d94cad9e9ea5b993bdbc383a522fb28).
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG v1.0.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(llama_cpp)

add_library(zenz_text STATIC zenz.cc protocol.cc)

add_executable(floatype_scorer scorer_main.cc)
target_link_libraries(floatype_scorer PRIVATE zenz_text llama)

enable_testing()
foreach(test zenz_test protocol_test)
  add_executable(${test} ${test}.cc)
  target_link_libraries(${test} PRIVATE zenz_text)
  add_test(NAME ${test} COMMAND ${test})
endforeach()
