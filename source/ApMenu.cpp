#include "PCH.h"

#include "ApMenu.h"

#include "Home.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace apmenu
{
	namespace
	{
		std::mutex g_lock;
		Report g_report;
		std::atomic<bool> g_installed{ false };
		std::atomic<bool> g_running{ false };

		// How many frames to wait for AP to finish reading its files. AP 4.1's MessengerMenu constructor fills
		// `_options` with its own None and Random cards straight away, then reads the JSON files with LoadVars,
		// one at a time, and only when the last is read hands the whole array to the list (OptionList.setItems
		// pushes every entry into mainOptions.entryList). So "the option array is not empty" is true from the
		// first frame and says nothing (the 2026-11-09 Nexus report: "2 card(s), 1 pass(es)", the "My own Home"
		// card not yet read, our fallback card added too early). The signal that AP is done is the list's own
		// entryList being filled. Ten seconds at 60 fps covers a long add-on list on a cold cache.
		inline constexpr int kMaxPasses = 600;

		RE::GPtr<RE::GFxMovieView> Movie()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { return {}; }
			auto menu = ui->GetMenu(home::kApMenuName);
			return menu ? menu->uiMovie : RE::GPtr<RE::GFxMovieView>{};
		}

		std::string MemberString(const RE::GFxValue& a_obj, const char* a_name)
		{
			RE::GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || !v.IsString()) { return {}; }
			const char* s = v.GetString();
			return s ? s : std::string{};
		}

		// The sub-choice is the house's own name, not the city as AP's are: Windhelm in this card is our Hjerim add-on's
		// (the owner, 2026-10-13), and both can be installed.
		const char* SubChoiceText() { return strings::TR("AHS_Card", home::kHomeName); }
		const char* OwnCardText() { return strings::TR("AHS_OwnCard", home::kHomeName); }

		// One suboption, shaped exactly like AP's own: the text the player reads, the quest's FormID
		// WITHOUT the plugin index (AP calls Game.GetFormFromFile with it), and the plugin that owns it -
		// a suboption without `mod` inherits the card's, which here would be AP's own ESP.
		bool MakeOurOption(RE::GFxMovieView* a_movie, RE::GFxValue& a_out, const char* a_text)
		{
			a_movie->CreateObject(&a_out);
			if (!a_out.IsObject()) { return false; }
			RE::GFxValue text, mod, id, enabled;
			a_movie->CreateString(&text, a_text);
			a_movie->CreateString(&mod, home::kPluginName);
			id.SetNumber(static_cast<double>(home::kStartQuestLocalID));
			// AP's validateConditions sets enabled = true on every entry it reads; MainListEntry draws an entry
			// without it at 25% alpha and the list refuses the click ("clickDisabled").
			enabled.SetBoolean(true);
			return a_out.SetMember("text", text) && a_out.SetMember("id", id) && a_out.SetMember("mod", mod) &&
			       a_out.SetMember("enabled", enabled);
		}

		// Already there? The menu is rebuilt on every open, but a second pass of ours (or a future AP that
		// keeps the array) must not add the option twice.
		bool AlreadyInSuboptions(const RE::GFxValue& a_subs)
		{
			const std::uint32_t n = a_subs.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i)
			{
				RE::GFxValue e;
				if (a_subs.GetElement(i, &e) && e.IsObject() && MemberString(e, "mod") == home::kPluginName) { return true; }
			}
			return false;
		}

		// The fallback, when AP has no "My own home" card any more: a card of our own. It runs only after AP has
		// handed its array to the list, so it goes into the list's entryList ONCE and nowhere else - pushing it into
		// `_options` as well would add it a second time if AP ever handed the array over again (until 2.0.0 the card
		// went into both before setItems ran, which drew it twice).
		bool AddOwnCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_list, RE::GFxValue& a_entries, std::string& a_problem)
		{
			if (AlreadyInSuboptions(a_entries)) { return true; }   // the same check works on the card list
			RE::GFxValue card;
			if (!MakeOurOption(a_movie, card, OwnCardText()))
			{
				a_problem = "the movie would not make an object";
				return false;
			}
			RE::GFxValue descr, colour;
			a_movie->CreateString(&descr, strings::TR("AHS_CardDescr", home::kCardDescr));
			a_movie->CreateString(&colour, "0x5B6D7A");
			card.SetMember("description", descr);
			card.SetMember("color", colour);
			if (!a_entries.PushBack(card))
			{
				a_problem = "the menu's entry list refused the card";
				return false;
			}
			a_list.Invoke("InvalidateData");
			return true;
		}

		// Returns true when this attempt is over (added, fell back, failed, or the menu closed); false to look again.
		bool Pass(int a_pass);

		// 1.0.3: every pass runs as an SKSE UI task - inside the UI's own message processing, the thread AP's movie
		// advances on. Until 1.0.2 each pass was an AddTask, whose queue on 1.5.97 runs as its own job beside the UI job,
		// so its GetVariable / GetMember / PushBack on AP's movie raced the movie's own advance (the shape that crashed
		// Dragon's Eye Minimap 1.7.5). The retries are paced from a short-lived thread of ours, one pass per ~16 ms with
		// the next posted only after the last has run, because SKSE drains its UI queue in a loop: a UI task that queued
		// the next pass itself would run all of them in the same frame, before AP had read its files. That thread never
		// touches Scaleform - it only waits and posts.
		inline constexpr auto kPassInterval = std::chrono::milliseconds(16);
		inline constexpr auto kPassRunTimeout = std::chrono::seconds(10);

		void Drive()
		{
			for (int pass = 1; pass <= kMaxPasses; ++pass)
			{
				if (pass > 1) { std::this_thread::sleep_for(kPassInterval); }

				auto* tasks = SKSE::GetTaskInterface();
				if (!tasks)
				{
					g_running.store(false);
					return;
				}

				// 0 = waiting for the UI to run it, 1 = look again, 2 = over.
				auto state = std::make_shared<std::atomic<int>>(0);
				tasks->AddUITask([pass, state]() { state->store(Pass(pass) ? 2 : 1); });

				const auto deadline = std::chrono::steady_clock::now() + kPassRunTimeout;
				while (state->load() == 0)
				{
					if (std::chrono::steady_clock::now() > deadline)
					{
						logger::warn("menu: pass {} was not run by the UI within {} s; giving up on this menu", pass,
									 std::chrono::duration_cast<std::chrono::seconds>(kPassRunTimeout).count());
						g_running.store(false);
						return;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				if (state->load() == 2) { return; }
			}
		}

		void Start()
		{
			try
			{
				std::thread(Drive).detach();
			}
			catch (const std::exception& e)
			{
				logger::error("menu: could not start the pass driver ({}); the start will not be added this time", e.what());
				g_running.store(false);
			}
		}

		void Finish(const Report& a_report)
		{
			{
				std::scoped_lock l(g_lock);
				g_report = a_report;
			}
			g_running.store(false);
			if (a_report.injected)
			{
				logger::info("menu: added \"{}\" to Alternate Perspective's \"My own home\" card ({} card(s), {} pass(es))",
							 SubChoiceText(), a_report.cards, a_report.passes);
			}
			else if (a_report.ownCardAdded)
			{
				logger::warn("menu: Alternate Perspective has no \"{}\" card in this version, so the start was added as a card of "
							 "its own ({} card(s), {} pass(es))", home::kApHomeCardText, a_report.cards, a_report.passes);
			}
			else
			{
				logger::error("menu: the start could not be added to Alternate Perspective's menu: {} ({} pass(es))",
							  a_report.problem.empty() ? "the option list never appeared" : a_report.problem, a_report.passes);
			}
		}

		bool Pass(int a_pass)
		{
			Report r;
			r.passes = a_pass;

			auto movie = Movie();
			if (!movie)
			{
				// The menu closed under us (the player pressed Tab); nothing to do and nothing wrong.
				if (a_pass > 1) { logger::debug("menu: it closed before the option list appeared (pass {})", a_pass); }
				g_running.store(false);
				return true;
			}

			// Ready only once AP has read every file and filled the list (see kMaxPasses). The cards in entryList are the
			// same objects as in `_options`, so a suboption pushed into the Home card here is the one AP reads when the
			// card is highlighted.
			RE::GFxValue list, entries;
			const bool ready = movie->GetVariable(&list, home::kApListPath) && list.IsObject() &&
			                   list.GetMember("entryList", &entries) && entries.IsArray() && entries.GetArraySize() > 0;
			if (!ready)
			{
				if (a_pass < kMaxPasses) { return false; }   // Drive() posts the next pass
				RE::GFxValue options;
				r.problem = movie->GetVariable(&options, home::kApOptionsPath) && options.IsArray() ?
				                "Alternate Perspective's menu never finished reading its files" :
				                "Alternate Perspective's option list never appeared - is this another mod's custom menu?";
				Finish(r);
				return true;
			}

			r.sawMenu = true;
			r.cards = static_cast<int>(entries.GetArraySize());
			for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
			{
				RE::GFxValue card;
				if (!entries.GetElement(i, &card) || !card.IsObject()) { continue; }
				if (MemberString(card, "text") != home::kApHomeCardText) { continue; }
				RE::GFxValue subs;
				if (!card.GetMember("suboptions", &subs) || !subs.IsArray())
				{
					r.problem = "the \"My own home\" card has no suboptions to join";
					break;
				}
				if (AlreadyInSuboptions(subs))
				{
					r.injected = true;
					Finish(r);
					return true;
				}
				RE::GFxValue option;
				if (!MakeOurOption(movie.get(), option, SubChoiceText()))
				{
					r.problem = "the movie would not make an object";
					break;
				}
				if (!subs.PushBack(option))
				{
					r.problem = "the card's suboptions refused the option";
					break;
				}
				r.injected = true;
				Finish(r);
				return true;
			}

			if (r.problem.empty())
			{
				r.problem = std::format("no card named {} among the {} the menu has", home::kApHomeCardText, r.cards);
			}
			std::string fallbackProblem;
			if (AddOwnCard(movie.get(), list, entries, fallbackProblem))
			{
				r.ownCardAdded = true;
			}
			else
			{
				r.problem += "; " + fallbackProblem;
			}
			Finish(r);
			return true;
		}

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton()
			{
				static MenuSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
												  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->opening && a_event->menuName == home::kApMenuName && !g_running.exchange(true))
				{
					logger::debug("menu: a custom menu opened; looking for Alternate Perspective's option list");
					Start();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		if (g_installed.exchange(true)) { return; }
		auto* ui = RE::UI::GetSingleton();
		if (!ui)
		{
			logger::error("menu: the UI is not available, so the start cannot be added to Alternate Perspective's menu");
			return;
		}
		ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
		logger::info("menu: listening for Alternate Perspective's menu ({}), to add the start to its \"{}\" card",
					 home::kApMenuName, home::kApHomeCardText);
	}

	const Report& Last()
	{
		return g_report;
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		std::string problem;
		for (char c : g_report.problem)
		{
			if (c == '"' || c == '\\') { problem += '\\'; }
			problem += c;
		}
		return std::format("\"menu\":{{\"sawMenu\":{},\"injected\":{},\"ownCardAdded\":{},\"passes\":{},\"cards\":{},\"problem\":\"{}\"}}",
						   g_report.sawMenu ? "true" : "false", g_report.injected ? "true" : "false",
						   g_report.ownCardAdded ? "true" : "false", g_report.passes, g_report.cards, problem);
	}
}
