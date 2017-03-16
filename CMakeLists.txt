cmake_minimum_required(VERSION 2.8)
project(luawrapper)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR})
set(CMAKE_GENERATOR_TOOLSET "v120" CACHE STRING "")			# building for VC++ 2013


# boost
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# lua
find_package(Lua51)
find_package(Lua52)
find_package(LuaJIT51)
if (NOT ${LUA51_FOUND} AND NOT ${LUA52_FOUND} AND NOT ${LUAJIT51_FOUND})
	message(FATAL_ERROR "Could not find lua library")
endif()
include_directories(${LUA_INCLUDE_DIR})

# including lua wrapper
include_directories(include)

# setting up external project
include(ExternalProject)

# gtest
find_package(Git REQUIRED)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/ThirdParty/googletest-src)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/ThirdParty/googletest-bin)
execute_process(COMMAND ${GIT_EXECUTABLE} clone https://github.com/google/googletest.git googletest-src WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/ThirdParty)
add_subdirectory(${CMAKE_BINARY_DIR}/ThirdParty/googletest-src ThirdParty/googletest-bin)
include_directories(${CMAKE_BINARY_DIR}/ThirdParty/googletest-src/include)

# flags
if (${CMAKE_COMPILER_IS_GNUCXX})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

# executable
add_executable(tests
	tests/main.cpp
	tests/basic_readwrite.cpp
	tests/advanced_readwrite.cpp
	tests/execution.cpp
	tests/functions_write.cpp
	tests/functions_read.cpp
	tests/custom_types.cpp
	tests/movable.cpp
	tests/metatables.cpp
	tests/threads.cpp
)

target_link_libraries(tests
	${LUA_LIBRARIES}
	gtest
	gtest_main
)

# hack
if (${UNIX})
	target_link_libraries(tests
		pthread
	)
endif()

enable_testing()
add_test(tests tests)
