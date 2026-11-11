#include "4DPluginAPI.h"
#include "native.hpp"
#include "unicode.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace alias_plugin;
namespace fs = std::filesystem;

struct Failure {
    Error error;
};

[[noreturn]] void fail(const std::string& code, const std::string& message,
                       const std::string& option = {}) {
    throw Failure{{code, message, option}};
}

void checkSdk(const char* operation) {
    auto code = PA_GetLastError();
    if (code != eER_NoErr)
        throw Failure{{"HOST_ERROR", operation, {}, "4D", code}};
}

class Value {
public:
    PA_Variable value{};
    bool owned = true;
    Value() : value(PA_CreateVariable(eVK_Undefined)) {}
    explicit Value(PA_Variable v) : value(v) {}
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    Value(Value&& other) noexcept : value(other.value), owned(other.owned) {
        other.owned = false;
    }
    ~Value() { if (owned) PA_ClearVariable(&value); }
    PA_VariableKind kind() const { return PA_GetVariableKind(value); }
};

class String {
public:
    PA_Unistring value{};
    explicit String(const std::string& text) {
        auto utf16 = fromUtf8(text);
        std::vector<PA_Unichar> data(utf16.begin(), utf16.end());
        data.push_back(0);
        value = PA_CreateUnistring(data.data());
        checkSdk("Creating a Unicode string");
    }
    explicit String(PA_Unistring text) : value(text) {}
    ~String() { PA_DisposeUnistring(&value); }
    String(const String&) = delete;
};

std::string readString(PA_Unistring* text) {
    const auto length = PA_GetUnistringLength(text);
    auto chars = PA_GetUnistring(text);
    if (length < 0 || (length && !chars)) fail("HOST_ERROR", "Invalid host string");
    std::u16string utf16;
    utf16.reserve(static_cast<std::size_t>(length));
    for (PA_long32 i = 0; i < length; ++i) utf16.push_back(chars[i]);
    try { return toUtf8(utf16); }
    catch (const std::invalid_argument& e) { fail("INVALID_TEXT", e.what()); }
}

std::string textValue(const Value& value, const std::string& option) {
    if (value.kind() != eVK_Unistring && value.kind() != eVK_Text)
        fail("INVALID_OPTION_TYPE", "Expected Text", option);
    String text(PA_GetStringVariable(value.value));
    checkSdk("Reading a string variable");
    std::string result;
    try { result = readString(&text.value); }
    catch (Failure& error) {
        if (error.error.option.empty()) error.error.option = option;
        throw;
    }
    if (result.find('\0') != std::string::npos)
        fail("INVALID_OPTION", "Text must not contain NUL", option);
    return result;
}

Value stringValue(const std::string& text) {
    String str(text);
    Value result;
    PA_SetStringVariable(&result.value, &str.value);
    // The descriptor now owns the allocation; the String destructor must not free it.
    str.value = {};
    return result;
}

Value objectValue() {
    Value result;
    PA_SetObjectVariable(&result.value, PA_CreateObject());
    checkSdk("Creating a result object");
    if (!PA_GetObjectVariable(result.value)) fail("HOST_ERROR", "Cannot allocate result object");
    return result;
}

Value collectionValue() {
    Value result;
    PA_SetCollectionVariable(&result.value, PA_CreateCollection());
    checkSdk("Creating a collection");
    if (!PA_GetCollectionVariable(result.value)) fail("HOST_ERROR", "Cannot allocate collection");
    return result;
}

void put(Value& object, const std::string& key, const Value& value) {
    String name(key);
    PA_SetObjectProperty(PA_GetObjectVariable(object.value), &name.value, value.value);
    checkSdk("Setting a result property");
}

void putText(Value& object, const std::string& key, const std::string& text) {
    auto value = stringValue(text);
    put(object, key, value);
}

Value keys(PA_ObjectRef object) {
    PA_Variable borrowed{};
    PA_SetObjectVariable(&borrowed, object);
    Value param;
    PA_CopyVariable(&borrowed, &param.value);
    checkSdk("Retaining options for OB Keys");
    Value result(PA_ExecuteCommandByID(1719, &param.value, 1));
    checkSdk("Enumerating option names");
    if (result.kind() != eVK_Collection) fail("HOST_ERROR", "OB Keys did not return a collection");
    return result;
}

struct Parsed {
    Options options;
    std::string pathFormat = "system";
};

