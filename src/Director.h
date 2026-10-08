#pragma once

namespace mis
{
	// The mod's own thread: follows Skyrim's music choice with New Vegas tracks and runs Radio New Vegas.
	namespace Director
	{
		void Start();        // after data_loaded; starts the thread once
		void ToggleRadio();  // game thread (radio key)
	}
}
