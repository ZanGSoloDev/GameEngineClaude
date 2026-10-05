#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Starfall/Core/Base.h"

#include <cstdlib>

#include "GpuTestUtils.h"

int main(int argc, char** argv)
{
	Starfall::Log::Init();
	Starfall::Log::SetLevel(std::getenv("SF_TEST_LOG") ? Starfall::LogLevel::Trace : Starfall::LogLevel::Warn);
	doctest::Context context(argc, argv);
	int result = context.run();
	ShutdownGpuTestContext();
	Starfall::Log::Shutdown();
	return result;
}
