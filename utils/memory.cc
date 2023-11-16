#include <memory>

#include <utils/memory.h>

#include <windows.h>

using namespace std;
using namespace utils;


uint64_t memory::scanPattern(const string&pattern) {
    uint64_t varAddress = 0;
    const shared_ptr<void> sharedProcessHandle(GetCurrentProcess(), CloseHandle);
    if (sharedProcessHandle) {
        const auto BaseAddress = reinterpret_cast<uint64_t>(GetModuleHandle(nullptr));
        string scannedString;
        scannedString.resize(1024);
        SIZE_T readLength = 0;
        for (auto i = 0; i < 100000; i++) {
            if (ReadProcessMemory(
                sharedProcessHandle.get(),
                reinterpret_cast<LPCVOID>(BaseAddress + i * scannedString.size() * sizeof(char)),
                scannedString.data(),
                scannedString.size() * sizeof(char),
                &readLength
            )) {
                if (const auto found = scannedString.find(pattern); found != string::npos) {
                    varAddress = BaseAddress + i * scannedString.size() * sizeof(char) + found;
                    break;
                }
            }
        }
    }
    return varAddress;
}

optional<uint32_t> memory::readMemory32(uint64_t address, const bool relative) {
    uint32_t value = 0;
    const shared_ptr<void> sharedProcessHandle(GetCurrentProcess(), CloseHandle);
    if (sharedProcessHandle) {
        if (relative) {
            address += reinterpret_cast<uint64_t>(GetModuleHandle(nullptr));
        }
        if (ReadProcessMemory(
            sharedProcessHandle.get(),
            reinterpret_cast<LPCVOID>(address),
            &value,
            sizeof(uint32_t),
            nullptr
        )) {
            return value;
        }
    }
    return nullopt;
}

bool memory::writeMemory(const uint64_t address, const string&value) {
    const shared_ptr<void> sharedProcessHandle(GetCurrentProcess(), CloseHandle);
    if (sharedProcessHandle) {
        return WriteProcessMemory(
            sharedProcessHandle.get(),
            reinterpret_cast<LPVOID>(address),
            value.data(),
            (value.length() + 1) * sizeof(char),
            nullptr
        );
    }
    return false;
}
