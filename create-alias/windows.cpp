#include "native.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <propsys.h>

#include <climits>
#include <cwctype>
#include <utility>

namespace alias_plugin {
namespace {

// PKEY_Link_Arguments (System.Link.Arguments), defined locally to avoid any
// property-system helper imports or external property-key definitions.
constexpr PROPERTYKEY linkArgumentsKey = {
    {0x436f2667, 0x14e2, 0x4feb, {0xb3, 0x0a, 0x14, 0x6c, 0x53, 0xb5, 0xb6, 0x74}}, 100
};

struct PropertyValue {
    PROPVARIANT value{};
    ~PropertyValue() { PropVariantClear(&value); }
};

template<class T> struct ComPtr {
    T* value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
    T* operator->() const { return value; }
};

struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (status == S_OK || status == S_FALSE) CoUninitialize(); }
};

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

void error(Result& result, const char* code, const char* message,
           const std::string& option = {}, std::int64_t nativeCode = 0,
           const char* domain = "Win32") {
    result.errors.push_back({code, message, option, nativeCode ? domain : "",
                             static_cast<std::int64_t>(nativeCode)});
}

bool wide(Result& result, const std::string& input, std::wstring& output,
          const std::string& option) {
    if (input.find('\0') != std::string::npos || input.size() > INT_MAX) {
        error(result, "INVALID_PATH", "Embedded NUL or excessive input length.", option);
        return false;
    }
    if (input.empty()) { output.clear(); return true; }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0);
    if (!size) {
        error(result, "INVALID_PATH", "Input is not valid UTF-8.", option, GetLastError());
        return false;
    }
    output.resize(size);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                        static_cast<int>(input.size()), output.data(), size);
    return true;
}

bool absolute(const std::wstring& path) {
    // Reject device namespaces and alternate data streams, not just relative paths.
    if (path.find(L'/') != std::wstring::npos) return false;
    if (path.size() >= 3 && iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\')
        return path.find(L':', 2) == std::wstring::npos;
    if (path.size() > 4 && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] != L'?' && path[2] != L'.') {
        const auto server = path.find(L'\\', 2);
        return server != std::wstring::npos && server > 2 && server + 1 < path.size() &&
               path.find(L':') == std::wstring::npos;
    }
    return false;
}

bool hasSuffix(const std::wstring& path) {
    const auto n = path.size();
    return n >= 4 && path[n-4] == L'.' && towlower(path[n-3]) == L'l' &&
           towlower(path[n-2]) == L'n' && towlower(path[n-1]) == L'k';
}

bool bounded(Result& result, const std::wstring& text, std::size_t maximum,
             const std::string& option) {
    if (text.size() <= maximum) return true;
    error(result, "INVALID_OPTION", "Value exceeds the supported Shell Link length.", option);
    return false;
}

bool checked(Result& result, HRESULT status, const char* operation,
             const std::string& option = {}) {
    if (SUCCEEDED(status)) return true;
    error(result, "IO_ERROR", operation, option, static_cast<std::int64_t>(status), "HRESULT");
    return false;
}

bool identity(HANDLE file, BY_HANDLE_FILE_INFORMATION& info) {
    return GetFileInformationByHandle(file, &info) != FALSE;
}

