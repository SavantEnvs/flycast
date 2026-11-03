// INI/config parser harness: IniFile::load() in both plain and C-escape modes, then lookups
// and a save() round-trip over whatever was parsed.
#include "cfg/ini.h"

#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const std::string text((const char *)data, size);
	for (int cEscape = 0; cEscape < 2; cEscape++)
	{
		config::IniFile ini;
		ini.load(text, cEscape != 0);
		ini.hasSection("config");
		ini.getString("config", "name");
		ini.getInt("config", "value", 1);
		ini.getInt64("config", "value", 1);
		ini.getBool("config", "flag");
		ini.getFloat("config", "ratio", 1.f);
		for (const std::string& name : ini.getEntryNames("config"))
			ini.getString("config", name);
		std::string out;
		ini.save(out);
		config::IniFile again;
		again.load(out, cEscape != 0);
	}
	return 0;
}
