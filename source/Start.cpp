#include "PCH.h"

#include "Start.h"

#include "Home.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <thread>

namespace start
{
	namespace
	{
		std::mutex g_lock;
		Report g_report;
		std::atomic<bool> g_installed{ false };
		std::atomic<bool> g_chosen{ false };
		std::atomic<bool> g_queued{ false };
		// Persisted in the co-save: the house was handed over by THIS mod in this game.
		std::atomic<bool> g_ranInThisGame{ false };
		// Where the move was aimed. Nothing is handed over until the player is actually THERE.
		RE::TESObjectCELL* g_destination = nullptr;
		// Filled in at kDataLoaded.
		RE::FormID g_questID = 0;
		RE::FormID g_signID = 0;

		constexpr std::uint32_t kRecordRan = 'RAN_';
		constexpr std::uint32_t kRecordVersion = 1;

		std::string JsonText(const std::string& a_text)
		{
			std::string out;
			for (char c : a_text)
			{
				if (c == '"' || c == '\\') { out += '\\'; out += c; }
				else if (c == '\n') { out += "\\n"; }
				else { out += c; }
			}
			return out;
		}

		RE::TESQuest* OurQuest()
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			return handler ? handler->LookupForm<RE::TESQuest>(home::kStartQuestLocalID, home::kPluginName) : nullptr;
		}

