cmake_minimum_required(VERSION 3.24)
project(Starfall LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Static C runtime on Windows so exported games run without the Visual C++ redistributable.
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(cmake/Dependencies.cmake)
include(cmake/ThirdParty.cmake)

enable_testing()
add_subdirectory(Tools)
add_subdirectory(Starfall)
add_subdirectory(StarfallRuntime)
add_subdirectory(Tests)
