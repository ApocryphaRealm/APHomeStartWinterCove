#include "PCH.h"

#include "Settings.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <Windows.h>

namespace settings
{
	void Init(const char* a_iniFileName)
	{
		const auto path = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName;
		if (!std::filesystem::exists(path))
		{
			logger::warn("INI not found at {}; keeping compiled defaults", path.string());
			return;
		}
		const auto ini = path.wstring();
		const auto readFloat = [&](const wchar_t* a_section, const wchar_t* a_key, float a_default) {
			wchar_t text[32]{};
			GetPrivateProfileStringW(a_section, a_key, L"", text, 32, ini.c_str());
			return text[0] ? static_cast<float>(std::wcstod(text, nullptr)) : a_default;
		};
		debug::logLevel = GetPrivateProfileIntW(L"Debug", L"uLogLevel", static_cast<int>(debug::logLevel), ini.c_str());
		general::enabled = GetPrivateProfileIntW(L"General", L"bEnabled", general::enabled ? 1 : 0, ini.c_str()) != 0;
		general::startDelaySeconds = std::clamp(readFloat(L"General", L"fStartDelaySeconds", general::startDelaySeconds), 0.0f, 30.0f);
		general::stageGapSeconds = std::clamp(readFloat(L"General", L"fStageGapSeconds", general::stageGapSeconds), 0.0f, 5.0f);
		logger::info("settings: enabled={} delay={:.2f}s gap={:.2f}s logLevel={}", general::enabled, general::startDelaySeconds,
					 general::stageGapSeconds, debug::logLevel);
	}

	void ApplyLogLevel()
	{
		const auto lvl = static_cast<spdlog::level::level_enum>(std::clamp<std::uint32_t>(debug::logLevel, 0u, 6u));
		SKSE::log::set_level(lvl, lvl);
	}
}
