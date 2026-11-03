#include "PCH.h"

#include "DevBenchTool.h"

#include "ApMenu.h"
#include "DevBench/DevBenchAPI.h"
#include "Home.h"
#include "Start.h"
#include "utils/Logger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace DevBenchTool
{
	namespace
	{
		// The handler runs on DevBench's own thread; moving the player and reading references are task-queue work. Results
		// are owned by shared_ptr so a timed-out task never writes into a dead frame (the Civil War Starts lesson, 2026-09-17).
		bool RunOnMainThread(std::function<void()> a_fn, int a_timeoutMs = 8000)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) { return false; }
			auto done = std::make_shared<std::atomic<bool>>(false);
			auto m = std::make_shared<std::mutex>();
			auto cv = std::make_shared<std::condition_variable>();
			auto fn = std::make_shared<std::function<void()>>(std::move(a_fn));
			tasks->AddTask([=]() {
				(*fn)();
				{
					std::scoped_lock l(*m);
					done->store(true);
				}
				cv->notify_all();
			});
			std::unique_lock l(*m);
			return cv->wait_for(l, std::chrono::milliseconds(a_timeoutMs), [&]() { return done->load(); });
		}

		std::string StateJson()
		{
			return std::format("{{\"ok\":true,\"op\":\"state\",\"mod\":\"{}\",\"plugin\":\"{}\",{},{}}}", home::kDisplayName,
							   home::kPluginName, start::StateJson(), apmenu::StateJson());
		}

		void StartTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";

			if (args.find("\"run\"") != std::string_view::npos)
			{
				auto ok = std::make_shared<bool>(false);
				if (!RunOnMainThread([ok]() { *ok = start::Run("devbench"); }))
				{
					a_write(a_sink, R"({"ok":false,"op":"run","error":"the task did not run in time"})");
					return;
				}
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"run\"}}", *ok ? "true" : "false").c_str());
				return;
			}
			if (args.find("\"menu\"") != std::string_view::npos)
			{
				// AP opens its menu when its Messenger dialogue sends this mod event (APDialoguePlayer listens for it), so
				// sending it here opens the same menu the player would, and our sink adds the option as it would in play.
				auto sent = std::make_shared<bool>(false);
				if (!RunOnMainThread([sent]() {
						if (auto* source = SKSE::GetModCallbackEventSource())
						{
							SKSE::ModCallbackEvent e{ "AP_MessengerMenuOpen", "", 0.0f, nullptr };
							source->SendEvent(&e);
							*sent = true;
						}
					}))
				{
					a_write(a_sink, R"({"ok":false,"op":"menu","error":"the task did not run in time"})");
					return;
				}
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"menu\",\"note\":\"AP_MessengerMenuOpen sent; read op=state a second later\"}}",
											*sent ? "true" : "false").c_str());
				return;
			}

			auto text = std::make_shared<std::string>();
			if (!RunOnMainThread([text]() { *text = StateJson(); }))
			{
				a_write(a_sink, R"({"ok":false,"op":"state","error":"the task did not run in time"})");
				return;
			}
			a_write(a_sink, text->c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }

		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"aphomestartwintercove.start\" tool"); }
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"AP Home Start - Winter Cove. op=state (default): whether the start was chosen or has run in this "
			"game, whether the player is in the home's cell, the keys held, the sale sign, and what the menu pass did. op=run: run the start now "
			"without going through Alternate Perspective's menu. op=menu opens Alternate Perspective's own menu (the mod event its "
			"Messenger sends), which is how the option added to its My own home card can be seen.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (devBench->RegisterTool("aphomestartwintercove.start", descriptor, &StartTool, nullptr))
		{
			logger::info("Registered \"aphomestartwintercove.start\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
