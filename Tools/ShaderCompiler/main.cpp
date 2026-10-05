// Offline GLSL -> SPIR-V compiler used by the build.
// Usage: ShaderCompiler <input.{vert,frag,comp}> <output.spv> [-I <include dir>]... [-D NAME[=VALUE]]...

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

	std::string ReadFile(const std::filesystem::path& path, bool& ok)
	{
		std::ifstream file(path, std::ios::binary);
		ok = static_cast<bool>(file);
		std::ostringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}

	class FileIncluder final : public glslang::TShader::Includer
	{
	public:
		explicit FileIncluder(std::vector<std::filesystem::path> dirs) : m_Dirs(std::move(dirs)) {}

		IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t) override
		{
			std::filesystem::path includer = std::filesystem::path(includerName).parent_path();
			return Find(headerName, includer);
		}

		IncludeResult* includeSystem(const char* headerName, const char*, size_t) override { return Find(headerName, {}); }

		void releaseInclude(IncludeResult* result) override
		{
			if(!result)
				return;
			delete static_cast<std::string*>(result->userData);
			delete result;
		}

	private:
		IncludeResult* Find(const char* headerName, const std::filesystem::path& relativeTo)
		{
			std::vector<std::filesystem::path> candidates;
			if(!relativeTo.empty())
				candidates.push_back(relativeTo / headerName);
			for(const auto& dir : m_Dirs)
				candidates.push_back(dir / headerName);
			for(const auto& candidate : candidates)
			{
				bool ok = false;
				auto* content = new std::string(ReadFile(candidate, ok));
				if(ok)
					return new IncludeResult(candidate.generic_string(), content->data(), content->size(), content);
				delete content;
			}
			return nullptr;
		}

		std::vector<std::filesystem::path> m_Dirs;
	};

	bool StageFromExtension(const std::filesystem::path& path, EShLanguage& stage)
	{
		std::string ext = path.extension().string();
		if(ext == ".vert") stage = EShLangVertex;
		else if(ext == ".frag") stage = EShLangFragment;
		else if(ext == ".comp") stage = EShLangCompute;
		else return false;
		return true;
	}

}

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		std::fprintf(stderr, "usage: ShaderCompiler <input> <output.spv> [-I dir]... [-D NAME[=VALUE]]...\n");
		return 2;
	}

	std::filesystem::path input = argv[1];
	std::filesystem::path output = argv[2];
	std::vector<std::filesystem::path> includeDirs;
	std::string defines;
	for(int i = 3; i < argc; i++)
	{
		std::string arg = argv[i];
		if(arg == "-I" && i + 1 < argc)
			includeDirs.emplace_back(argv[++i]);
		else if(arg == "-D" && i + 1 < argc)
		{
			std::string define = argv[++i];
			size_t eq = define.find('=');
			defines += "#define " + (eq == std::string::npos ? define : define.substr(0, eq) + " " + define.substr(eq + 1)) + "\n";
		}
	}

	EShLanguage stage;
	if(!StageFromExtension(input, stage))
	{
		std::fprintf(stderr, "%s: unknown shader stage (expected .vert/.frag/.comp)\n", input.string().c_str());
		return 2;
	}

	bool ok = false;
	std::string source = ReadFile(input, ok);
	if(!ok)
	{
		std::fprintf(stderr, "%s: cannot read file\n", input.string().c_str());
		return 2;
	}

	glslang::InitializeProcess();
	int exitCode = 0;
	{
		glslang::TShader shader(stage);
		const std::string inputName = input.generic_string();
		const char* sources[] = { source.c_str() };
		const char* names[] = { inputName.c_str() };
		shader.setStringsWithLengthsAndNames(sources, nullptr, names, 1);
		shader.setPreamble(defines.c_str());
		shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
		shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
		shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

		FileIncluder includer(includeDirs);
		const TBuiltInResource* resources = GetDefaultResources();
		auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
		if(!shader.parse(resources, 450, false, messages, includer))
		{
			std::fprintf(stderr, "%s\n%s\n", shader.getInfoLog(), shader.getInfoDebugLog());
			exitCode = 1;
		}
		else
		{
			glslang::TProgram program;
			program.addShader(&shader);
			if(!program.link(messages))
			{
				std::fprintf(stderr, "%s\n%s\n", program.getInfoLog(), program.getInfoDebugLog());
				exitCode = 1;
			}
			else
			{
				std::vector<unsigned int> spirv;
				glslang::SpvOptions options;
				options.validate = true;
#ifdef NDEBUG
				options.stripDebugInfo = true;
#else
				options.generateDebugInfo = false;
#endif
				glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &options);

				std::error_code ec;
				std::filesystem::create_directories(output.parent_path(), ec);
				std::ofstream out(output, std::ios::binary | std::ios::trunc);
				out.write(reinterpret_cast<const char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(unsigned int)));
				if(!out)
				{
					std::fprintf(stderr, "%s: cannot write output\n", output.string().c_str());
					exitCode = 1;
				}
			}
		}
	}
	glslang::FinalizeProcess();
	return exitCode;
}