bool sameFile(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

bool verifyPersistedArguments(Result& result, const std::wstring& staged,
                              const std::wstring& expected, const char* option) {
    ComPtr<IShellLinkW> savedLink;
    ComPtr<IPersistFile> savedFile;
    if (!checked(result, CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                 IID_IShellLinkW, reinterpret_cast<void**>(&savedLink.value)),
                 "Cannot create persisted shortcut verifier.") ||
        !checked(result, savedLink->QueryInterface(IID_IPersistFile,
                 reinterpret_cast<void**>(&savedFile.value)), "Cannot obtain persisted shortcut verifier.") ||
        !checked(result, savedFile->Load(staged.c_str(), STGM_READ),
                 "Cannot reload staged shortcut.")) return false;
    ComPtr<IPropertyStore> properties;
    PropertyValue arguments;
    if (!checked(result, savedLink->QueryInterface(IID_IPropertyStore,
                 reinterpret_cast<void**>(&properties.value)), "Cannot obtain persisted shortcut properties.", option) ||
        !checked(result, properties->GetValue(linkArgumentsKey, &arguments.value),
                 "Cannot read persisted shortcut arguments property.", option)) return false;
    const bool validType = arguments.value.vt == VT_LPWSTR || arguments.value.vt == VT_EMPTY;
    const std::wstring actual = arguments.value.vt == VT_LPWSTR && arguments.value.pwszVal ?
                                arguments.value.pwszVal : L"";
    if (validType && expected == actual) return true;
    // Microsoft recommends IPropertyStore for full arguments. GetArguments is
    // diagnostic only: a short legacy read must not reject an intact property.
    std::vector<wchar_t> legacy(32767);
    const HRESULT legacyStatus = savedLink->GetArguments(legacy.data(), static_cast<int>(legacy.size()));
    const std::string message = "Persisted Shell Link arguments mismatch: expected UTF-16 length=" +
        std::to_string(expected.size()) + ", property length=" + std::to_string(actual.size()) +
        ", property type=" + std::to_string(arguments.value.vt) +
        ", GetArguments length=" + std::to_string(std::wstring(legacy.data()).size()) +
        ", GetArguments HRESULT=" + std::to_string(static_cast<std::int64_t>(legacyStatus)) + ".";
    error(result, "IO_ERROR", message.c_str(),
          option, static_cast<std::int64_t>(E_FAIL), "HRESULT");
    return false;
}

}

