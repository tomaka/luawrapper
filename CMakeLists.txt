cmake_minimum_required(VERSION 2.8)
project(luawrapper)

# boost
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# lua
find_package(Lua51 REQUIRED)
include_directories(${LUA_INCLUDE_DIR})

# including lua wrapper
include_directories(include)

# flags
if (${CMAKE_COMPILER_IS_GNUCXX})
	set(CMAKE_CXX_FLAGS "-std=c++0x")
endif()

# executable
add_executable(tests
	tests/tests.cpp
)

target_link_libraries(tests
	${LUA_LIBRARIES}
)

add_test(test1 tests)
