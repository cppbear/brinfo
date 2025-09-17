cmake_minimum_required(VERSION 3.28)

project(BrInfo VERSION 0.1.0 LANGUAGES CXX C)

include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

find_package(LLVM 17 REQUIRED CONFIG)
find_package(Clang 17 REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

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

add_executable(brinfo
  BrInfo.cpp
  Analysis.cpp
  Condition.cpp
  CondChain.cpp
  Meta.cpp
  MetaBridge.cpp
)

target_link_libraries(brinfo
  PRIVATE
  clangFrontend
  clangTooling
  nlohmann_json::nlohmann_json
  $<$<BOOL:${HAVE_XXHASH}>:${XXHASH_LIBRARY}>
  )

if(HAVE_XXHASH)
  target_compile_definitions(brinfo PRIVATE BRINFO_HAVE_XXHASH=1)
endif()

# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     target_compile_definitions(brinfo PRIVATE DEBUG)
# endif()

install(TARGETS brinfo
  RUNTIME DESTINATION bin)
