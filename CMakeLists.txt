cmake_minimum_required(VERSION 3.28)

project(BrInfo VERSION 0.1.0 LANGUAGES CXX C)

include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

find_package(LLVM 17 REQUIRED CONFIG)
find_package(Clang 17 REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

add_executable(brinfo
  BrInfo.cpp
  Analysis.cpp
  Condition.cpp
  CondChain.cpp
  )

target_link_libraries(brinfo
  PRIVATE
  clangFrontend
  clangTooling
  nlohmann_json::nlohmann_json
  )

# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     target_compile_definitions(brinfo PRIVATE DEBUG)
# endif()

install(TARGETS brinfo
  RUNTIME DESTINATION bin)
