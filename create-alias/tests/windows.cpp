#include "../native.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdint>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using alias_plugin::Options;
using alias_plugin::Result;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void hr(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    require(size != 0, "UTF-8 conversion failed");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string path(const fs::path& value) { return utf8(value.wstring()); }
void success(const Result& result) {
    if (!result.ok) {
        std::string message = "createAlias failed:";
        for (const auto& error : result.errors)
            message += " " + error.code + "/" + error.option + " " + error.message;
        throw std::runtime_error(message);
    }
    require(result.errors.empty(), "Successful result contains errors");
    require(result.kind == "shortcut", "Wrong Windows kind");
}
void failure(const Result& result, const std::string& code) {
    require(!result.ok, "Expected failure");
    require(!result.errors.empty(), "Failure lacks structured errors");
    require(result.errors.front().code == code, "Unexpected stable error code");
}
void writeFile(const fs::path& destination, const std::string& bytes) {
    std::ofstream file(destination, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(file.good(), "Cannot write fixture");
}
std::string bytes(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    require(input.good(), "Cannot read fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
struct Link {
    IShellLinkW* link = nullptr;
    IPersistFile* persist = nullptr;
    explicit Link(const fs::path& file) {
        hr(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                            IID_IShellLinkW, reinterpret_cast<void**>(&link)), "Cannot create COM reader");
        const auto query = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
        if (FAILED(query)) { link->Release(); link = nullptr; hr(query, "Cannot query persistence"); }
        const auto load = persist->Load(file.c_str(), STGM_READ);
        if (FAILED(load)) {
            persist->Release(); link->Release(); persist = nullptr; link = nullptr;
            hr(load, "Cannot load generated shortcut");
        }
    }
    ~Link() { if (persist) persist->Release(); if (link) link->Release(); }
};
struct Root {
    fs::path value;
    Root() {
        GUID id{};
        hr(CoCreateGuid(&id), "Cannot generate test root name");
        wchar_t name[40]{};
        StringFromGUID2(id, name, 40);
        value = fs::current_path() / (std::wstring(L"alias-native-tests-") + name);
        require(fs::create_directory(value), "Cannot create test root");
    }
    ~Root() {
        std::error_code ignored;
        fs::remove_all(value, ignored);
    }
};

std::vector<std::wstring> recorded(const fs::path& output) {
    std::ifstream file(output, std::ios::binary);
    require(file.good(), "Recorder did not produce output");
    auto number = [&]() {
        std::uint32_t value = 0;
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        require(file.good(), "Truncated recorder header");
        require(value < 100000, "Unreasonable recorder length");
        return value;
    };
    const auto count = number();
    std::vector<std::wstring> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::wstring text(number(), L'\0');
        file.read(reinterpret_cast<char*>(text.data()), text.size() * sizeof(wchar_t));
        require(file.good(), "Truncated recorder argument");
        result.push_back(std::move(text));
    }
    require(file.peek() == std::char_traits<char>::eof(), "Unexpected recorder bytes");
    return result;
}

void launch(const fs::path& shortcut, const fs::path& output,
            const std::vector<std::wstring>& expected) {
    require(SetEnvironmentVariableW(L"CREATE_ALIAS_ARGV_OUTPUT", output.c_str()) != FALSE,
            "Cannot set recorder environment");
    SHELLEXECUTEINFOW execution{};
    execution.cbSize = sizeof(execution);
    execution.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execution.lpFile = shortcut.c_str();
    execution.nShow = SW_SHOWNORMAL;
    const bool started = ShellExecuteExW(&execution) != FALSE;
    SetEnvironmentVariableW(L"CREATE_ALIAS_ARGV_OUTPUT", nullptr);
    require(started, "ShellExecuteExW could not launch shortcut");
    require(execution.hProcess != nullptr, "ShellExecuteExW returned no process");
    const DWORD wait = WaitForSingleObject(execution.hProcess, 30000);
    DWORD status = 1;
    const bool exited = GetExitCodeProcess(execution.hProcess, &status) != FALSE;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(execution.hProcess, 99);
        WaitForSingleObject(execution.hProcess, 5000);
    }
    CloseHandle(execution.hProcess);
    require(wait == WAIT_OBJECT_0 && exited && status == 0, "Recorder failed or timed out");
    require(recorded(output) == expected, "Launched shortcut did not preserve exact argv");
}

void readback(const fs::path& root, const fs::path& recorder) {
    const fs::path working = root / L"work directory \u65e5\u672c\u8a9e";
    fs::create_directory(working);
    Options options;
    options.arguments = std::vector<std::string>{"space argument", "", "quote\"here", "tail\\"};
    options.workingDirectory = path(working);
    options.description = "Unicode description \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    options.iconPath = path(recorder);
    options.iconIndex = -7;
    options.showCommand = 3;
    const auto result = alias_plugin::createAlias(path(recorder), path(root / L"properties.LNK"), options);
    success(result);
    require(result.aliasPath == path(root / L"properties.LNK"), "Case-insensitive suffix was duplicated");
    require(result.targetPath == path(recorder), "Target path result changed");
    Link saved(root / L"properties.LNK");
    wchar_t buffer[32767]{};
    hr(saved.link->GetPath(buffer, 32767, nullptr, SLGP_RAWPATH), "Cannot read target");
    require(fs::path(buffer) == recorder, "Target readback mismatch");
    hr(saved.link->GetArguments(buffer, 32767), "Cannot read arguments");
    require(utf8(buffer) == alias_plugin::quoteArguments(*options.arguments), "Argument readback mismatch");
    hr(saved.link->GetWorkingDirectory(buffer, 32767), "Cannot read working directory");
    require(fs::path(buffer) == working, "Working directory readback mismatch");
    hr(saved.link->GetDescription(buffer, 32767), "Cannot read description");
    require(utf8(buffer) == *options.description, "Description readback mismatch");
    int number = 0;
    hr(saved.link->GetIconLocation(buffer, 32767, &number), "Cannot read icon");
    require(fs::path(buffer) == recorder && number == -7, "Icon readback mismatch");
    hr(saved.link->GetShowCmd(&number), "Cannot read show command");
    require(number == 3, "Show command readback mismatch");
    for (int show : {1, 7}) {
        Options showOptions;
        showOptions.showCommand = show;
        const auto destination = root / (std::wstring(L"show-") + std::to_wstring(show) + L".lnk");
        success(alias_plugin::createAlias(path(recorder), path(destination), showOptions));
        Link link(destination);
        hr(link.link->GetShowCmd(&number), "Cannot read supported show command");
        require(number == show, "Supported show command not retained");
    }
    Options empty;
    empty.arguments = std::vector<std::string>{};
    empty.description = "";
    empty.iconPath = path(recorder);
    empty.iconIndex = 0;
    success(alias_plugin::createAlias(path(recorder), path(root / L"empty-options"), empty));
    Link emptyLink(root / L"empty-options.lnk");
    hr(emptyLink.link->GetArguments(buffer, 32767), "Cannot read empty arguments");
    require(buffer[0] == L'\0', "Explicit empty array not retained");
    hr(emptyLink.link->GetDescription(buffer, 32767), "Cannot read empty description");
    require(buffer[0] == L'\0', "Explicit empty description not retained");
    Options folderOptions;
    const auto folderResult = alias_plugin::createAlias(path(working), path(root / L"folder"), folderOptions);
    success(folderResult);
    Link folder(root / L"folder.lnk");
    hr(folder.link->GetPath(buffer, 32767, nullptr, SLGP_RAWPATH), "Cannot read folder target");
    require(fs::path(buffer) == working, "Folder target mismatch");
    Options boundary;
    boundary.description = std::string(1023, 'd');
    boundary.argumentsText = std::string(1023, 'a');
    success(alias_plugin::createAlias(path(recorder), path(root / L"field-boundaries"), boundary));
    Link boundaryLink(root / L"field-boundaries.lnk");
    hr(boundaryLink.link->GetArguments(buffer, 32767), "Cannot read maximum arguments");
    require(utf8(buffer) == *boundary.argumentsText, "Maximum argument field was truncated");
    hr(boundaryLink.link->GetDescription(buffer, 32767), "Cannot read maximum description");
    require(utf8(buffer) == *boundary.description, "Maximum description was truncated");
}

void launchArguments(const fs::path& root, const fs::path& recorder) {
    const std::vector<std::wstring> expected = {
        L"plain", L"two words", L"", L"\u65e5\u672c\u8a9e \U0001f600", L"\"",
        L"embedded\"quote", L"trailing\\", L"space trailing\\", L"\\\\",
        L"slashes\\\\\"quote", L"\t", L"", L"final\\\\"
    };
    Options options;
    options.arguments.emplace();
    for (const auto& argument : expected) options.arguments->push_back(utf8(argument));
    success(alias_plugin::createAlias(path(recorder), path(root / L"argv"), options));
    launch(root / L"argv.lnk", root / L"argv.bin", expected);
    Options raw;
    raw.argumentsText = "\"verbatim spaced\" \"\" plain \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    success(alias_plugin::createAlias(path(recorder), path(root / L"raw"), raw));
    Link rawLink(root / L"raw.lnk");
    wchar_t buffer[32767]{};
    hr(rawLink.link->GetArguments(buffer, 32767), "Cannot read raw arguments");
    require(utf8(buffer) == *raw.argumentsText, "Raw arguments were transformed");
    launch(root / L"raw.lnk", root / L"raw.bin", {L"verbatim spaced", L"", L"plain", L"\u65e5\u672c\u8a9e"});
    Options none;
    success(alias_plugin::createAlias(path(recorder), path(root / L"none"), none));
    launch(root / L"none.lnk", root / L"none.bin", {});
    Options rawBoundary;
    rawBoundary.argumentsText = std::string(1023, 'a');
    success(alias_plugin::createAlias(path(recorder), path(root / L"raw-boundary"), rawBoundary));
    launch(root / L"raw-boundary.lnk", root / L"raw-boundary.bin", {std::wstring(1023, L'a')});
    Options quotedBoundary;
    quotedBoundary.arguments = std::vector<std::string>{std::string(1021, 'q')};
    success(alias_plugin::createAlias(path(recorder), path(root / L"quoted-boundary"), quotedBoundary));
    launch(root / L"quoted-boundary.lnk", root / L"quoted-boundary.bin", {std::wstring(1021, L'q')});
}

void failures(const fs::path& root, const fs::path& recorder) {
    const auto destination = root / L"must-not-exist.lnk";
    const auto reject = [&](const std::string& target, const Options& options, const char* code) {
        failure(alias_plugin::createAlias(target, path(destination), options), code);
        require(!fs::exists(destination), "Failed validation wrote a shortcut");
    };
    reject("", {}, "INVALID_PATH");
    reject("relative.exe", {}, "INVALID_PATH");
    reject(path(root / L"missing.exe"), {}, "TARGET_NOT_FOUND");
    reject(path(recorder) + std::string("\0suffix", 7), {}, "INVALID_PATH");
    reject(std::string("\xff", 1), {}, "INVALID_PATH");
    failure(alias_plugin::createAlias(path(recorder), "", {}), "INVALID_PATH");
    failure(alias_plugin::createAlias(path(recorder), "relative", {}), "INVALID_PATH");
    failure(alias_plugin::createAlias(path(recorder), path(destination) + std::string("\0x", 2), {}),
            "INVALID_PATH");
    failure(alias_plugin::createAlias(path(recorder), path(root / L"missing-parent" / L"link"), {}),
            "IO_ERROR");
    Options options;
    options.arguments.emplace();
    options.argumentsText = "";
    reject(path(recorder), options, "CONFLICTING_OPTIONS");
    options = {};
    options.iconIndex = 0;
    reject(path(recorder), options, "INVALID_OPTION");
    for (int show : {0, 2, 4, 5, 6, 8, 9, 10, -1, 999}) {
        options = {};
        options.showCommand = show;
        reject(path(recorder), options, "INVALID_OPTION");
    }
    options = {};
    options.description = std::string(1024, 'x');
    reject(path(recorder), options, "INVALID_OPTION");
    options = {};
    options.argumentsText = std::string(32767, 'x');
    reject(path(recorder), options, "INVALID_OPTION");
    options.argumentsText = std::string(32766, 'x');
    reject(path(recorder), options, "INVALID_OPTION");
    options.argumentsText = std::string(1024, 'x');
    reject(path(recorder), options, "INVALID_OPTION");
    options = {};
    options.arguments = std::vector<std::string>{std::string(32765, 'x')};
    reject(path(recorder), options, "INVALID_OPTION");
    options.arguments = std::vector<std::string>{std::string(1022, 'x')};
    reject(path(recorder), options, "INVALID_OPTION");
    options = {};
    options.arguments = std::vector<std::string>{std::string("a\0b", 3)};
    reject(path(recorder), options, "INVALID_PATH");
    options = {};
    options.argumentsText = std::string("a\0b", 3);
    reject(path(recorder), options, "INVALID_PATH");
    options = {};
    options.description = std::string("a\0b", 3);
    reject(path(recorder), options, "INVALID_PATH");
    options.description = std::string("\xff", 1);
    reject(path(recorder), options, "INVALID_PATH");
    options = {};
    options.workingDirectory = "";
    reject(path(recorder), options, "INVALID_OPTION");
    options.workingDirectory = path(recorder);
    reject(path(recorder), options, "INVALID_OPTION");
    options.workingDirectory = path(root / L"missing-working");
    reject(path(recorder), options, "INVALID_OPTION");
    options = {};
    options.iconPath = "";
    reject(path(recorder), options, "INVALID_OPTION");
    options.iconPath = "relative.ico";
    reject(path(recorder), options, "INVALID_OPTION");
    options.iconPath = path(root) + "\\" + std::string(260, 'x');
    reject(path(recorder), options, "INVALID_OPTION");
    reject(path(root) + "\\" + std::string(260, 'x'), {}, "INVALID_OPTION");
    failure(alias_plugin::createAlias(path(recorder), path(root) + "\\" + std::string(260, 'x'), {}),
            "INVALID_OPTION");
    require(!fs::exists(destination), "Failed validation wrote output");

    const auto unrelated = root / L"unrelated.lnk";
    writeFile(unrelated, "not a shortcut");
    const auto original = bytes(unrelated);
    failure(alias_plugin::createAlias(path(recorder), path(unrelated), {}), "DESTINATION_EXISTS");
    options = {};
    options.overwrite = true;
    failure(alias_plugin::createAlias(path(recorder), path(unrelated), options), "UNSAFE_OVERWRITE");
    require(bytes(unrelated) == original, "Unrelated destination changed");
    const auto directory = root / L"directory.lnk";
    fs::create_directory(directory);
    failure(alias_plugin::createAlias(path(recorder), path(directory), options), "UNSAFE_OVERWRITE");
    require(fs::is_directory(directory), "Directory destination changed");

    const auto existing = root / L"existing.lnk";
    Options originalOptions;
    originalOptions.description = "original description";
    success(alias_plugin::createAlias(path(recorder), path(existing), originalOptions));
    const auto originalLink = bytes(existing);
    failure(alias_plugin::createAlias(path(recorder), path(existing), {}), "DESTINATION_EXISTS");
    require(bytes(existing) == originalLink, "Default mode overwrote a real shortcut");
    Options excessive;
    excessive.overwrite = true;
    for (const std::size_t length : {std::size_t{1024}, std::size_t{32766}}) {
        excessive.argumentsText = std::string(length, 'x');
        failure(alias_plugin::createAlias(path(recorder), path(existing), excessive), "INVALID_OPTION");
        require(bytes(existing) == originalLink, "Overlong raw arguments changed the existing shortcut");
    }
    excessive.argumentsText.reset();
    excessive.arguments = std::vector<std::string>{std::string(1022, 'x')};
    failure(alias_plugin::createAlias(path(recorder), path(existing), excessive), "INVALID_OPTION");
    require(bytes(existing) == originalLink, "Overlong serialized arguments changed the existing shortcut");
    options.description = "replacement";
    require(SetFileAttributesW(existing.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
            "Cannot make shortcut read-only");
    const auto readonlyResult = alias_plugin::createAlias(path(recorder), path(existing), options);
    SetFileAttributesW(existing.c_str(), FILE_ATTRIBUTE_NORMAL);
    failure(readonlyResult, "IO_ERROR");
    require(readonlyResult.errors.front().nativeDomain == "Win32" &&
            readonlyResult.errors.front().nativeCode != 0, "Win32 failure lacks native metadata");
    require(bytes(existing) == originalLink, "Failed replacement did not preserve original bytes");
    const HANDLE lock = CreateFileW(existing.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(lock != INVALID_HANDLE_VALUE, "Cannot lock shortcut");
    const auto lockedResult = alias_plugin::createAlias(path(recorder), path(existing), options);
    CloseHandle(lock);
    failure(lockedResult, "IO_ERROR");
    require(bytes(existing) == originalLink, "Locked replacement changed original");
    success(alias_plugin::createAlias(path(recorder), path(existing), options));
    Link replaced(existing);
    wchar_t buffer[1024]{};
    hr(replaced.link->GetDescription(buffer, 1024), "Cannot read replacement");
    require(std::wstring(buffer) == L"replacement", "Replacement not published");

    const auto symlink = root / L"symlink.lnk";
    // Developer Mode or SeCreateSymbolicLinkPrivilege is required on some Windows hosts.
    if (CreateSymbolicLinkW(symlink.c_str(), existing.c_str(), 0x2) ||
        CreateSymbolicLinkW(symlink.c_str(), existing.c_str(), 0)) {
        const auto protectedBytes = bytes(existing);
        failure(alias_plugin::createAlias(path(recorder), path(symlink), options), "UNSAFE_OVERWRITE");
        require(bytes(existing) == protectedBytes, "Symlink destination changed referent");
        reject(path(symlink), {}, "UNSUPPORTED_TARGET");
        fs::remove(symlink);
    } else {
        std::cout << "SKIP symlink fixture: Windows privilege/developer mode unavailable\n";
    }
    for (const auto& entry : fs::directory_iterator(root))
        require(entry.path().filename().wstring().find(L".create-alias-") != 0,
                "Staging directory leaked after success/failure");
}

void concurrentCreation(const fs::path& root, const fs::path& recorder) {
    std::atomic<int> winners{0};
    std::atomic<int> losers{0};
    std::atomic<int> unexpected{0};
    std::vector<std::thread> threads;
    try {
        for (int i = 0; i < 12; ++i) {
            threads.emplace_back([&] {
                try {
                    const auto result = alias_plugin::createAlias(path(recorder), path(root / L"race.lnk"), {});
                    if (result.ok) ++winners;
                    else if (!result.errors.empty() && result.errors.front().code == "DESTINATION_EXISTS") ++losers;
                    else ++unexpected;
                } catch (...) { ++unexpected; }
            });
        }
    } catch (...) {
        for (auto& thread : threads) thread.join();
        throw;
    }
    for (auto& thread : threads) thread.join();
    require(winners == 1 && losers == 11 && unexpected == 0,
            "Concurrent no-clobber creation did not produce exactly one winner");
    Link link(root / L"race.lnk");
    wchar_t buffer[MAX_PATH]{};
    hr(link.link->GetPath(buffer, MAX_PATH, nullptr, SLGP_RAWPATH), "Cannot read concurrent winner");
    require(fs::path(buffer) == recorder, "Concurrent winner is invalid");
    for (const auto& entry : fs::directory_iterator(root))
        require(entry.path().filename().wstring().find(L".create-alias-") != 0,
                "Concurrent creation leaked staging directory");
}

void apartmentTests(const fs::path& root, const fs::path& recorder) {
    // A fresh worker owns no COM initialization; each successful call must balance it.
    std::exception_ptr failure;
    const auto worker = [](void* context) -> DWORD {
        auto* data = static_cast<std::pair<fs::path, std::exception_ptr>*>(context);
        try {
            const auto recorder = data->first;
            const auto base = recorder.parent_path();
            success(alias_plugin::createAlias(path(recorder), path(base / L"fresh-apartment"), {}));
            const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            require(init == S_OK, "Backend leaked its STA COM initialization");
            success(alias_plugin::createAlias(path(recorder), path(base / L"mta-apartment"), {}));
            const HRESULT stillMta = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            require(stillMta == S_FALSE, "Backend uninitialized a caller-owned MTA");
            CoUninitialize();
            CoUninitialize();
            const HRESULT sta = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            require(sta == S_OK, "MTA COM balance incorrect");
            success(alias_plugin::createAlias(path(recorder), path(base / L"sta-apartment"), {}));
            CoUninitialize();
            const HRESULT balanced = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            require(balanced == S_OK, "Backend did not balance S_FALSE initialization");
            CoUninitialize();
        } catch (...) { data->second = std::current_exception(); }
        return 0;
    };
    // std::thread avoids a calling-convention cast for the Win32 thread callback.
    (void)root;
    std::pair<fs::path, std::exception_ptr> data{recorder, failure};
    std::thread thread([&] { worker(&data); });
    thread.join();
    if (data.second) std::rethrow_exception(data.second);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "Usage: native-tests <absolute-path-to-argv-recorder.exe>");
        const auto init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        hr(init, "Cannot initialize test COM apartment");
        struct Balance { ~Balance() { CoUninitialize(); } } balance;
        Root root;
        const fs::path source = fs::absolute(argv[1]);
        const fs::path recorder = root.value / L"recorder space \u65e5\u672c\u8a9e.exe";
        fs::copy_file(source, recorder);
        readback(root.value, recorder);
        launchArguments(root.value, recorder);
        failures(root.value, recorder);
        concurrentCreation(root.value, recorder);
        apartmentTests(root.value, recorder);
        std::cout << "PASS Windows native shortcut tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
