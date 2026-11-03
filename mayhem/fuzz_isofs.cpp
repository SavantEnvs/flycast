// ISO9660 reader harness. The fuzz input is the disc image starting at its Primary Volume Descriptor
// (LBA 16): the 16 system-area sectors before it are zeros by spec, so they are synthesized, and any
// later LBA wraps modulo the input's sector count. That keeps a whole PVD + directory tree + file data
// inside a few KB (fuzzers cap inputs at a few KB by default), while IsoFs walks the volume descriptor,
// the directory tree and file contents.
#include "imgread/isofs.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr size_t SECTOR = 2048;
constexpr size_t SYSTEM_AREA = 16 * SECTOR;

class MemFile : public hostfs::File
{
public:
	MemFile(const uint8_t *data, size_t size) : data(data), size_(size) {}

	size_t read(void *buffer, size_t size, size_t count) override
	{
		uint8_t *out = static_cast<uint8_t *>(buffer);
		size_t total = size * count;
		while (total > 0)
		{
			size_t n;
			if (pos < SYSTEM_AREA)
			{
				n = std::min(total, SYSTEM_AREA - pos);
				memset(out, 0, n);
			}
			else
			{
				size_t off = (pos - SYSTEM_AREA) % size_;
				n = std::min(total, size_ - off);
				memcpy(out, data + off, n);
			}
			out += n;
			pos += n;
			total -= n;
		}
		return count;
	}
	size_t write(const void *, size_t, size_t) override { return 0; }
	s64 tell() override { return (s64)pos; }
	int seek(s64 offset, int whence) override
	{
		s64 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (s64)pos : (s64)(SYSTEM_AREA + size_);
		if (base + offset < 0)
			return -1;
		pos = (size_t)(base + offset);
		return 0;
	}
	char *gets(char *, int) override { return nullptr; }
	s64 size() override { return (s64)(SYSTEM_AREA + size_); }
	int eof() override { return 0; }
	int error() override { return 0; }

private:
	const uint8_t *data;
	size_t size_;
	size_t pos = 0;
};

void walk(IsoFs::Directory *dir, int depth)
{
	std::vector<IsoFs::Entry *> entries = dir->list();
	size_t n = 0;
	for (IsoFs::Entry *e : entries)
	{
		if (n++ < 8)
		{
			if (e->isDirectory())
			{
				if (depth < 2)
					walk(static_cast<IsoFs::Directory *>(e), depth + 1);
			}
			else
			{
				auto *f = static_cast<IsoFs::File *>(e);
				std::vector<u8> content(std::min<u32>(f->getSize(), 4096));
				f->read(content.data(), content.size());
			}
		}
		delete e;
	}
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const size_t sectors = size / SECTOR;
	if (sectors == 0)
		return -1;
	size = sectors * SECTOR;
	Disc disc;
	Track track;
	track.StartFAD = 150;
	track.EndFAD = 150 + 16 + sectors - 1;
	track.CTRL = 4;
	track.ADR = 1;
	track.file = new RawTrackFile(new MemFile(data, size), 0, 150, SECTOR);
	disc.tracks.push_back(track);
	Session session;
	session.StartFAD = 150;
	session.FirstTrack = 1;
	disc.sessions.push_back(session);
	disc.LeadOut.StartFAD = 150 + 16 + sectors;
	disc.EndFAD = 150 + 16 + sectors;
	disc.type = CdRom_XA;

	IsoFs fs(&disc);
	IsoFs::Directory *root = fs.getRoot();
	walk(root, 0);
	delete root;
	return 0;
}
