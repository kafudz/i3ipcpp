cmake_minimum_required(VERSION 3.5)
project(i3ipc++-examples CXX)

# Normally the library target (i3ipc++) is already defined by the parent project and brings its include directories
# and dependencies. The variables below are the fallback for building the examples against an installed copy
if(NOT TARGET i3ipc++)
  include_directories(${I3IPCPP_INCLUDE_DIRS})
  set(I3IPC_EXAMPLES_LIBS ${I3IPCPP_LIBRARIES})
else()
  set(I3IPC_EXAMPLES_LIBS i3ipc++)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

add_executable(workspaces workspaces.cpp)
target_link_libraries(workspaces ${I3IPC_EXAMPLES_LIBS})

add_executable(events events.cpp)
target_link_libraries(events ${I3IPC_EXAMPLES_LIBS})

add_executable(bar-configs bar-configs.cpp)
target_link_libraries(bar-configs ${I3IPC_EXAMPLES_LIBS})
