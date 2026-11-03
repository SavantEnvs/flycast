// Environment mock for the fuzz/test builds: shadows core/cfg/option.h so imgread/common.cpp links
// without dragging in the whole options/ini subsystem. Only GGPOEnable (netplay digest) is read there.
#pragma once

namespace config {

struct StubBoolOption {
	operator bool() const { return false; }
};
extern StubBoolOption GGPOEnable;

}
