#pragma once

namespace mis::CrashLog
{
	// crash_log: if code inside MojaveInSkyrim.asi faults, log where and write a minidump next to the
	// log (once). Faults elsewhere in the game are left alone.
	void Install(HMODULE a_self);
	bool Installed();
}
