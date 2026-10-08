#include "Game.h"

#include "CrashLog.h"
#include "Director.h"
#include "Settings.h"

namespace mis::Game
{
	using sheets::Hook;

	namespace
	{
		constexpr RE::FormID kMasterCategory = 0x0EB803;  // _AudioCategoryMaster
		constexpr RE::FormID kMusicCategory = 0x071E64;   // AudioCategoryMUS (the Music slider)

		std::unordered_map<const RE::BSIMusicType*, sheets::Pool> g_types;  // filled once, then read-only
		RE::BGSSoundCategory*  g_master = nullptr;
		RE::BGSSoundCategory*  g_music = nullptr;
		std::uint16_t          g_musicStaticMult = 0xFFFF;  // Skyrim's own value, restored for story moments
		bool                   g_muted = false;          // game thread only
		std::atomic<bool>      g_wantMuted{ false };     // set by the Director
		std::mutex             g_snapLock;
		Snapshot               g_snap;                   // written on the game thread
		const RE::BSIMusicType* g_lastType = nullptr;    // game thread only
		std::mutex             g_noticeLock;
		std::vector<std::string> g_notices;
		std::atomic<bool>      g_started{ false };
		std::atomic<bool>      g_mainMenuOpen{ false };
		bool                   g_seenMainMenu = false;  // game thread only
		int                    g_attempts = 0;

		void ShowQueuedNotices()  // game thread only
		{
			std::vector<std::string> pending;
			{
				std::scoped_lock l(g_noticeLock);
				pending.swap(g_notices);
			}
			for (const auto& n : pending) {
				RE::SendHUDMessage::ShowHUDMessage(n.c_str(), nullptr, false);
			}
		}