		// The home's records, looked up by the plugin that owns them, because the index depends on the player's load order.
		template <class T>
		T* FromHome(std::uint32_t a_local)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			return handler ? handler->LookupForm<T>(a_local, home::kHomePlugin) : nullptr;
		}

		// The Game.IncrementStat("Houses Owned") of a home whose sale counts. A native with no C++ handle, so through the VM.
		bool CountTheHouse()
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) { return false; }
			auto args = RE::MakeFunctionArguments(RE::BSFixedString("Houses Owned"), std::int32_t{ 1 });
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			const bool ok = vm->DispatchStaticCall(RE::BSFixedString("Game"), RE::BSFixedString("IncrementStat"), args, callback);
			logger::info("stat: Houses Owned +1 {}", ok ? "dispatched" : "NOT dispatched (the script call was refused)");
			return ok;
		}

		// Into the home as the console's coc does it: the game's own move to a cell, which lands on the cell's COC marker, close to
		// where the front door puts a visitor. That marker is not persistent, so it is not in memory to MoveTo; the inside door
		// is, and it is what says which cell this is, so a mod that renames the cell changes nothing here. A home with no COC
		// marker goes to the inside door, and StandAtArrival finishes the move once the player is in the cell.
		bool MovePlayer(RE::PlayerCharacter* a_player, std::string& a_problem)
		{
			auto* inside = FromHome<RE::TESObjectREFR>(home::kInsideDoor);
			auto* cell = inside ? inside->GetParentCell() : nullptr;
			if (!cell)
			{
				a_problem = std::format("the home's inside door {:06X} or its cell is not in the game", home::kInsideDoor);
				logger::error("move: {}", a_problem);
				return false;
			}
			g_destination = cell;
			if (home::kHasCocMarker && a_player->CenterOnCell(cell))
			{
				logger::info("move: the player was sent to the home's cell {:08X}", cell->GetFormID());
				return true;
			}
			if (home::kHasCocMarker)
			{
				logger::warn("move: the game refused to centre on cell {:08X}; going to the inside door itself", cell->GetFormID());
			}
			a_player->MoveTo(inside);
			logger::debug("move: sent to the inside door {:08X}", inside->GetFormID());
			return true;
		}

		void StandAtArrival(RE::PlayerCharacter* a_player)
		{
			if (home::kHasCocMarker) { return; }
			a_player->SetPosition(RE::NiPoint3{ home::kArrivalX, home::kArrivalY, home::kArrivalZ });
			a_player->SetHeading(home::kArrivalHeading);
			logger::info("move: stood at the door's arrival point ({:.1f}, {:.1f}, {:.1f})", home::kArrivalX, home::kArrivalY, home::kArrivalZ);
		}

		// One of an item the home's sale gives, unless the player already holds one. True when the record exists.
		template <class T>
		bool GiveOnce(RE::PlayerCharacter* a_player, std::uint32_t a_local, const char* a_what, std::string& a_problem)
		{
			auto* item = FromHome<T>(a_local);
			if (!item)
			{
				a_problem = std::format("the {} {:06X} is not in {}", a_what, a_local, home::kHomePlugin);
				logger::error("deed: {}", a_problem);
				return false;
			}
			if (a_player->GetItemCount(item) == 0)
			{
				a_player->AddObjectToContainer(item, nullptr, 1, nullptr);
				logger::info("deed: gave the {} ({:08X})", a_what, item->GetFormID());
			}
			return true;
		}

		// What the home's own way of getting it hands over, minus any gold. A sale sign goes when it loads.
		void HandOver(RE::PlayerCharacter* a_player, Report& a_report)
		{
			if (home::kKey != 0) { a_report.keyGiven = GiveOnce<RE::TESKey>(a_player, home::kKey, "key", a_report.problem); }
			if (home::kDeed != 0) { GiveOnce<RE::TESObjectBOOK>(a_player, home::kDeed, "deed", a_report.problem); }
			if (home::kCountHouse) { a_report.counted = CountTheHouse(); }
		}

		void HideSignIfLoaded(RE::FormID a_id)
		{
			auto* sign = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
			if (!sign)
			{
				logger::debug("sign: {:08X} is not in memory yet", a_id);
				return;
			}
			if (!sign->IsDisabled())
			{
				sign->Disable();
				logger::info("sign: the For Sale sign ({:08X}) is gone, the house is already yours", a_id);
			}
		}

		void RunNow(std::string a_reason)
		{
			g_queued.store(false);
			Run(a_reason.c_str());
		}

		// The wait is a real wait, not a frame count: AP is mid-hand-off when it starts our quest. The sleep happens on its
		// own thread and the work itself is handed back to the task queue, because moving the player from anywhere else is
		// not safe.
		void QueueRun(const char* a_reason)
		{
			if (g_queued.exchange(true)) { return; }
			const std::string reason = a_reason;
			const float delay = settings::general::startDelaySeconds;
			std::thread([reason, delay]() {
				if (delay > 0.0f)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000.0f)));
				}
				if (auto* task = SKSE::GetTaskInterface())
				{
					task->AddTask([reason]() { RunNow(reason); });
				}
				else
				{
					RunNow(reason);
				}
			}).detach();
		}

		// THERE IS NO WATCHDOG, AND THAT IS DELIBERATE - see Civil War Starts, where one fired at selection time and
		// teleported the player before they walked through the door (2026-09-16). The quest-start event is the only trigger.

		void QueueArrivalLook(std::string a_reason);

		// How many times to look for the player's arrival before giving up and carrying on anyway.
		inline constexpr int kArrivalLooks = 40;
		int g_arrivalLooks = 0;

		// Checked, not timed: a teleport takes as long as it takes. Then the key and the count, then our quest stops, since it
		// has nothing left to do and a quest left running shows up in save inspections forever.
		void LookForArrival(std::string a_reason)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* here = player ? player->GetParentCell() : nullptr;
			const bool arrived = !g_destination || (here && here == g_destination);
			if (!arrived && ++g_arrivalLooks < kArrivalLooks)
			{
				QueueArrivalLook(a_reason);
				return;
			}
			logger::info("move: the player is now in cell {} (expected: {}){}", here ? here->GetFormEditorID() : "<unknown>",
						 home::kCellEditorID, arrived ? "" : " - gave up waiting and carried on");
			std::scoped_lock l(g_lock);
			if (player)
			{
				if (arrived) { StandAtArrival(player); }
				HandOver(player, g_report);
			}
			if (auto* ours = OurQuest(); ours && ours->IsEnabled())
			{
				ours->Stop();
				logger::debug("start: our own quest stopped");
			}
			logger::info("start ({}): finished - moved={} key={} counted={}{}", a_reason, g_report.moved, g_report.keyGiven, g_report.counted,
						 g_report.problem.empty() ? "" : std::format(" (problem: {})", g_report.problem));
		}

		void QueueArrivalLook(std::string a_reason)
		{
			const float gap = std::clamp(settings::general::stageGapSeconds, 0.0f, 5.0f);
			std::thread([a_reason, gap]() {
				if (gap > 0.0f)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(gap * 1000.0f)));
				}
				if (auto* tasks = SKSE::GetTaskInterface())
				{
					tasks->AddTask([a_reason]() { LookForArrival(a_reason); });
				}
			}).detach();
		}

		class Sinks final :
			public RE::BSTEventSink<RE::TESQuestStartStopEvent>,
			public RE::BSTEventSink<RE::TESObjectLoadedEvent>
		{
		public:
			static Sinks* GetSingleton()
			{
				static Sinks singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStartStopEvent* a_event,
												  RE::BSTEventSource<RE::TESQuestStartStopEvent>*) override
			{
				if (a_event && a_event->started && g_questID != 0 && a_event->formID == g_questID)
				{
					g_chosen.store(true);
					logger::info("chosen: Alternate Perspective started our quest {:08X}; it runs in {:.2f}s",
								 a_event->formID, settings::general::startDelaySeconds);
					QueueRun("alternate perspective");
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			// The sign comes into memory with its cell, which a player who chose this start usually first sees on walking
			// out of the door. Only for a game this mod handed the house over in; anyone else still buys it.
			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
												  RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (a_event && a_event->loaded && g_signID != 0 && a_event->formID == g_signID && g_ranInThisGame.load())
				{
					if (auto* tasks = SKSE::GetTaskInterface())
					{
						tasks->AddTask([id = g_signID]() { HideSignIfLoaded(id); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	bool Install()
	{
		if (g_installed.exchange(true)) { return g_questID != 0; }

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler || !handler->LookupLoadedModByName(home::kHomePlugin))
		{
			// Said plainly: the option would start a game in a house that is not there.
			logger::error("install: {} is not loaded, so the start is not offered. This add-on needs {} (Nexus {}).", home::kHomePlugin,
						  home::kHomeName, home::kHomeNexusId);
			return false;
		}
		auto* quest = OurQuest();
		if (!quest)
		{
			logger::error("install: {} has no quest {:03X}. The plugin ships with the mod - check that it is installed and enabled.",
						  home::kPluginName, home::kStartQuestLocalID);
			return false;
		}
		g_questID = quest->GetFormID();
		g_signID = home::kSaleSign != 0 ? handler->LookupFormID(home::kSaleSign, home::kHomePlugin) : 0;
		logger::info("install: the start is quest {:08X} ({}); the sale sign is {:08X}", g_questID, quest->GetFormEditorID(), g_signID);

		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder)
		{
			logger::error("install: the event source holder is not available; a start cannot be noticed");
			g_questID = 0;
			return false;
		}
		holder->AddEventSink<RE::TESQuestStartStopEvent>(Sinks::GetSingleton());
		if (g_signID != 0) { holder->AddEventSink<RE::TESObjectLoadedEvent>(Sinks::GetSingleton()); }
		return true;
	}

	bool Run(const char* a_reason)
	{
		std::scoped_lock l(g_lock);

		Report report;
		report.reason = a_reason ? a_reason : "";

		if (!settings::general::enabled)
		{
			report.problem = "the mod is switched off in its settings (bEnabled=0)";
			logger::warn("start ({}): {}", report.reason, report.problem);
			g_report = report;
			return false;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player)
		{
			report.problem = "there is no player to move";
			logger::error("start ({}): {}", report.reason, report.problem);
			g_report = report;
			return false;
		}

		logger::info("start ({}): beginning", report.reason);
		g_ranInThisGame.store(true);
		g_destination = nullptr;
		report.moved = MovePlayer(player, report.problem);
		report.ran = true;
		g_report = report;
		g_arrivalLooks = 0;
		QueueArrivalLook(report.reason);
		return true;
	}

	void OnSave(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) { return; }
		if (a_intfc->OpenRecord(kRecordRan, kRecordVersion))
		{
			const std::uint8_t v = g_ranInThisGame.load() ? 1 : 0;
			a_intfc->WriteRecordData(&v, sizeof(v));
		}
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		g_ranInThisGame.store(false);
		if (!a_intfc) { return; }
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length))
		{
			if (type == kRecordRan && length >= sizeof(std::uint8_t))
			{
				std::uint8_t v = 0;
				if (a_intfc->ReadRecordData(&v, sizeof(v)) == sizeof(v)) { g_ranInThisGame.store(v != 0); }
			}
		}
		logger::debug("co-save: ran-in-this-game={}", g_ranInThisGame.load());
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		g_ranInThisGame.store(false);
		std::scoped_lock l(g_lock);
		g_report = Report{};
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);

		auto* ours = OurQuest();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* key = home::kKey != 0 ? FromHome<RE::TESKey>(home::kKey) : nullptr;
		auto* inside = FromHome<RE::TESObjectREFR>(home::kInsideDoor);
		auto* cell = inside ? inside->GetParentCell() : nullptr;
		auto* owner = cell ? cell->GetOwner() : nullptr;
		const auto* here = player ? player->GetParentCell() : nullptr;
		const auto* sign = g_signID ? RE::TESForm::LookupByID<RE::TESObjectREFR>(g_signID) : nullptr;

		return std::format(
			"\"chosen\":{},\"ranInThisGame\":{},"
			"\"ourQuest\":{{\"formID\":\"{:08X}\",\"enabled\":{},\"stopped\":{}}},"
			"\"house\":{{\"cell\":\"{:08X}\",\"playerInIt\":{},\"owner\":\"{:08X}\",\"keys\":{},\"signInMemory\":{},\"signDisabled\":{}}},"
			"\"lastRun\":{{\"ran\":{},\"reason\":\"{}\",\"moved\":{},\"keyGiven\":{},\"counted\":{},\"problem\":\"{}\"}}",
			g_chosen.load() ? "true" : "false", g_ranInThisGame.load() ? "true" : "false",
			g_questID, ours && ours->IsEnabled() ? "true" : "false", ours && ours->IsStopped() ? "true" : "false",
			cell ? cell->GetFormID() : 0, cell && here == cell ? "true" : "false", owner ? owner->GetFormID() : 0,
			player && key ? player->GetItemCount(key) : -1, sign ? "true" : "false", sign && sign->IsDisabled() ? "true" : "false",
			g_report.ran ? "true" : "false", JsonText(g_report.reason), g_report.moved ? "true" : "false",
			g_report.keyGiven ? "true" : "false", g_report.counted ? "true" : "false", JsonText(g_report.problem));
	}
}
