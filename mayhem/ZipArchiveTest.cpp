// Fork-side gtest for flycast's ZipArchive wrapper (upstream ships none): opens the two multi-entry
// archives from the fuzz_zip seed set and checks entry names, sizes, CRC lookup and known content.
#include "gtest/gtest.h"
#include "archive/ZipArchive.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static std::vector<uint8_t> slurp(const char *name)
{
	std::string path = std::string(MAYHEM_SEEDS_DIR "/") + name;
	FILE *f = fopen(path.c_str(), "rb");
	EXPECT_NE(nullptr, f) << path;
	std::vector<uint8_t> data;
	if (f == nullptr)
		return data;
	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		data.insert(data.end(), buf, buf + n);
	fclose(f);
	return data;
}

TEST(ZipArchiveTest, StoredEntries)
{
	std::vector<uint8_t> data = slurp("multi_stored.zip");
	ASSERT_FALSE(data.empty());
	ZipArchive zip;
	ASSERT_TRUE(zip.Open(data.data(), data.size()));

	std::unique_ptr<ArchiveFile> first(zip.OpenFirstFile());
	ASSERT_NE(nullptr, first);
	ASSERT_STREQ("a.rom", first->getName());
	ASSERT_EQ(256u, first->length());
	uint8_t buf[256];
	ASSERT_EQ(256u, first->Read(buf, sizeof(buf)));
	for (int i = 0; i < 256; i++)
		ASSERT_EQ(i, buf[i]);

	std::unique_ptr<ArchiveFile> second(zip.OpenFileByIndex(1));
	ASSERT_NE(nullptr, second);
	ASSERT_STREQ("b.rom", second->getName());
	ASSERT_EQ(64u, second->length());

	ASSERT_EQ(nullptr, zip.OpenFileByIndex(2));
	ASSERT_EQ(nullptr, zip.OpenFile("missing.rom"));
}

TEST(ZipArchiveTest, DeflatedEntries)
{
	std::vector<uint8_t> data = slurp("multi_deflated.zip");
	ASSERT_FALSE(data.empty());
	ZipArchive zip;
	ASSERT_TRUE(zip.Open(data.data(), data.size()));

	std::unique_ptr<ArchiveFile> game(zip.OpenFile("game.bin"));
	ASSERT_NE(nullptr, game);
	ASSERT_EQ(760u, game->length());
	uint8_t buf[760];
	ASSERT_EQ(760u, game->Read(buf, sizeof(buf)));
	ASSERT_EQ(0, memcmp("SEGA DREAMCAST R", buf, 16));
	uint32_t sum = 0;
	for (uint8_t b : buf)
		sum += b;
	ASSERT_EQ(51280u, sum);

	std::unique_ptr<ArchiveFile> readme(zip.OpenFileByCrc(0x53f785e2));
	ASSERT_NE(nullptr, readme);
	ASSERT_STREQ("dir/readme.txt", readme->getName());
	ASSERT_EQ(280u, readme->length());
	uint8_t text[280];
	ASSERT_EQ(280u, readme->Read(text, sizeof(text)));
	ASSERT_EQ(0, memcmp("hello flycast\nhello flycast\n", text, 28));

	ASSERT_EQ(nullptr, zip.OpenFileByCrc(0x12345678));
}

TEST(ZipArchiveTest, RejectsGarbage)
{
	static const uint8_t junk[] = "this is not a zip archive";
	ZipArchive zip;
	ASSERT_FALSE(zip.Open(junk, sizeof(junk)));
}
