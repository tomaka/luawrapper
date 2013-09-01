cmake_minimum_required(VERSION 2.8)
project(luawrapper)

set(CMAKE_GENERATOR_TOOLSET "v120" CACHE STRING "")			# building for VC++ 2013


# boost
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# lua
find_package(Lua51 REQUIRED)
include_directories(${LUA_INCLUDE_DIR})

# including lua wrapper
include_directories(include)

# setting up external project
include(ExternalProject)

# gtest
ExternalProject_Add(
    googletest
    SVN_REPOSITORY http://googletest.googlecode.com/svn/trunk/
    SVN_REVISION -r 660
    TIMEOUT 10
    # Force separate output paths for debug and release builds to allow easy
    # identification of correct lib in subsequent TARGET_LINK_LIBRARIES commands
    CMAKE_ARGS -Dgtest_force_shared_crt=ON
    UPDATE_COMMAND ""
    INSTALL_COMMAND ""
)
include_directories(${CMAKE_BINARY_DIR}/googletest-prefix/src/googletest/include)

# flags
if (${CMAKE_COMPILER_IS_GNUCXX})
	set(CMAKE_CXX_FLAGS "-std=c++11")
endif()
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(CMAKE_CXX_FLAGS "-std=c++11")
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
)

add_dependencies(tests googletest)
target_link_libraries(tests
	${LUA_LIBRARIES}
	${CMAKE_BINARY_DIR}/googletest-prefix/src/googletest-build/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a
	${CMAKE_BINARY_DIR}/googletest-prefix/src/googletest-build/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a
)

# hack
if (${UNIX})
	target_link_libraries(tests
		pthread
	)
endif()

enable_testing()
add_test(tests tests)
