#pragma once

// The start sequence itself.
//
// Alternate Perspective owns the menu and the choosing; this mod owns exactly one moment - the one where AP starts the
// quest named in the option. From there it puts the player inside the home and hands it over as the home's own way of getting it would.

#include <string>

namespace start
{
	// Resolve the records and start listening. Called once at kDataLoaded. False when the start cannot be offered: the home is
	// not loaded, or our own plugin is not.
	bool Install();

	// Run the sequence now. Called by the quest-start listener after the configured delay, and by the DevBench tool so the
	// whole thing can be exercised without going through the menu.
	bool Run(const char* a_reason);

	// What the last run did, for the log and the DevBench tool.
	struct Report
	{
		bool ran = false;
		std::string reason;
		bool moved = false;
		bool keyGiven = false;
		bool counted = false;  // "Houses Owned" was dispatched
		std::string problem;   // empty when everything asked for actually happened
	};

	// Co-save hooks, registered by main.cpp. One byte: whether this mod handed the house over in this game, which decides
	// whether the For Sale sign goes when it loads.
	void OnSave(SKSE::SerializationInterface* a_intfc);
	void OnLoad(SKSE::SerializationInterface* a_intfc);
	void OnRevert(SKSE::SerializationInterface* a_intfc);

	std::string StateJson();
}
