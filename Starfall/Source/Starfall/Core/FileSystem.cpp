#include "Starfall/Core/FileSystem.h"

#include "Starfall/Core/Base.h"

#include <fstream>
#include <sstream>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#elif defined(__APPLE__)
	#include <mach-o/dyld.h>
#else
	#include <unistd.h>
#endif

namespace Starfall {

	namespace {
		std::filesystem::path s_ResourcesDirectory;
	}

	std::optional<std::string> FileSystem::ReadText(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if(!file)
			return std::nullopt;
		std::ostringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}

	std::optional<std::vector<uint8_t>> FileSystem::ReadBinary(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
		if(!file)
			return std::nullopt;
		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> data(static_cast<size_t>(size));
		if(size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size))
			return std::nullopt;
		return data;
	}

	bool FileSystem::WriteBinary(const std::filesystem::path& path, const void* data, size_t size)
	{
		std::error_code ec;
		if(path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), ec);

		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::out | std::ios::binary | std::ios::trunc);
			if(!file)
				return false;
			file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			if(!file)
				return false;
		}
		std::filesystem::rename(temp, path, ec);
		if(ec)
		{
			// rename over an existing file can fail on some platforms; fall back to remove + rename
			std::filesystem::remove(path, ec);
			ec.clear();
			std::filesystem::rename(temp, path, ec);
		}
		return !ec;
	}

	bool FileSystem::WriteText(const std::filesystem::path& path, const std::string& content)
	{
		return WriteBinary(path, content.data(), content.size());
	}

	std::filesystem::path FileSystem::GetExecutableDirectory()
	{
#if defined(_WIN32)
		wchar_t buffer[MAX_PATH * 4];
		DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
		return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#elif defined(__APPLE__)
		char buffer[4096];
		uint32_t size = sizeof(buffer);
		if(_NSGetExecutablePath(buffer, &size) != 0)
			return std::filesystem::current_path();
		return std::filesystem::weakly_canonical(buffer).parent_path();
#else
		char buffer[4096];
		ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if(length <= 0)
			return std::filesystem::current_path();
		return std::filesystem::path(std::string(buffer, static_cast<size_t>(length))).parent_path();
#endif
	}

	std::filesystem::path FileSystem::GetResourcesDirectory()
	{
		if(!s_ResourcesDirectory.empty())
			return s_ResourcesDirectory;
		return GetExecutableDirectory() / "Resources";
	}

	void FileSystem::SetResourcesDirectory(const std::filesystem::path& path)
	{
		s_ResourcesDirectory = path;
	}

}
