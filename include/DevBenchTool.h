#pragma once

namespace DevBenchTool
{
	// Registers "aphomestartwintercove.start" with DevBench when it is there. Called with false at kPostLoad and true at kDataLoaded.
	void Init(bool a_lastAttempt);
}
