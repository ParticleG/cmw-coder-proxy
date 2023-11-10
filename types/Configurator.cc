#include <format>

#include <magic_enum.hpp>

#include <types/Configurator.h>
#include <utils/logger.h>
#include <utils/system.h>

using namespace magic_enum;
using namespace std;
using namespace types;
using namespace utils;

Configurator::Configurator() {
    const auto [major, minor, build, _] = system::getVersion();
    if (major == 3 && minor == 5) {
        _siVersion = make_pair(
                SiVersion::Major::V35,
                enum_cast<SiVersion::Minor>(build).value_or(SiVersion::Minor::Unknown)
        );
        _siVersionString = "_3.50." + format("{:0>{}}", build, 4);
    } else {
        _siVersion = make_pair(
                SiVersion::Major::V40,
                enum_cast<SiVersion::Minor>(build).value_or(SiVersion::Minor::Unknown)
        );
        _siVersionString = "_4.00." + format("{:0>{}}", build, 4);
    }
}

pair<SiVersion::Major, SiVersion::Minor> Configurator::version() const {
    return _siVersion;
}

string Configurator::reportVersion(const string &version) const {
    return version + _siVersionString;
}
