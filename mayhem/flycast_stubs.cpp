// Environment mocks for the disc-image/ini fuzz harnesses and the unit-test runner: the
// emulator globals and platform services that core/imgread/*.cpp only touches at its edges.
// None of the parsers under test are replaced or wrapped.
#include "types.h"
#include "cfg/option.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/gdrom/gdrom_if.h"
#include "hw/sh4/sh4_sched.h"
#include "oslib/i18n.h"
#include "oslib/oslib.h"
#include "serialize.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

settings_t settings;
config::StubBoolOption config::GGPOEnable;

GD_SecNumbT SecNumber;
int sns_asc, sns_ascq, sns_key;

void gd_setdisc() {}

int sh4_sched_register(int, sh4_sched_callback *, void *) { return 0; }
void sh4_sched_unregister(int) {}
void sh4_sched_request(int, int) {}
bool sh4_sched_is_scheduled(int) { return false; }
void sh4_sched_serialize(Serializer&, int) {}
void sh4_sched_deserialize(Deserializer&, int) {}

namespace i18n {
std::string Ts(const std::string& msg) { return msg; }
const char *T(const char *msg) { return msg; }
}

void GenericLog(LogTypes::LOG_LEVELS, LogTypes::LOG_TYPE, const char *, int, const char *, ...) {}

void fatal_error(const char *text, ...)
{
	va_list args;
	va_start(args, text);
	vfprintf(stderr, text, args);
	va_end(args);
	fputc('\n', stderr);
}

[[noreturn]] void os_DebugBreak() { abort(); }
void os_SetThreadName(const char *) {}
