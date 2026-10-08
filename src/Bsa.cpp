#include "Bsa.h"

namespace mis
{
	namespace
	{
		constexpr std::uint32_t kDirNames = 0x1;
		constexpr std::uint32_t kFileNames = 0x2;
		constexpr std::uint32_t kCompressed = 0x4;
		constexpr std::uint32_t kEmbedNames = 0x100;
		constexpr std::uint32_t kSizeToggle = 0x40000000;

		template <class T>
		bool ReadPod(std::ifstream& a_in, T& a_out)
		{
			return static_cast<bool>(a_in.read(reinterpret_cast<char*>(&a_out), sizeof(T)));
		}

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(c == '/' ? '\\' : std::tolower(c)); });
			return a_s;
		}
	}

	bool Bsa::Open(const fs::path& a_path)
	{
		path = a_path;
		entries.clear();
		std::ifstream in(a_path, std::ios::binary);
		struct Header
		{
			char          magic[4];
			std::uint32_t version, folderOffset, archiveFlags, folderCount, fileCount, folderNamesLength, fileNamesLength, fileFlags;
		} h{};
		if (!ReadPod(in, h) || std::memcmp(h.magic, "BSA\0", 4) != 0 || h.version != 104) {
			logger::error("bsa: {} is not a New Vegas archive (version 104)", a_path.string());
			return false;
		}
		flags = h.archiveFlags;
		if (!(flags & kDirNames) || !(flags & kFileNames)) {
			logger::error("bsa: {} has no file names", a_path.string());
			return false;
		}
		std::vector<std::uint32_t> counts(h.folderCount);
		for (auto& c : counts) {
			std::uint64_t hash;
			std::uint32_t offset;
			if (!ReadPod(in, hash) || !ReadPod(in, c) || !ReadPod(in, offset)) {
				return false;
			}
		}
		entries.reserve(h.fileCount);
		for (const auto c : counts) {
			std::uint8_t len = 0;
			ReadPod(in, len);
			std::string folder(len, '\0');
			in.read(folder.data(), len);
			folder.resize(len ? len - 1 : 0);  // drop the terminator
			folder = Lower(folder);
			for (std::uint32_t i = 0; i < c; ++i) {
				std::uint64_t hash;
				std::uint32_t size, offset;
				ReadPod(in, hash);
				ReadPod(in, size);
				ReadPod(in, offset);
				const bool compressed = ((flags & kCompressed) != 0) != ((size & kSizeToggle) != 0);
				entries.push_back({ folder, {}, size & 0x3FFFFFFF, offset, compressed });
			}
		}
		std::string names(h.fileNamesLength, '\0');
		in.read(names.data(), h.fileNamesLength);
		if (!in) {
			logger::error("bsa: {} is truncated", a_path.string());
			entries.clear();
			return false;
		}
		std::size_t pos = 0;
		for (auto& e : entries) {
			const auto end = names.find('\0', pos);
			e.name = Lower(names.substr(pos, end - pos));
			pos = end + 1;
		}
		logger::info("bsa: {} ({} files)", a_path.filename().string(), entries.size());
		return true;
	}

	std::vector<const Bsa::Entry*> Bsa::Folder(std::string_view a_folder) const
	{
		const auto want = Lower(std::string(a_folder));
		std::vector<const Entry*> out;
		for (const auto& e : entries) {
			if (e.folder == want) {
				out.push_back(&e);
			}
		}
		return out;
	}

	std::vector<std::uint8_t> Bsa::Read(const Entry& a_entry) const
	{
		if (a_entry.compressed) {
			logger::warn("bsa: {}\\{} is compressed; skipped", a_entry.folder, a_entry.name);
			return {};
		}
		std::ifstream in(path, std::ios::binary);
		in.seekg(a_entry.offset);
		std::uint32_t size = a_entry.size;
		if (flags & kEmbedNames) {
			std::uint8_t len = 0;
			ReadPod(in, len);
			in.seekg(len, std::ios::cur);
			size -= len + 1u;
		}
		std::vector<std::uint8_t> data(size);
		if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
			logger::warn("bsa: couldn't read {}\\{}", a_entry.folder, a_entry.name);
			return {};
		}
		return data;
	}
}
