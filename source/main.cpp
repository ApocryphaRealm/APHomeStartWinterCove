// AP Home Start - Winter Cove. Own code, GPL-3.0-or-later (2026-11-03).
//
// An add-on for Alternate Perspective (Nexus 50307) and Winter Cove (Nexus 4171), one of the AP Home Start series. It adds the home
// to AP's own "My own home" card:
//   * AP Home Start - Winter Cove.esp - one quest record, so AP has something to name and start.
//   * This DLL - it adds the option to AP's menu when the menu opens (ApMenu.cpp), then watches for that quest starting, puts
//     the player inside and hands the home over (Start.cpp, with the home's data in Home.h).
#include "PCH.h"

#include "ApMenu.h"
#include "DevBenchTool.h"
#include "Home.h"
#include "Settings.h"
#include "Start.h"

#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			DevBenchTool::Init(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			strings::Configure(home::kTranslationStem);
			// With no home there is no option: it would start a game in a house that is not there.
			if (start::Install()) { apmenu::Install(); }
			DevBenchTool::Init(true);
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::log::init(home::kLogName);
	if (!AddressLibraryGuard::Guard(home::kDisplayName))
	{
		return true;
	}
	SKSE::Init(a_skse);

	settings::Init(home::kIniName);
	settings::ApplyLogLevel();

	logger::info("{} {} loading", home::kDisplayName, SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);

	if (auto* serialization = SKSE::GetSerializationInterface())
	{
		serialization->SetUniqueID('AHWC');
		serialization->SetSaveCallback(start::OnSave);
		serialization->SetLoadCallback(start::OnLoad);
		serialization->SetRevertCallback(start::OnRevert);
	}
	return true;
}
