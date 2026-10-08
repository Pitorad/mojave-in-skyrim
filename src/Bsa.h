#pragma once

namespace mis
{
	// Reads files out of a Fallout 3 / New Vegas archive (BSA version 104) in the player's own install.
	class Bsa
	{
	public:
		struct Entry
		{
			std::string   folder;  // lower case, backslashes
			std::string   name;    // lower case
			std::uint32_t size{ 0 };
			std::uint32_t offset{ 0 };
			bool          compressed{ false };
		};

		bool Open(const fs::path& a_path);
		// Every file directly inside a_folder (case-insensitive, backslashes).
		std::vector<const Entry*> Folder(std::string_view a_folder) const;
		// The file's bytes; empty if it can't be read (compressed entries are skipped: New Vegas
		// stores its music and voices uncompressed).
		std::vector<std::uint8_t> Read(const Entry& a_entry) const;

		const fs::path& Path() const { return path; }

	private:
		fs::path           path;
		std::uint32_t      flags{ 0 };
		std::vector<Entry> entries;
	};
}
