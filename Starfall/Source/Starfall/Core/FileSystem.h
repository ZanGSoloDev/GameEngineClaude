#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Starfall {

	class FileSystem
	{
	public:
		static std::optional<std::string> ReadText(const std::filesystem::path& path);
		static std::optional<std::vector<uint8_t>> ReadBinary(const std::filesystem::path& path);
		// Writes atomically (temp file + rename) so a crash cannot leave a half-written asset.
		static bool WriteText(const std::filesystem::path& path, const std::string& content);
		static bool WriteBinary(const std::filesystem::path& path, const void* data, size_t size);

		// Directory containing the running executable.
		static std::filesystem::path GetExecutableDirectory();
		// Engine resources (shaders, icons, default assets) live next to the executable.
		static std::filesystem::path GetResourcesDirectory();
		static void SetResourcesDirectory(const std::filesystem::path& path);
	};

}
