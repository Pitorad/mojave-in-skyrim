#include "Director.h"
#include "Game.h"
#include "Settings.h"

// Mojave in Skyrim loads as an .asi through Ultimate ASI Loader (which Melty installs for every
// player), so it needs no SKSE and works on game versions SKSE isn't published for. DllMain only
// starts a thread; the thread waits for the game's UI to exist and then listens for menu events,
// which tell it when the game's data has loaded (game_hooks: asi_boot, data_loaded).

namespace
{
	HMODULE g_module = nullptr;

	void SetupLog()
	{
		auto dir = logger::log_directory();
		if (!dir) {
			return;
		}
		std::error_code ec;
		fs::create_directories(*dir, ec);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "MojaveInSkyrim.log").string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	fs::path ModulePath()
	{
		std::wstring buf(MAX_PATH, L'\0');
		for (;;) {
			const auto n = ::GetModuleFileNameW(g_module, buf.data(), static_cast<DWORD>(buf.size()));
			if (n < buf.size()) {
				buf.resize(n);
				return buf;
			}
			buf.resize(buf.size() * 2);
		}
	}

	DWORD WINAPI BootThread(LPVOID)
	{
		SetupLog();
		const auto self = ModulePath();
		logger::info("Mojave in Skyrim {} ({}) in {}", MIS_VERSION, self.filename().string(), REL::Module::get().version().string());

		mis::Settings::Load(fs::path(self).replace_extension(".ini"));
		mis::Game::LogHookTable();

		// asi_boot: the game creates its UI singleton during start-up; event sinks need it.
		for (int i = 0; !RE::UI::GetSingleton(); ++i) {
			if (i == 1200) {  // two minutes
				logger::error("asi_boot: the game's UI never appeared; Mojave in Skyrim stays off");
				return 0;
			}
			std::this_thread::sleep_for(100ms);
		}
		logger::info("hook asi_boot ok: UI is up");
		mis::Game::InstallDataLoadedWatch();
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE a_module, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH) {
		g_module = a_module;
		::DisableThreadLibraryCalls(a_module);
		// Loader lock is held here: start a thread and nothing else.
		if (auto t = ::CreateThread(nullptr, 0, BootThread, nullptr, 0, nullptr)) {
			::CloseHandle(t);
		}
	}
	return TRUE;
}
