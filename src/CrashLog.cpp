#include "CrashLog.h"

#include <DbgHelp.h>

namespace mis::CrashLog
{
	namespace
	{
		std::uintptr_t    g_begin = 0, g_end = 0;
		std::atomic<bool> g_written{ false };
		PVOID             g_handler = nullptr;

		bool IsFatal(DWORD a_code)
		{
			switch (a_code) {
			case EXCEPTION_ACCESS_VIOLATION:
			case EXCEPTION_ILLEGAL_INSTRUCTION:
			case EXCEPTION_INT_DIVIDE_BY_ZERO:
			case EXCEPTION_STACK_OVERFLOW:
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			case EXCEPTION_PRIV_INSTRUCTION:
				return true;
			default:
				return false;
			}
		}

		LONG CALLBACK Handler(EXCEPTION_POINTERS* a_info)
		{
			const auto rec = a_info->ExceptionRecord;
			const auto at = reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress);
			if (!IsFatal(rec->ExceptionCode) || at < g_begin || at >= g_end || g_written.exchange(true)) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			logger::critical("crash_log: exception {:#x} at MojaveInSkyrim.asi+{:#x} (thread {})", rec->ExceptionCode, at - g_begin, ::GetCurrentThreadId());
			if (auto dir = logger::log_directory()) {
				const auto path = *dir / "MojaveInSkyrim.dmp";
				HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file != INVALID_HANDLE_VALUE) {
					MINIDUMP_EXCEPTION_INFORMATION mei{ ::GetCurrentThreadId(), a_info, FALSE };
					::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
						static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mei, nullptr, nullptr);
					::CloseHandle(file);
					logger::critical("crash_log: minidump written to {}", path.string());
				}
			}
			spdlog::default_logger()->flush();
			return EXCEPTION_CONTINUE_SEARCH;  // let the game (and Windows) handle it as usual
		}
	}

	void Install(HMODULE a_self)
	{
		MODULEINFO mi{};
		if (::GetModuleInformation(::GetCurrentProcess(), a_self, &mi, sizeof(mi))) {
			g_begin = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
			g_end = g_begin + mi.SizeOfImage;
			g_handler = ::AddVectoredExceptionHandler(1, Handler);
		}
		logger::info("hook crash_log {}", g_handler ? "ok" : "FAILED");
	}

	bool Installed() { return g_handler != nullptr; }
}