Parsed parseOptions(PA_ObjectRef object) {
    Parsed parsed;
    if (!object) return parsed;
    auto names = keys(object);
    auto collection = PA_GetCollectionVariable(names.value);
    const auto count = PA_GetCollectionLength(collection);
    checkSdk("Reading option count");
    for (PA_long32 i = 0; i < count; ++i) {
        Value nameValue(PA_GetCollectionElement(collection, i));
        checkSdk("Reading option name");
        auto name = textValue(nameValue, {});
#ifdef __APPLE__
        if (name == "arguments" || name == "argumentsText" || name == "workingDirectory" ||
            name == "description" || name == "iconPath" || name == "iconIndex" ||
            name == "showCommand")
            fail("UNSUPPORTED_OPTION", "Launch options are Windows-only", name);
#endif
        String key(name);
        Value value(PA_GetObjectProperty(object, &key.value));
        checkSdk("Reading option value");
        if (name == "overwrite") {
            if (value.kind() != eVK_Boolean)
                fail("INVALID_OPTION_TYPE", "Expected Boolean", name);
            parsed.options.overwrite = PA_GetBooleanVariable(value.value) != 0;
        } else if (name == "pathFormat") {
            parsed.pathFormat = textValue(value, name);
            if (parsed.pathFormat != "system" && parsed.pathFormat != "posix")
                fail("INVALID_OPTION", "Expected system or posix", name);
#ifdef _WIN32
            if (parsed.pathFormat == "posix")
                fail("UNSUPPORTED_OPTION", "POSIX mode is supported only on macOS", name);
#endif
        } else if (name == "arguments") {
            if (value.kind() != eVK_Collection || !PA_GetCollectionVariable(value.value))
                fail("INVALID_OPTION_TYPE", "Expected a collection of Text", name);
            parsed.options.arguments.emplace();
            auto args = PA_GetCollectionVariable(value.value);
            const auto length = PA_GetCollectionLength(args);
            checkSdk("Reading argument count");
            for (PA_long32 j = 0; j < length; ++j) {
                Value arg(PA_GetCollectionElement(args, j));
                checkSdk("Reading argument");
                parsed.options.arguments->push_back(textValue(arg, name));
            }
        } else if (name == "argumentsText") parsed.options.argumentsText = textValue(value, name);
        else if (name == "workingDirectory") parsed.options.workingDirectory = textValue(value, name);
        else if (name == "description") parsed.options.description = textValue(value, name);
        else if (name == "iconPath") parsed.options.iconPath = textValue(value, name);
        else if (name == "iconIndex" || name == "showCommand") {
            double number;
            if (value.kind() == eVK_Real) number = PA_GetRealVariable(value.value);
            else if (value.kind() == eVK_Longint)
                number = PA_GetLongintVariable(value.value);
            else fail("INVALID_OPTION_TYPE", "Expected an integer number", name);
            if (!std::isfinite(number) || std::trunc(number) != number ||
                number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
                fail("INVALID_OPTION", "Number is outside the integer range", name);
            if (name == "iconIndex") parsed.options.iconIndex = static_cast<int>(number);
            else parsed.options.showCommand = static_cast<int>(number);
        } else fail("UNKNOWN_OPTION", "Unknown option", name);
    }
#ifndef __APPLE__
    if (parsed.options.arguments && parsed.options.argumentsText)
        fail("CONFLICTING_OPTIONS", "arguments and argumentsText are mutually exclusive", "argumentsText");
    if (parsed.options.iconIndex && !parsed.options.iconPath)
        fail("INVALID_OPTION", "iconIndex requires iconPath", "iconIndex");
    if (parsed.options.showCommand && *parsed.options.showCommand != 1 &&
        *parsed.options.showCommand != 3 && *parsed.options.showCommand != 7)
        fail("INVALID_OPTION", "showCommand must be 1, 3 or 7", "showCommand");
#endif
    return parsed;
}

std::string pathUtf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path nativePath(const std::string& path) {
    return fs::path(std::u8string(path.begin(), path.end()));
}

std::string convertPath(const std::string& text, PA_long32 command) {
    auto param = stringValue(text);
    Value converted(PA_ExecuteCommandByID(command, &param.value, 1));
    checkSdk("Converting a path");
    auto result = textValue(converted, {});
    if (result.empty()) fail("PATH_CONVERSION_FAILED", "4D returned an empty path");
    return result;
}

class Paths {
    fs::path cwd;
    std::string format;
public:
    explicit Paths(std::string pathFormat) : format(std::move(pathFormat)) {
        std::error_code ec;
        cwd = fs::current_path(ec);
        if (ec) throw Failure{{"CWD_ERROR", ec.message(), {}, ec.category().name(), ec.value()}};
        if (!cwd.is_absolute()) fail("CWD_ERROR", "Host cwd is not absolute");
    }
    std::string input(const std::string& path, const std::string& field) const {
        if (path.empty()) fail(field == "targetPath" ? "INVALID_TARGET" : "INVALID_PATH",
                               "Path must not be empty", field);
        if (path.find('\0') != std::string::npos) fail("INVALID_PATH", "Path contains NUL", field);
#ifdef __APPLE__
        if (format == "system") {
            if (path.front() == '/') fail("INVALID_PATH_FORMAT", "POSIX paths require pathFormat posix", field);
            auto absolute = path;
            if (path.front() == ':' || path.find(':') == std::string::npos) {
                auto base = convertPath(pathUtf8(cwd), 1107);
                if (base.back() != ':') base += ':';
                absolute = base + (path.front() == ':' ? path.substr(1) : path);
            }
            auto converted = nativePath(convertPath(absolute, 1106));
            if (!converted.is_absolute()) fail("PATH_CONVERSION_FAILED", "4D did not return an absolute path", field);
            return pathUtf8(converted.lexically_normal());
        }
#else
        auto windowsPath = nativePath(path);
        if (windowsPath.has_root_name() != windowsPath.has_root_directory())
            fail("INVALID_PATH_FORMAT", "Drive-relative and root-relative Windows paths are ambiguous", field);
#endif
        auto native = nativePath(path);
        return pathUtf8((native.is_absolute() ? native : cwd / native).lexically_normal());
    }
    std::string output(const std::string& path) const {
#ifdef __APPLE__
        if (format == "system") return convertPath(path, 1107);
#endif
        return path;
    }
};

Value makeResult(const Result& result) {
    auto object = objectValue();
    Value ok;
    PA_SetBooleanVariable(&ok.value, result.ok);
    put(object, "OK", ok);
    if (result.ok) {
        putText(object, "targetPath", result.targetPath);
        putText(object, "aliasPath", result.aliasPath);
        putText(object, "kind", result.kind);
    }
    auto errors = collectionValue();
    auto collection = PA_GetCollectionVariable(errors.value);
    PA_long32 index = 0;
    for (const auto& error : result.errors) {
        auto item = objectValue();
        putText(item, "code", error.code);
        putText(item, "message", error.message);
        if (!error.option.empty()) putText(item, "option", error.option);
        if (!error.nativeDomain.empty()) {
            putText(item, "nativeDomain", error.nativeDomain);
            Value code;
            PA_SetRealVariable(&code.value, static_cast<double>(error.nativeCode));
            put(item, "nativeCode", code);
        }
        PA_SetCollectionElement(collection, index++, item.value);
        checkSdk("Adding a result error");
    }
    put(object, "errors", errors);
    return object;
}

Result invoke(PA_PluginParameters params) {
    auto parsed = parseOptions(PA_GetObjectParameter(params, 3));
    auto parameter = [params](short index, const std::string& field) {
        try { return readString(PA_GetStringParameter(params, index)); }
        catch (Failure& error) {
            if (error.error.option.empty()) error.error.option = field;
            throw;
        }
    };
    const auto target = parameter(1, "targetPath");
    const auto alias = parameter(2, "aliasPath");
    if (target.empty()) fail("INVALID_TARGET", "Target must not be empty", "targetPath");
    Paths paths(parsed.pathFormat);
    auto nativeTarget = paths.input(target, "targetPath");
    auto nativeAlias = paths.input(alias, "aliasPath");
    if (nativeTarget == nativeAlias)
        fail("INVALID_PATH", "Target and destination must be different", "aliasPath");
    if (parsed.options.workingDirectory)
        parsed.options.workingDirectory = paths.input(*parsed.options.workingDirectory, "workingDirectory");
    if (parsed.options.iconPath)
        parsed.options.iconPath = paths.input(*parsed.options.iconPath, "iconPath");
    // Check return conversion before publication, not after a file has been written.
    auto returnedTarget = paths.output(nativeTarget);
#ifdef _WIN32
    auto suffix = nativeAlias.size() >= 4 ? nativeAlias.substr(nativeAlias.size() - 4) : "";
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (suffix != ".lnk") nativeAlias += ".lnk";
#endif
    auto returnedAlias = paths.output(nativeAlias);
    auto result = createAlias(nativeTarget, nativeAlias, parsed.options);
    if (result.ok) {
        result.targetPath = std::move(returnedTarget);
        result.aliasPath = std::move(returnedAlias);
    }
    return result;
}
}