Result createAlias(const std::string& target, const std::string& destination,
                   const Options& options) {
    Result result;
    result.targetPath = target;
    result.aliasPath = destination;
    result.kind = "shortcut";
    std::wstring targetW, destinationW, argumentsW, workingW, descriptionW, iconW;
    if (!wide(result, target, targetW, "targetPath") ||
        !wide(result, destination, destinationW, "aliasPath")) return result;
    if (targetW.empty() || !absolute(targetW)) {
        error(result, "INVALID_PATH", "Target must be an absolute native Windows path.", "targetPath");
        return result;
    }
    if (destinationW.empty() || !absolute(destinationW) || destinationW.back() == L'\\' ||
        destinationW.back() == L'.' || destinationW.back() == L' ') {
        error(result, "INVALID_PATH", "Destination must be an absolute Windows file path.", "aliasPath");
        return result;
    }
    if (!hasSuffix(destinationW)) { destinationW += L".lnk"; result.aliasPath += ".lnk"; }
    if (!bounded(result, targetW, MAX_PATH-1, "targetPath") ||
        !bounded(result, destinationW, MAX_PATH-1, "aliasPath")) return result;
    if (options.arguments && options.argumentsText) {
        error(result, "CONFLICTING_OPTIONS", "arguments and argumentsText are mutually exclusive.", "arguments");
        return result;
    }
    if (options.iconIndex && !options.iconPath) {
        error(result, "INVALID_OPTION", "iconIndex requires iconPath.", "iconIndex");
        return result;
    }
    if (options.showCommand && *options.showCommand != 1 &&
        *options.showCommand != 3 && *options.showCommand != 7) {
        error(result, "INVALID_OPTION", "showCommand must be 1, 3 or 7.", "showCommand");
        return result;
    }
    if (options.arguments) {
        for (const auto& argument : *options.arguments) {
            std::wstring unused;
            if (!wide(result, argument, unused, "arguments")) return result;
        }
    }
    if (!wide(result, options.arguments ? quoteArguments(*options.arguments) :
              options.argumentsText.value_or(""), argumentsW,
              options.arguments ? "arguments" : "argumentsText") ||
        // Supported bound, independently verified after persistence through
        // IPropertyStore rather than the legacy GetArguments accessor.
        !bounded(result, argumentsW, INFOTIPSIZE-1, options.arguments ? "arguments" : "argumentsText"))
        return result;
    const auto pathOption = [&](const std::optional<std::string>& value,
                                std::wstring& output, const char* name) {
        if (!value) return true;
        if (!wide(result, *value, output, name)) return false;
        if (output.empty() || !absolute(output)) {
            error(result, "INVALID_OPTION", "Option requires an absolute nonempty Windows path.", name);
            return false;
        }
        return bounded(result, output, MAX_PATH-1, name);
    };
    if (!pathOption(options.workingDirectory, workingW, "workingDirectory") ||
        !pathOption(options.iconPath, iconW, "iconPath")) return result;
    if (options.description &&
        (!wide(result, *options.description, descriptionW, "description") ||
         !bounded(result, descriptionW, INFOTIPSIZE-1, "description"))) return result;
    const DWORD targetAttributes = GetFileAttributesW(targetW.c_str());
    if (targetAttributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD status = GetLastError();
        error(result, status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND ?
              "TARGET_NOT_FOUND" : "IO_ERROR", "Cannot inspect target.", "targetPath", status);
        return result;
    }
    if (targetAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) {
        error(result, "UNSUPPORTED_TARGET", "Target must be a regular file or folder.", "targetPath");
        return result;
    }
    if (options.workingDirectory) {
        const DWORD attributes = GetFileAttributesW(workingW.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            error(result, "INVALID_OPTION", "workingDirectory must name an existing folder.", "workingDirectory");
            return result;
        }
    }
    Apartment apartment;
    if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE) {
        checked(result, apartment.status, "Cannot initialize COM.");
        return result;
    }
    Handle existing;
    BY_HANDLE_FILE_INFORMATION original{};
    const DWORD destinationAttributes = GetFileAttributesW(destinationW.c_str());
    const DWORD destinationStatus = destinationAttributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
    if (destinationAttributes != INVALID_FILE_ATTRIBUTES) {
        if (!options.overwrite) {
            error(result, "DESTINATION_EXISTS", "Destination already exists.", "aliasPath", ERROR_FILE_EXISTS);
            return result;
        }
        if (destinationAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                                     FILE_ATTRIBUTE_DEVICE)) {
            error(result, "UNSAFE_OVERWRITE", "Only an existing genuine shortcut can be overwritten.", "aliasPath");
            return result;
        }
        // Deny in-place writers while validating and replacing the original.
        existing.value = CreateFileW(destinationW.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (existing.value == INVALID_HANDLE_VALUE || !identity(existing.value, original)) {
            error(result, "IO_ERROR", "Cannot lock existing shortcut.", "aliasPath", GetLastError());
            return result;
        }
        if (original.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            error(result, "UNSAFE_OVERWRITE", "Destination changed during validation.", "aliasPath");
            return result;
        }
        ComPtr<IShellLinkW> oldLink;
        ComPtr<IPersistFile> oldFile;
        if (!checked(result, CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IShellLinkW, reinterpret_cast<void**>(&oldLink.value)), "Cannot create shortcut reader.") ||
            !checked(result, oldLink->QueryInterface(IID_IPersistFile,
                     reinterpret_cast<void**>(&oldFile.value)), "Cannot obtain shortcut reader.")) return result;
        wchar_t oldPath[MAX_PATH]{};
        if (FAILED(oldFile->Load(destinationW.c_str(), STGM_READ)) ||
            FAILED(oldLink->GetPath(oldPath, MAX_PATH, nullptr, SLGP_RAWPATH)) || !oldPath[0]) {
            error(result, "UNSAFE_OVERWRITE", "Existing file is not a readable filesystem shortcut.", "aliasPath");
            return result;
        }
    } else if (destinationStatus != ERROR_FILE_NOT_FOUND && destinationStatus != ERROR_PATH_NOT_FOUND) {
        error(result, "IO_ERROR", "Cannot inspect destination.", "aliasPath", destinationStatus);
        return result;
    }
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    if (!checked(result, CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                 IID_IShellLinkW, reinterpret_cast<void**>(&link.value)), "Cannot create Shell Link.") ||
        !checked(result, link->QueryInterface(IID_IPersistFile,
                 reinterpret_cast<void**>(&persist.value)), "Cannot obtain shortcut persistence.") ||
        !checked(result, link->SetPath(targetW.c_str()), "Cannot set target.", "targetPath") ||
        !checked(result, link->SetArguments(argumentsW.c_str()), "Cannot set arguments.", "arguments") ||
        (options.workingDirectory && !checked(result, link->SetWorkingDirectory(workingW.c_str()),
                                             "Cannot set working directory.", "workingDirectory")) ||
        (options.description && !checked(result, link->SetDescription(descriptionW.c_str()),
                                        "Cannot set description.", "description")) ||
        (options.iconPath && !checked(result, link->SetIconLocation(iconW.c_str(), options.iconIndex.value_or(0)),
                                     "Cannot set icon.", "iconPath")) ||
        (options.showCommand && !checked(result, link->SetShowCmd(*options.showCommand),
                                        "Cannot set show command.", "showCommand"))) return result;
    const auto parent = destinationW.substr(0, destinationW.find_last_of(L'\\') + 1);
    std::wstring staging;
    std::string stagingPath;
    for (int attempt = 0; attempt < 16; ++attempt) {
        GUID id{};
        if (!checked(result, CoCreateGuid(&id), "Cannot name staging directory.")) return result;
        wchar_t text[40]{};
        StringFromGUID2(id, text, 40);
        staging = parent + L".create-alias-" + text;
        stagingPath = result.aliasPath.substr(0, result.aliasPath.find_last_of('\\') + 1) + ".create-alias-";
        for (const wchar_t* character = text; *character; ++character)
            stagingPath += static_cast<char>(*character);
        if (CreateDirectoryW(staging.c_str(), nullptr)) break;
        const DWORD status = GetLastError();
        if (status != ERROR_ALREADY_EXISTS || attempt == 15) {
            error(result, "IO_ERROR", "Cannot create exclusive sibling staging directory.", "aliasPath", status);
            return result;
        }
    }
    const std::wstring staged = staging + L"\\shortcut.lnk";
    const std::wstring backup = staging + L"\\original.lnk";
    bool published = false;
    bool keepBackup = false;
    if (checked(result, persist->Save(staged.c_str(), TRUE), "Cannot save staged shortcut.")) {
        const bool persisted = verifyPersistedArguments(result, staged, argumentsW,
                                                        options.arguments ? "arguments" : "argumentsText");
        if (persisted && existing.value != INVALID_HANDLE_VALUE) {
            Handle current;
            current.value = CreateFileW(destinationW.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            BY_HANDLE_FILE_INFORMATION latest{};
            if (current.value == INVALID_HANDLE_VALUE || !identity(current.value, latest) ||
                !sameFile(original, latest) || (latest.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                error(result, "UNSAFE_OVERWRITE", "Destination identity changed before replacement.", "aliasPath");
            } else {
                // ReplaceFile has no compare-and-swap file-ID API. A hostile namespace
                // rename in the final check/call window cannot be excluded here.
                published = ReplaceFileW(destinationW.c_str(), staged.c_str(), backup.c_str(), 0, nullptr, nullptr) != FALSE;
                if (!published) {
                    const DWORD status = GetLastError();
                    error(result, "IO_ERROR", "Cannot replace existing shortcut.", "aliasPath", status);
                    if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        if (GetFileAttributesW(destinationW.c_str()) == INVALID_FILE_ATTRIBUTES &&
                            MoveFileExW(backup.c_str(), destinationW.c_str(), 0)) {
                            // Restored the original after a partially completed ReplaceFile.
                        } else {
                            keepBackup = true;
                            const std::string message = "Replacement failed; original backup retained at " +
                                                        stagingPath + "\\original.lnk";
                            error(result, "CLEANUP_FAILED", message.c_str(), "aliasPath", status);
                        }
                    }
                }
            }
            // ReplaceFile moves the original (and our open handle) to the backup name.
            // Close it before deleting the backup so directory cleanup is not delete-pending.
            if (existing.value != INVALID_HANDLE_VALUE) {
                if (!CloseHandle(existing.value))
                    error(result, "CLEANUP_FAILED", "Cannot close original shortcut handle.", "aliasPath", GetLastError());
                existing.value = INVALID_HANDLE_VALUE;
            }
            if (!keepBackup && !DeleteFileW(backup.c_str())) {
                const DWORD status = GetLastError();
                if (status != ERROR_FILE_NOT_FOUND)
                    error(result, "CLEANUP_FAILED", "Cannot remove original shortcut backup.", "aliasPath", status);
            }
        } else if (persisted) {
            published = MoveFileExW(staged.c_str(), destinationW.c_str(), 0) != FALSE;
            if (!published) {
                const DWORD status = GetLastError();
                error(result, status == ERROR_ALREADY_EXISTS || status == ERROR_FILE_EXISTS ?
                      "DESTINATION_EXISTS" : "IO_ERROR", "Cannot publish shortcut without replacing files.",
                      "aliasPath", status);
            }
        }
    }
    if (!published && !DeleteFileW(staged.c_str())) {
        const DWORD status = GetLastError();
        if (status != ERROR_FILE_NOT_FOUND)
            error(result, "CLEANUP_FAILED", "Cannot remove staged shortcut.", "aliasPath", status);
    }
    if (!keepBackup && !RemoveDirectoryW(staging.c_str()))
        error(result, "CLEANUP_FAILED", "Cannot remove staging directory.", "aliasPath", GetLastError());
    result.ok = published && result.errors.empty();
    return result;
}

}
