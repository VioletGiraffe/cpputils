#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "system/cpuinfo.hpp"

TEST_CASE("cpuModelName", "[system]")
{
	const std::string name = cpuModelName();
	INFO(name);
	CHECK(!name.empty());
}
