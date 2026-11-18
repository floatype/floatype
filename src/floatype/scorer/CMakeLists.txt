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

include(FetchContent)
# llama.cpp v1.0.0 (commit b1b770d90d94cad9e9ea5b993bdbc383a522fb28).
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG v1.0.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(llama_cpp)

add_executable(zenz_eval zenz_eval.cc)
target_link_libraries(zenz_eval PRIVATE llama)
