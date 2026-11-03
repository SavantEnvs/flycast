// Disc-image parser harness: the fuzz input becomes `disc.<DISC_EXT>` and goes through
// OpenDisc(); a successfully parsed disc then gets its tracks/sessions/sectors exercised.
// gdi/cue images reference sibling track files, so a few dummy ones are created next to it.
#include "imgread/common.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#ifndef DISC_EXT
#error "DISC_EXT must be defined (gdi, cue, cdi or chd)"
#endif

static std::string imagePath;

static void writeFile(const std::string& path, const uint8_t *data, size_t size)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (f == nullptr)
		abort();
	if (size != 0 && fwrite(data, 1, size, f) != size)
		abort();
	fclose(f);
}

static void writeTrackFile(const std::string& path, unsigned sectors)
{
	std::vector<uint8_t> buf(2352 * (size_t)sectors);
	for (unsigned s = 0; s < sectors; s++)
	{
		uint8_t *p = &buf[2352 * (size_t)s];
		p[0] = 0;
		memset(p + 1, 0xff, 10);
		p[11] = 0;
		p[15] = 1;
		for (unsigned i = 16; i < 2352; i++)
			p[i] = (uint8_t)(i * 7 + s * 13);
	}
	writeFile(path, buf.data(), buf.size());
}

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
	const char *tmp = getenv("TMPDIR");
	std::string tmpl = std::string(tmp != nullptr && *tmp != '\0' ? tmp : "/tmp") + "/fuzz_disc.XXXXXX";
	if (mkdtemp(tmpl.data()) == nullptr)
		abort();
	if (chdir(tmpl.c_str()) != 0)
		abort();
	imagePath = tmpl + "/disc." DISC_EXT;
#if defined(FUZZ_TRACK_FILES)
	static const char *const names[] = {
		"cs01.bin", "cs02.raw", "cs03.bin", "cs (0,1).bin",
		"track01.bin", "track02.raw", "track03.bin", "track04.bin",
		"a.bin", "b.bin", "c.bin",
	};
	for (const char *name : names)
		writeTrackFile(tmpl + "/" + name, 24);
#endif
	return 0;
}

static void touchSectors(Disc *disc, u32 fad, u32 count, u32 fmt)
{
	static u8 buf[3 * 2448];
	disc->ReadSectors(fad, count, buf, fmt, false);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	writeFile(imagePath, data, size);

	Disc *disc = nullptr;
	try {
		disc = OpenDisc(imagePath);
	} catch (const FlycastException&) {
		return 0;
	}
	if (disc == nullptr)
		return 0;

	u8 info[6];
	for (size_t i = 0; i <= disc->sessions.size() && i < 8; i++)
		disc->GetSessionInfo(info, (u8)i);
	disc->GetBaseFAD();

	size_t n = 0;
	for (const Track& track : disc->tracks)
	{
		if (n++ == 6)
			break;
		const u32 fmt = track.isDataTrack() ? 2048 : 2352;
		touchSectors(disc, track.StartFAD, 3, fmt);
		touchSectors(disc, track.StartFAD, 1, 2352);
		if (track.EndFAD >= track.StartFAD)
			touchSectors(disc, track.EndFAD, 1, fmt);
	}
	delete disc;
	return 0;
}
