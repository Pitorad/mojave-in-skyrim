#include "Game.h"

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
		std::atomic<bool>      g_muted{ false };
		std::mutex             g_noticeLock;
		std::vector<std::string> g_notices;
		std::atomic<bool>      g_started{ false };

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
				if (g_music && !g_muted) {
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
			case Hook::kCount:
				break;
			}
			return false;
		}

		void OnDataLoaded()
		{
			bool ok = true;
			for (const auto& h : sheets::kHooks) {
				const bool r = Install(h.id);
				ok &= r;
				if (r) {
					logger::info("hook {} ok", h.name);
				} else {
					logger::error("hook {} FAILED ({})", h.name, h.api);
				}
			}
			if (!ok) {
				logger::error("Mojave in Skyrim stays off: a hook failed");
				return;
			}
			Director::Start();
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
				// data_loaded: the first menu event once the player form (0x7) exists.
				if (!g_started && RE::TESForm::LookupByID(0x7)) {
					g_started = true;
					logger::info("hook data_loaded ok: forms are loaded ({} menu event)", a_event ? a_event->menuName.c_str() : "?");
					OnDataLoaded();
				}
				ShowQueuedNotices();
				return RE::BSEventNotifyControl::kContinue;
			}
		};
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

	MusicNow CurrentMusic()
	{
		MusicNow now;
		auto mgr = RE::BSMusicManager::GetSingleton();
		const RE::BSIMusicType* cur = mgr ? mgr->current : nullptr;
		now.type = cur;
		if (!cur) {
			return now;
		}
		if (const auto it = g_types.find(cur); it != g_types.end()) {
			now.pool = it->second;
			now.known = true;
		} else {
			const auto& s = Settings::Get();
			now.pool = cur->flags.any(RE::BSIMusicType::MST::kPlaysOnce) ? s.music_sUnknownOncePool : s.music_sUnknownLoopPool;
		}
		return now;
	}

	std::string MusicTypeName(const void* a_type)
	{
		if (!a_type) {
			return "(none)";
		}
		auto t = static_cast<const RE::BSIMusicType*>(a_type);
		if (auto form = skyrim_cast<const RE::BGSMusicType*>(t)) {
			const char* ed = form->GetFormEditorID();
			return fmt::format("{} [{:08X}]", ed && *ed ? ed : "?", form->GetFormID());
		}
		return "(not a music type form)";
	}

	void SetSkyrimMusicMuted(bool a_muted)
	{
		if (!g_music || g_muted == a_muted) {
			return;
		}
		g_muted = a_muted;
		// The static multiplier is form data, not the player's saved Music slider.
		g_music->staticMult = a_muted ? 0 : g_musicStaticMult;
		logger::info("hook music_mute: Skyrim's music {}", a_muted ? "muted" : "restored");
	}

	float MasterVolume() { return g_master ? std::clamp(g_master->volumeMult, 0.0f, 1.0f) : 1.0f; }
	float MusicVolume() { return g_music ? std::clamp(g_music->volumeMult, 0.0f, 1.0f) : 1.0f; }

	float Hour()
	{
		auto cal = RE::Calendar::GetSingleton();
		return cal ? cal->GetHour() : 12.0f;
	}

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
