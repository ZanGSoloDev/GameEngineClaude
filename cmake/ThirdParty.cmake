# Targets for dependencies that do not ship their own CMake build.

# ---- Lua (C library; built as C) ----
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\.c$")
add_library(lua_lib STATIC ${LUA_SOURCES})
target_include_directories(lua_lib SYSTEM PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_lib PROPERTIES FOLDER "ThirdParty" LINKER_LANGUAGE C)
if(UNIX)
	target_compile_definitions(lua_lib PRIVATE LUA_USE_POSIX)
endif()

# ---- miniaudio ----
add_library(miniaudio_lib STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio_lib SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
if(UNIX AND NOT APPLE)
	target_link_libraries(miniaudio_lib PUBLIC dl pthread m)
endif()
set_target_properties(miniaudio_lib PROPERTIES FOLDER "ThirdParty")

# ---- Dear ImGui + ImGuizmo ----
add_library(imgui_lib STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(imgui_lib SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imgui_lib PUBLIC glfw)
set_target_properties(imgui_lib PROPERTIES FOLDER "ThirdParty")

# ---- Header-only: cgltf, stb ----
add_library(cgltf_lib INTERFACE)
target_include_directories(cgltf_lib SYSTEM INTERFACE ${cgltf_SOURCE_DIR})
add_library(stb_lib INTERFACE)
target_include_directories(stb_lib SYSTEM INTERFACE ${stb_SOURCE_DIR})

# Jolt enables warnings-as-errors; newer toolchains / SDK headers emit warnings we cannot fix upstream.
if(MSVC)
	target_compile_options(Jolt PRIVATE /WX-)
else()
	target_compile_options(Jolt PRIVATE -Wno-error)
endif()
