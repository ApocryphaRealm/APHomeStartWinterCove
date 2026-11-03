#pragma once

// Settings for AP Home Start - Winter Cove: a plain INI beside the DLL. There is no in-game page (the owner,
// 2026-10-02: Alternate Perspective add-ons are simple mods with no AMF page). Every compiled default below is the value
// the shipped INI carries (rule 16).

#include <cstdint>

namespace settings
{
	namespace debug
	{
		inline std::uint32_t logLevel = 2;  // uLogLevel:Debug - 2 is info, the shipped default
	}

	namespace general
	{
		inline bool enabled = true;              // bEnabled:General
		inline float startDelaySeconds = 0.25f;  // fStartDelaySeconds:General
		inline float stageGapSeconds = 0.15f;    // fStageGapSeconds:General
	}

	void Init(const char* a_iniFileName);
	void ApplyLogLevel();
}
