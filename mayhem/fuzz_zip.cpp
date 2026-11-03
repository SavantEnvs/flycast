// ZIP archive harness. Drives flycast's ZipArchive wrapper (core/archive/ZipArchive.cpp, used to load
// zipped ROMs/discs) over an in-memory archive built by the vendored libzip: central-directory parsing,
// lookups by name/CRC/index and decompression of the entries.
#include "archive/ZipArchive.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

// Reads an entry through its ArchiveFile and returns the name it reports ("" if it could not be opened).
std::string drain(ArchiveFile *f)
{
	std::unique_ptr<ArchiveFile> file(f);
	if (!file)
		return {};
	(void)file->length();
	std::string name = file->getName() != nullptr ? file->getName() : "";
	uint8_t buf[4096];
	// A deflate stream can expand ~1000x; bound the work per entry.
	for (int i = 0; i < 256; i++)
		if (file->Read(buf, sizeof(buf)) == 0)
			break;
	return name;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	ZipArchive zip;
	if (!zip.Open(data, size))
		return -1;

	drain(zip.OpenFirstFile());
	// Entries opened by index report their stored name, which then goes through the by-name lookup.
	for (size_t i = 0; i < 8; i++)
	{
		std::string name = drain(zip.OpenFileByIndex(i));
		if (!name.empty())
			drain(zip.OpenFile(name.c_str()));
	}
	// The CRC of the first local file header (offset 14 after the "PK\3\4" signature) is the one the
	// central directory is expected to carry, so the by-CRC lookup can actually match an entry.
	for (size_t i = 0; i + 18 <= size; i++)
	{
		if (memcmp(data + i, "PK\3\4", 4) == 0)
		{
			uint32_t crc;
			memcpy(&crc, data + i + 14, sizeof(crc));
			drain(zip.OpenFileByCrc(crc));
			break;
		}
	}
	drain(zip.OpenFile("a.rom"));
	return 0;
}
