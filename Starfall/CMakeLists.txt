file(GLOB_RECURSE STARFALL_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.h)

add_library(Starfall STATIC ${STARFALL_SOURCES})
target_include_directories(Starfall PUBLIC Source)
target_link_libraries(Starfall PUBLIC glm::glm nlohmann_json::nlohmann_json nvrhi nvrhi_vk Vulkan::Headers glfw imgui_lib)
target_link_libraries(Starfall PRIVATE Jolt sol2 lua_lib miniaudio_lib stb_lib cgltf_lib)
target_compile_definitions(Starfall PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN VK_NO_PROTOTYPES $<$<CONFIG:Debug>:SF_DEBUG> GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)
if(MSVC)
	target_compile_options(Starfall PRIVATE /W4 /permissive- /utf-8 /MP /bigobj)
else()
	target_compile_options(Starfall PRIVATE -Wall -Wextra -Wpedantic)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${STARFALL_SOURCES})

# Engine shaders are compiled to SPIR-V next to the executables (bin/Resources/Shaders).
starfall_compile_shaders(Starfall ${CMAKE_SOURCE_DIR}/Resources/Shaders ${CMAKE_BINARY_DIR}/bin/Resources/Shaders)