		void SampleGame();  // game thread, every frame

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* Get()
			{
				static InputSink s;
				return &s;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				// Called every frame, with or without input.
				SampleGame();
				ShowQueuedNotices();
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto key = static_cast<std::uint32_t>(Settings::Get().radio_iToggleKey);
				for (auto e = *a_event; e; e = e->next) {
					const auto b = e->AsButtonEvent();
					if (!b || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard || b->GetIDCode() != key || !b->IsDown()) {
						continue;
					}
					// ui_state: the key means nothing while a menu or the console has it.
					auto ui = RE::UI::GetSingleton();
					if (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) {
						continue;
					}
					Director::ToggleRadio();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void ResolveMusicTypes()
		{
			auto data = RE::TESDataHandler::GetSingleton();
			std::size_t found = 0;
			for (const auto& row : sheets::kMusicTypes) {
				auto form = data->LookupForm<RE::BGSMusicType>(row.localFormId, row.plugin);
				if (!form) {
					logger::debug("music type {} ({} {:06X}) not loaded", row.editorId, row.plugin, row.localFormId);
					continue;
				}
				g_types[static_cast<RE::BSIMusicType*>(form)] = row.pool;
				++found;
			}
			logger::info("hook music_types ok: {} of {} sheet music types resolved", found, sheets::kMusicTypes.size());
		}

		bool Install(Hook a_hook)
		{
			switch (a_hook) {
			case Hook::asi_boot:
			case Hook::data_loaded:
				return true;  // we're here, so both happened
			case Hook::music_types:
				ResolveMusicTypes();
				return !g_types.empty();
			case Hook::music_current:
				return RE::BSMusicManager::GetSingleton() != nullptr;
			case Hook::music_mute:
			case Hook::volume_sliders:
				g_master = RE::TESForm::LookupByID<RE::BGSSoundCategory>(kMasterCategory);
				g_music = RE::TESForm::LookupByID<RE::BGSSoundCategory>(kMusicCategory);
				if (g_music && !g_muted && g_music->staticMult != 0) {
					g_musicStaticMult = g_music->staticMult;
				}
				return g_master && g_music;
			case Hook::game_hour:
				return RE::Calendar::GetSingleton() != nullptr;
			case Hook::input:
				if (auto input = RE::BSInputDeviceManager::GetSingleton()) {
					input->AddEventSink(InputSink::Get());
					return true;
				}
				return false;
			case Hook::ui_state:
				return RE::UI::GetSingleton() != nullptr;
			case Hook::notify:
				return true;  // proven by the first notice on screen
			case Hook::focus:
				return true;
			case Hook::crash_log:
				return CrashLog::Installed();
			case Hook::kCount:
				break;
			}
			return false;
		}

		bool OnDataLoaded()
		{
			bool ok = true;
			for (const auto& h : sheets::kHooks) {
				if (h.id == Hook::input && ok) {
					continue;  // added last, once everything else is in place (an event sink can't be added twice)
				}
				const bool r = Install(h.id);
				ok &= r;
				if (r) {
					logger::info("hook {} ok", h.name);
				} else {
					logger::error("hook {} FAILED ({})", h.name, h.api);
				}
			}
			if (!ok) {
				return false;
			}
			const bool input = Install(Hook::input);
			logger::info("hook input {}", input ? "ok" : "FAILED");
			if (!input) {
				return false;
			}
			Director::Start();
			return true;
		}

		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* Get()
			{
				static MenuSink s;
				return &s;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				// data_loaded: the Main Menu opens only after every plugin has loaded. (Menus such as the
				// loading spinner open earlier, while plugins are still loading.) Later menu events retry
				// if something wasn't ready.
				const bool mainMenu = a_event && a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME;
				if (a_event && a_event->menuName == RE::MainMenu::MENU_NAME) {
					g_mainMenuOpen = a_event->opening;
				}
				if (!g_started && (mainMenu || g_seenMainMenu) && RE::TESForm::LookupByID(0x7) && g_attempts < 20) {
					g_seenMainMenu = true;
					++g_attempts;
					logger::info("hook data_loaded ok: forms are loaded ({} menu event, attempt {})", a_event->menuName.c_str(), g_attempts);
					g_started = OnDataLoaded();
					if (!g_started && g_attempts < 20) {
						logger::error("not ready yet; trying again on the next menu event");
					} else if (!g_started) {
						logger::error("Mojave in Skyrim stays off: a hook failed");
					}
				}
				ShowQueuedNotices();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		std::string MusicTypeName(const RE::BSIMusicType* a_type)
		{
			if (!a_type) {
				return "(none)";
			}
			if (auto form = skyrim_cast<const RE::BGSMusicType*>(a_type)) {
				const char* ed = form->GetFormEditorID();
				return fmt::format("{} [{:08X}]", ed && *ed ? ed : "?", form->GetFormID());
			}
			return fmt::format("(not a music type form: {})", typeid(*a_type).name());
		}

		void ApplyMute()
		{
			const bool want = g_wantMuted;
			if (!g_music || g_muted == want) {
				return;
			}
			g_muted = want;
			// The static multiplier is form data, not the player's saved Music slider.
			g_music->staticMult = want ? 0 : g_musicStaticMult;
			logger::info("hook music_mute: Skyrim's music {}", want ? "muted" : "restored");
		}

		void SampleGame()
		{
			ApplyMute();
			auto mgr = RE::BSMusicManager::GetSingleton();
			const RE::BSIMusicType* cur = mgr ? mgr->current : nullptr;
			const auto& s = Settings::Get();
			sheets::Pool pool = sheets::Pool::silence;
			bool known = false;
			if (cur) {
				if (const auto it = g_types.find(cur); it != g_types.end()) {
					pool = it->second;
					known = true;
				} else if (!skyrim_cast<const RE::BGSMusicType*>(cur)) {
					// Skyrim's internal NoMusic: it wants silence (main menu, loading screens).
					pool = g_mainMenuOpen ? s.music_sMainMenuPool : s.music_sNoMusicPool;
					known = true;
				} else {
					pool = cur->flags.any(RE::BSIMusicType::MST::kPlaysOnce) ? s.music_sUnknownOncePool : s.music_sUnknownLoopPool;
				}
			}
			const auto cal = RE::Calendar::GetSingleton();
			std::scoped_lock l(g_snapLock);
			if (cur != g_lastType || !g_snap.valid) {
				g_lastType = cur;
				g_snap.typeName = MusicTypeName(cur);
			}
			g_snap.valid = true;
			g_snap.type = reinterpret_cast<std::uintptr_t>(cur);
			g_snap.pool = pool;
			g_snap.known = known;
			g_snap.master = g_master ? std::clamp(g_master->volumeMult, 0.0f, 1.0f) : 1.0f;
			g_snap.music = g_music ? std::clamp(g_music->volumeMult, 0.0f, 1.0f) : 1.0f;
			g_snap.hour = cal ? cal->GetHour() : 12.0f;
		}
	}

	void LogHookTable()
	{
		for (const auto& h : sheets::kHooks) {
			logger::info("hook table: {:<15} {} {}", h.name, h.verified ? "verified" : "unverified", h.api);
		}
	}

	void InstallDataLoadedWatch()
	{
		RE::UI::GetSingleton()->AddEventSink(MenuSink::Get());
	}

	Snapshot Sample()
	{
		std::scoped_lock l(g_snapLock);
		return g_snap;
	}

	void WantSkyrimMusicMuted(bool a_muted) { g_wantMuted = a_muted; }

	bool GameHasFocus()
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
		return pid == ::GetCurrentProcessId();
	}

	void QueueNotice(std::string a_text)
	{
		std::scoped_lock l(g_noticeLock);
		g_notices.push_back(std::move(a_text));
	}
}
