#pragma once

// AP Home Start - Winter Cove. Starts the game inside Winter Cove from Alternate Perspective's "My own Home" card.
//
// How the home is obtained, read from its records on 2026-11-03:
// Nothing is sold: the key waits in the firewood storage (chest 0200E31C, reached from the firewood piles inside and left of the front door). Scripted lock: invisible collision at both sides of the front door (02018D71, 02034078, enable parent 02018D70 inverted) stands until the Door Lock activator is used with the key in the pack (WiCoDoorLockScript), so the key is what lets a player out. No 'Houses Owned' count anywhere in its scripts.
//
// Everything below is written by .MD\scripts\new-home-start.py from .MD\scripts\home-starts.json. No record of the
// home is edited.

#include <cstdint>

namespace home
{
	inline constexpr const char* kLogName = "APHomeStartWinterCove";
	inline constexpr const char* kDisplayName = "AP Home Start - Winter Cove";
	inline constexpr const char* kIniName = "APHomeStartWinterCove.ini";
	inline constexpr const char* kPluginName = "AP Home Start - Winter Cove.esp";
	inline constexpr const char* kTranslationStem = "APHomeStartWinterCove";

	// Our own quest, the one Alternate Perspective starts. AP is handed the same number without the index, because it
	// calls Game.GetFormFromFile.
	inline constexpr std::uint32_t kStartQuestLocalID = 0x800;

	// ---- Alternate Perspective's menu (see ApMenu.cpp) ----
	inline constexpr const char* kApMenuName = "CustomMenu";
	inline constexpr const char* kApOptionsPath = "_root.main._options";
	inline constexpr const char* kApListPath = "_root.main.menu.mainOptions";
	inline constexpr const char* kApHomeCardText = "$AltPersp_Home";

	// ---- Winter Cove, in Winter Cove.esp: every id read from its records. LOCAL: its place in the load order is resolved at runtime ----
	inline constexpr const char* kHomeName = "Winter Cove";
	inline constexpr const char* kHomePlugin = "Winter Cove.esp";
	inline constexpr std::uint32_t kHomeNexusId = 4171;
	inline constexpr const char* kCellEditorID = "1xmWinterCoveInt";  // only for the log
	inline constexpr const char* kCardDescr = "Winter Cove is already yours - the key is in your pack.";

	// Persistent, so it is in memory before the cell loads; it says which cell the home is.
	inline constexpr std::uint32_t kInsideDoor = 0x005447;
	// 0: the home has none of it. The sign is the one the sale disables; it goes when it loads, in a game this mod started.
	inline constexpr std::uint32_t kKey = 0x0182A6;
	inline constexpr std::uint32_t kDeed = 0x000000;
	inline constexpr std::uint32_t kSaleSign = 0x000000;
	// Whether the home's own sale raises "Houses Owned". The start does what the home does.
	inline constexpr bool kCountHouse = false;
	// True: the cell has a COC marker (0x0022ED), the game's own arrival point. False: the player goes to the inside door and
	// then stands at the point below, where the outside door's teleport puts a visitor.
	inline constexpr bool kHasCocMarker = true;
	inline constexpr float kArrivalX = 0.0f;
	inline constexpr float kArrivalY = 0.0f;
	inline constexpr float kArrivalZ = 0.0f;
	inline constexpr float kArrivalHeading = 0.000f;
}
