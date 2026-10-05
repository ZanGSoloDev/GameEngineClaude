add_executable(ShaderCompiler ShaderCompiler/main.cpp)
target_link_libraries(ShaderCompiler PRIVATE glslang SPIRV glslang-default-resource-limits)
set_target_properties(ShaderCompiler PROPERTIES FOLDER "Tools" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

# starfall_compile_shaders(<target> <shader dir> <output dir>)
# Compiles every *.vert/*.frag/*.comp in <shader dir> to <output dir>/<name>.spv and makes <target> depend on them.
function(starfall_compile_shaders TARGET SHADER_DIR OUTPUT_DIR)
	file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag ${SHADER_DIR}/*.comp)
	file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_DIR}/Include/*.glsl)
	set(OUTPUTS "")
	foreach(SHADER ${SHADER_SOURCES})
		get_filename_component(NAME ${SHADER} NAME)
		set(OUT ${OUTPUT_DIR}/${NAME}.spv)
		add_custom_command(
			OUTPUT ${OUT}
			COMMAND ShaderCompiler ${SHADER} ${OUT} -I ${SHADER_DIR}/Include
			DEPENDS ${SHADER} ${SHADER_INCLUDES} ShaderCompiler
			COMMENT "Compiling shader ${NAME}"
			VERBATIM)
		list(APPEND OUTPUTS ${OUT})
	endforeach()
	add_custom_target(${TARGET}_Shaders DEPENDS ${OUTPUTS})
	set_target_properties(${TARGET}_Shaders PROPERTIES FOLDER "Tools")
	add_dependencies(${TARGET} ${TARGET}_Shaders)
endfunction()

add_executable(GenerateTestProject GenerateTestProject/main.cpp)
target_link_libraries(GenerateTestProject PRIVATE Starfall)
set_target_properties(GenerateTestProject PROPERTIES FOLDER "Tools" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
