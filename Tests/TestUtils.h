#pragma once

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/Project.h"

#include <atomic>
#include <filesystem>
#include <string>

// Creates a throw-away project asset directory for a test and points Project at it.
class TestProject
{
public:
	TestProject()
	{
		static std::atomic<int> counter{ 0 };
		m_Root = std::filesystem::temp_directory_path() / ("StarfallTest_" + std::to_string(::std::filesystem::file_time_type::clock::now().time_since_epoch().count()) + "_" + std::to_string(counter++));
		std::filesystem::create_directories(m_Root / "Assets");
		Starfall::Project::SetAssetDirectory(m_Root / "Assets");
	}

	~TestProject()
	{
		Starfall::Project::Unload();
		std::error_code ec;
		std::filesystem::remove_all(m_Root, ec);
	}

	void Write(const std::string& assetPath, const std::string& content) const
	{
		Starfall::FileSystem::WriteText(m_Root / "Assets" / assetPath, content);
	}

	void WriteBinary(const std::string& assetPath, const std::vector<uint8_t>& data) const
	{
		Starfall::FileSystem::WriteBinary(m_Root / "Assets" / assetPath, data.data(), data.size());
	}

	std::filesystem::path Root() const { return m_Root; }
	std::filesystem::path Assets() const { return m_Root / "Assets"; }

private:
	std::filesystem::path m_Root;
};
