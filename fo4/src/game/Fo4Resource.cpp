#include "Fo4NiUtils.h"

#include "Offsets.h"

// Milestone 5: files packed in BA2 archives (SMP hair NIFs and XMLs), read through the game's own resource system
// with a BSResourceNiBinaryStream (F4SE 1.11.240 GameStreams.h; addresses in Offsets.h). The game's resource system
// is only asked once game data is ready (EnableGameFiles), and only from the main thread, where FO4SMP reads files.

namespace hdt::fo4
{
	namespace
	{
		bool g_enabled = false;
	}

	void EnableGameFiles()
	{
		g_enabled = true;
	}

	std::string ReadGameFile(const char* a_path)
	{
		if (!g_enabled || !a_path || !*a_path) {
			return {};
		}
		using Ctor_t = void*(void* a_stream, const char* a_path, std::uint8_t a_writeable, std::uint64_t a_location, std::int8_t a_fullReadHint);
		using Dtor_t = void(void* a_stream);
		using Read_t = std::uint64_t(void* a_stream, void* a_buffer, std::uint64_t a_length);
		static REL::Relocation<Ctor_t*> ctor{ FO4SMP::Offsets::ResourceStreamCtor };
		static REL::Relocation<Dtor_t*> dtor{ FO4SMP::Offsets::ResourceStreamDtor };
		static REL::Relocation<Read_t*> read{ FO4SMP::Offsets::ResourceStreamRead };

		alignas(16) std::byte stream[0x30]{};
		ctor(stream, a_path, 0, 0, 0);
		std::string out;
		// IsValid: the resource stream at +0x10 is set when the file was found.
		if (*reinterpret_cast<void**>(stream + 0x10)) {
			std::array<char, 0x10000> chunk;
			for (int i = 0; i < 4096; ++i) {  // up to 256 MB
				const auto n = read(stream, chunk.data(), chunk.size());
				if (n == 0 || n > chunk.size()) {
					break;
				}
				out.append(chunk.data(), static_cast<std::size_t>(n));
				if (n < chunk.size()) {
					break;
				}
			}
		}
		dtor(stream);
		static int s_logged = 0;
		if (s_logged < 40) {
			++s_logged;
			logger::info("Game files: {} {} ({} bytes, from the game's resource system: loose or BA2){}", out.empty() ? "didn't find" : "read", a_path, out.size(),
				s_logged == 40 ? "; further reads not logged" : "");
		}
		return out;
	}
}
