#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <vector>

namespace {
bool write(HANDLE file, const void* data, DWORD size) {
    DWORD written = 0;
    return WriteFile(file, data, size, &written, nullptr) && written == size;
}
}

int wmain(int argc, wchar_t** argv) {
    const DWORD size = GetEnvironmentVariableW(L"CREATE_ALIAS_ARGV_OUTPUT", nullptr, 0);
    if (!size) return 2;
    std::vector<wchar_t> path(size);
    if (!GetEnvironmentVariableW(L"CREATE_ALIAS_ARGV_OUTPUT", path.data(), size)) return 3;
    const HANDLE file = CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 4;
    const std::uint32_t count = static_cast<std::uint32_t>(argc - 1);
    bool ok = write(file, &count, sizeof(count));
    for (int i = 1; ok && i < argc; ++i) {
        const auto length = static_cast<std::uint32_t>(std::wcslen(argv[i]));
        ok = write(file, &length, sizeof(length)) &&
             write(file, argv[i], length * sizeof(wchar_t));
    }
    if (!CloseHandle(file)) ok = false;
    return ok ? 0 : 5;
}
