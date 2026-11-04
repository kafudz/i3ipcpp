# Project setup

cmake_minimum_required(VERSION 3.5.0 FATAL_ERROR)
project(i3ipc++ CXX)

option(WITH_LOGGING "Build with log support" OFF)
option(WITH_TESTS "Build unit tests executables" OFF)
option(BUILD_EXAMPLES "Build example executables" OFF)

add_library(${PROJECT_NAME} STATIC
  ${PROJECT_SOURCE_DIR}/3rd/auss/include/auss.hpp
  ${PROJECT_SOURCE_DIR}/include/i3ipc++/ipc.hpp
  ${PROJECT_SOURCE_DIR}/include/i3ipc++/ipc-util.hpp
  ${PROJECT_SOURCE_DIR}/include/i3ipc++/log.hpp
  ${PROJECT_SOURCE_DIR}/src/ipc.cpp
  ${PROJECT_SOURCE_DIR}/src/ipc-util.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/3rd/auss/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)

# Use the standard of the compiler's flag instead of a hardcoded -std=c++11 (which also broke clang with newer libs)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-g3>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:-O2>)

# The logging macros are used in the sources only, but the user could include log.hpp, so the options are public
if(WITH_LOGGING)
  target_compile_definitions(${PROJECT_NAME} PUBLIC WITH_LOGGING)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)

# External library: jsoncpp {{{

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

target_link_libraries(${PROJECT_NAME} PUBLIC ${JSONCPP_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${JSONCPP_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PUBLIC ${JSONCPP_LIBRARY_DIRS})

# }}}
# Export lists to the parent scope if there are any {{{

get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if(HAS_PARENT)
  set(I3IPCPP_LIBRARIES ${PROJECT_NAME} PARENT_SCOPE)
  set(I3IPCPP_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/include ${JSONCPP_INCLUDE_DIRS} PARENT_SCOPE)
  # Kept for the compatibility with the previous (differently cased) names, see README
  set(I3IPCpp_LIBRARIES ${PROJECT_NAME} PARENT_SCOPE)
  set(I3IPCpp_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/include ${JSONCPP_INCLUDE_DIRS} PARENT_SCOPE)
endif()

# }}}
# Build examples if the option was given {{{

if(BUILD_EXAMPLES)
  add_subdirectory("${PROJECT_SOURCE_DIR}/examples")
endif()

# }}}
# Build cpp tests if the option was given {{{

if(WITH_TESTS)
  find_package(CxxTest)
  if(CXXTEST_FOUND)
    include_directories(${CXXTEST_INCLUDE_DIR})
    enable_testing()
    file(GLOB SRC_TEST "${PROJECT_SOURCE_DIR}/test/*.hpp")
    CXXTEST_ADD_TEST(i3ipcpp_check test.cpp ${SRC_TEST})
    target_link_libraries(i3ipcpp_check ${PROJECT_NAME})
  else()
    message(WARNING "CxxTest not found. Unable to run unit-tests")
  endif()
endif()

# }}}
