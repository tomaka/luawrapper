cmake_minimum_required(VERSION 2.8)
project(luawrapper)

# boost
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# lua
find_package(Lua51 REQUIRED)
include_directories(${LUA_INCLUDE_DIR})

# gtest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# including lua wrapper
include_directories(include)

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

target_link_libraries(tests
	${LUA_LIBRARIES}
	${GTEST_BOTH_LIBRARIES}
)

enable_testing()
add_test(tests tests)