void PluginMain(PA_long32 selector, PA_PluginParameters params) {
    if (selector != 1) return;
    try {
        Result hostFailure;
        hostFailure.errors.push_back({"HOST_ERROR", "4D could not construct the detailed result"});
        auto emergency = makeResult(hostFailure);
        Result result;
        try { result = invoke(params); }
        catch (const Failure& error) { result.errors.push_back(error.error); }
        catch (const std::exception& error) { result.errors.push_back({"INTERNAL_ERROR", error.what()}); }
        try {
            auto object = makeResult(result);
            PA_ReturnObject(params, PA_GetObjectVariable(object.value));
            object.owned = false;
        } catch (const Failure& error) {
            std::fprintf(stderr, "CREATE_ALIAS_EX: result construction failed: %s (%s, %lld)\n",
                         error.error.message.c_str(), error.error.nativeDomain.c_str(),
                         static_cast<long long>(error.error.nativeCode));
            PA_ReturnObject(params, PA_GetObjectVariable(emergency.value));
            emergency.owned = false;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "CREATE_ALIAS_EX: result construction failed: %s\n", error.what());
            PA_ReturnObject(params, PA_GetObjectVariable(emergency.value));
            emergency.owned = false;
        }
    } catch (const Failure& error) {
        std::fprintf(stderr, "CREATE_ALIAS_EX: cannot reserve error result: %s (%s, %lld)\n",
                     error.error.message.c_str(), error.error.nativeDomain.c_str(),
                     static_cast<long long>(error.error.nativeCode));
        PA_ReturnObject(params, nullptr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CREATE_ALIAS_EX: cannot reserve error result: %s\n", error.what());
        PA_ReturnObject(params, nullptr);
    }
}
