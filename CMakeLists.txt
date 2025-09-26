cmake_minimum_required(VERSION 3.28)

project(BrInfo VERSION 0.1.0 LANGUAGES CXX C)

include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

find_package(LLVM 17 REQUIRED CONFIG)
find_package(Clang 17 REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Try to find xxHash (system installed). If found, define macro.
find_path(XXHASH_INCLUDE_DIR xxhash.h)
find_library(XXHASH_LIBRARY NAMES xxhash libxxhash)
if(XXHASH_INCLUDE_DIR AND XXHASH_LIBRARY)
  message(STATUS "Found xxHash: ${XXHASH_INCLUDE_DIR} ${XXHASH_LIBRARY}")
  include_directories(${XXHASH_INCLUDE_DIR})
  set(HAVE_XXHASH TRUE)
else()
  message(STATUS "xxHash not found; fallback to FNV-1a")
  set(HAVE_XXHASH FALSE)
endif()

add_library(brinfo_core
  src/core/Analysis.cpp
  src/core/Condition.cpp
  src/core/CondChain.cpp
  src/core/Meta.cpp
  src/core/MetaBridge.cpp
)
target_include_directories(brinfo_core PUBLIC include)
target_link_libraries(brinfo_core PUBLIC
  clangFrontend
  clangTooling
  nlohmann_json::nlohmann_json
  $<$<BOOL:${HAVE_XXHASH}>:${XXHASH_LIBRARY}>
)

add_executable(brinfo
  src/frontend/BrInfo.cpp
  # src/frontend/Matcher.cpp
)
target_link_libraries(brinfo PRIVATE brinfo_core)

if(HAVE_XXHASH)
  target_compile_definitions(brinfo_core PUBLIC BRINFO_HAVE_XXHASH=1)
  target_compile_definitions(brinfo PRIVATE BRINFO_HAVE_XXHASH=1)
endif()

# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     target_compile_definitions(brinfo PRIVATE DEBUG)
# endif()

install(TARGETS brinfo
  RUNTIME DESTINATION bin)

add_library(brinfo_runtime
  src/runtime/Runtime.cpp
)
target_include_directories(brinfo_runtime PUBLIC include)

add_executable(brinfo_inst
  src/instrumenter/Instrumenter.cpp
)
target_link_libraries(brinfo_inst PRIVATE
  clangFrontend
  clangRewrite
  clangTooling
  brinfo_runtime
)
  if(HAVE_XXHASH)
    target_compile_definitions(brinfo_inst PRIVATE BRINFO_HAVE_XXHASH=1)
    target_link_libraries(brinfo_inst PRIVATE ${XXHASH_LIBRARY})
  endif()

install(TARGETS brinfo_inst
  RUNTIME DESTINATION bin)

add_executable(brinfo_callwrap
  src/rewrite/AutoWrapInvocations.cpp
)
target_link_libraries(brinfo_callwrap PRIVATE
  clangFrontend
  clangTooling
  clangRewrite
)
target_include_directories(brinfo_callwrap PRIVATE include)

install(TARGETS brinfo_callwrap
  RUNTIME DESTINATION bin)
