#include "native.hpp"

#import <Foundation/Foundation.h>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace alias_plugin {
namespace {

std::string utf8(NSString *value) {
    return value ? std::string(value.UTF8String ?: "") : std::string();
}

void error(Result& result, const char *code, const std::string& message,
           NSError *native = nil, const char *option = "") {
    result.errors.push_back({code, message, option,
                             native ? utf8(native.domain) : "",
                             native ? static_cast<std::int64_t>(native.code) : 0});
}

void posixError(Result& result, const char *code, const std::string& operation,
                int number = errno) {
    NSError *native = [NSError errorWithDomain:NSPOSIXErrorDomain code:number userInfo:nil];
    error(result, code, operation + ": " + utf8(native.localizedDescription), native);
}

NSString *pathString(const std::string& path) {
    if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos)
        return nil;
    return [[NSString alloc] initWithBytes:path.data()
                                   length:path.size()
                                 encoding:NSUTF8StringEncoding];
}

bool sameIdentity(const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

bool sameVersion(const struct stat& a, const struct stat& b) {
    return sameIdentity(a, b) && a.st_mode == b.st_mode && a.st_size == b.st_size &&
           a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
           a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
           a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec &&
           a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}

bool sameContentVersion(const struct stat& a, const struct stat& b) {
    // Exchanging names may change ctime, but must not change file contents.
    return sameIdentity(a, b) && a.st_mode == b.st_mode && a.st_size == b.st_size &&
           a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
           a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec;
}

struct Descriptor {
    int value;
    explicit Descriptor(int fd) : value(fd) {}
    ~Descriptor() { if (value >= 0) close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
};

struct Stage {
    Result& result;
    int parent;
    std::string name;
    std::string recoveryPath;
    int directory = -1;
    bool preserve = false;

    ~Stage() {
        if (directory >= 0) {
            if (!preserve) {
                if (unlinkat(directory, "alias", 0) != 0 && errno != ENOENT)
                    posixError(result, "CLEANUP_FAILED", "Removing staged alias in " + recoveryPath);
            }
            close(directory);
        }
        if (!name.empty() && !preserve && unlinkat(parent, name.c_str(), AT_REMOVEDIR) != 0)
            posixError(result, "CLEANUP_FAILED", "Removing staging directory " + recoveryPath);
        if (preserve)
            error(result, "CLEANUP_FAILED",
                  "Concurrent modification: retained displaced file for recovery in " + recoveryPath);
    }
};

bool genuineAlias(NSURL *url, Result& result) {
    NSNumber *isAlias = nil;
    NSError *native = nil;
    if (![url getResourceValue:&isAlias forKey:NSURLIsAliasFileKey error:&native]) {
        error(result, "IO_ERROR", "Inspecting destination: " + utf8(native.localizedDescription), native);
        return false;
    }
    if (!isAlias.boolValue) {
        error(result, "UNSAFE_OVERWRITE", "Only an existing genuine Finder alias may be replaced");
        return false;
    }
    if (![NSURL bookmarkDataWithContentsOfURL:url error:&native]) {
        error(result, "UNSAFE_OVERWRITE",
              "Destination is not a readable Finder bookmark file: " + utf8(native.localizedDescription),
              native);
        return false;
    }
    return true;
}

void publish(Result& result, NSURL *targetURL, NSURL *destinationURL,
             const struct stat& targetInfo, const Options& options) {
    NSString *destination = destinationURL.path;
    NSString *parentPath = destination.stringByDeletingLastPathComponent;
    std::string leaf = utf8(destination.lastPathComponent);
    Descriptor parent(open(parentPath.fileSystemRepresentation, O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (parent.value < 0) {
        posixError(result, "IO_ERROR", "Opening destination parent");
        return;
    }
    struct stat original{};
    bool exists = fstatat(parent.value, leaf.c_str(), &original, AT_SYMLINK_NOFOLLOW) == 0;
    if (!exists && errno != ENOENT) {
        posixError(result, "IO_ERROR", "Inspecting destination");
        return;
    }
    if (exists && sameIdentity(targetInfo, original)) {
        error(result, "SELF_DESTINATION", "Target and destination identify the same filesystem object");
        return;
    }
    if (exists && !options.overwrite) {
        error(result, "DESTINATION_EXISTS", "Destination already exists");
        return;
    }
    Descriptor oldFile(exists && S_ISREG(original.st_mode)
                           ? openat(parent.value, leaf.c_str(), O_EVTONLY | O_NOFOLLOW | O_CLOEXEC)
                           : -1);
    if (exists) {
        if (!S_ISREG(original.st_mode)) {
            error(result, "UNSAFE_OVERWRITE", "Directories, symlinks and special files cannot be replaced");
            return;
        }
        if (oldFile.value < 0) {
            posixError(result, "IO_ERROR", "Opening existing alias without following symlinks");
            return;
        }
        struct stat opened{};
        if (fstat(oldFile.value, &opened) != 0) {
            posixError(result, "IO_ERROR", "Inspecting existing alias descriptor");
            return;
        }
        if (!sameVersion(original, opened)) {
            error(result, "CONCURRENT_MODIFICATION", "Destination changed while opening it");
            return;
        }
        if (!genuineAlias(destinationURL, result)) return;
    }

    NSError *native = nil;
    NSData *bookmark = [targetURL bookmarkDataWithOptions:NSURLBookmarkCreationSuitableForBookmarkFile
                         includingResourceValuesForKeys:nil relativeToURL:nil error:&native];
    if (!bookmark) {
        error(result, "IO_ERROR", "Creating Finder bookmark: " + utf8(native.localizedDescription), native);
        return;
    }
    struct stat targetNow{};
    if (lstat(targetURL.path.fileSystemRepresentation, &targetNow) != 0) {
        posixError(result, "CONCURRENT_MODIFICATION", "Rechecking target after bookmark creation");
        return;
    }
    if (!sameVersion(targetInfo, targetNow)) {
        error(result, "CONCURRENT_MODIFICATION", "Target changed while creating its bookmark");
        return;
    }
    Stage stage{result, parent.value, "", ""};
    std::string stageName = ".create-alias-" + utf8(NSUUID.UUID.UUIDString);
    if (mkdirat(parent.value, stageName.c_str(), 0700) != 0) {
        posixError(result, "IO_ERROR", "Creating exclusive sibling staging directory");
        return;
    }
    stage.name = stageName;
    stage.recoveryPath = utf8([parentPath stringByAppendingPathComponent:@(stage.name.c_str())]);
    stage.directory = openat(parent.value, stage.name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (stage.directory < 0) {
        posixError(result, "IO_ERROR", "Opening staging directory");
        return;
    }
    NSURL *stageURL = [[destinationURL.URLByDeletingLastPathComponent
                         URLByAppendingPathComponent:@(stage.name.c_str()) isDirectory:YES]
                        URLByAppendingPathComponent:@"alias"];
    if (![NSURL writeBookmarkData:bookmark toURL:stageURL options:0 error:&native]) {
        error(result, "IO_ERROR", "Writing Finder alias: " + utf8(native.localizedDescription), native);
        return;
    }
    struct stat staged{};
    if (fstatat(stage.directory, "alias", &staged, AT_SYMLINK_NOFOLLOW) != 0) {
        posixError(result, "IO_ERROR", "Inspecting staged alias");
        return;
    }
    if (!exists) {
        if (renameatx_np(stage.directory, "alias", parent.value, leaf.c_str(), RENAME_EXCL) != 0)
            posixError(result, errno == EEXIST ? "DESTINATION_EXISTS" : "IO_ERROR",
                       "Publishing alias without replacing destination");
        return;
    }

    struct stat current{}, held{};
    if (fstatat(parent.value, leaf.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
        fstat(oldFile.value, &held) != 0) {
        posixError(result, "CONCURRENT_MODIFICATION", "Rechecking destination before replacement");
        return;
    }
    if (!sameVersion(original, current) || !sameVersion(original, held)) {
        error(result, "CONCURRENT_MODIFICATION", "Destination changed before replacement");
        return;
    }
    // Coordination serializes cooperating writers, not hostile namespace mutations.
    // Darwin has no compare-inode-and-rename operation. Untrusted writers with
    // access to this directory must be excluded by the caller's access controls;
    // identity checks/rollback do not constitute a lock against those writers.
    // SWAP retains the displaced object; unlike rename(), it never destroys it.
    if (renameatx_np(stage.directory, "alias", parent.value, leaf.c_str(), RENAME_SWAP) != 0) {
        posixError(result, "IO_ERROR", "Atomically exchanging existing alias");
        return;
    }
    struct stat displaced{};
    if (fstatat(stage.directory, "alias", &displaced, AT_SYMLINK_NOFOLLOW) == 0 &&
        sameContentVersion(original, displaced)) return;

    error(result, "CONCURRENT_MODIFICATION", "Destination changed during replacement; attempting rollback");
    // Never delete an unrecognized object. If rollback cannot be established,
    // retain the displaced object and report its recovery directory explicitly.
    struct stat published{};
    if (fstatat(parent.value, leaf.c_str(), &published, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameIdentity(staged, published) ||
        renameatx_np(stage.directory, "alias", parent.value, leaf.c_str(), RENAME_SWAP) != 0) {
        stage.preserve = true;
        return;
    }
    struct stat recovered{};
    if (fstatat(stage.directory, "alias", &recovered, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameIdentity(staged, recovered)) stage.preserve = true;
}

} // namespace

Result createAlias(const std::string& target, const std::string& destination,
                   const Options& options) {
    @autoreleasepool {
        Result result;
        result.targetPath = target;
        result.aliasPath = destination;
        result.kind = "alias";
        const auto unsupported = [&](bool present, const char *name) {
            if (present)
                error(result, "UNSUPPORTED_OPTION", "Option is supported only on Windows", nil, name);
        };
        unsupported(options.arguments.has_value(), "arguments");
        unsupported(options.argumentsText.has_value(), "argumentsText");
        unsupported(options.workingDirectory.has_value(), "workingDirectory");
        unsupported(options.description.has_value(), "description");
        unsupported(options.iconPath.has_value(), "iconPath");
        unsupported(options.iconIndex.has_value(), "iconIndex");
        unsupported(options.showCommand.has_value(), "showCommand");
        if (!result.errors.empty()) return result;

        NSString *targetString = pathString(target);
        NSString *destinationString = pathString(destination);
        if (!targetString || !destinationString) {
            error(result, "INVALID_PATH", "Paths must be nonempty absolute UTF-8 POSIX paths without NUL");
            return result;
        }
        while (targetString.length > 1 && [targetString hasSuffix:@"/"])
            targetString = [targetString substringToIndex:targetString.length - 1];
        // Final target symlinks are rejected rather than silently aliasing their referent.
        // Symlinks in ancestor directories follow normal POSIX path resolution.
        struct stat info{};
        if (lstat(targetString.fileSystemRepresentation, &info) != 0) {
            posixError(result, errno == ENOENT ? "TARGET_NOT_FOUND" : "IO_ERROR", "Inspecting target");
            return result;
        }
        if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) {
            error(result, "UNSUPPORTED_TARGET", "Only ordinary files and folders are supported; target symlinks are rejected");
            return result;
        }
        NSString *normalizedDestination = destinationString.stringByStandardizingPath;
        if ([targetString.stringByStandardizingPath isEqualToString:normalizedDestination]) {
            error(result, "SELF_DESTINATION", "Target and destination paths must differ");
            return result;
        }
        if ([normalizedDestination isEqualToString:@"/"] ||
            [destinationString hasSuffix:@"/"] ||
            [destinationString.lastPathComponent isEqualToString:@"."] ||
            [destinationString.lastPathComponent isEqualToString:@".."]) {
            error(result, "INVALID_PATH", "Destination must name a file, without a trailing slash, dot or dot-dot");
            return result;
        }
        NSURL *targetURL = [NSURL fileURLWithPath:targetString isDirectory:S_ISDIR(info.st_mode)];
        NSURL *destinationURL = [NSURL fileURLWithPath:destinationString isDirectory:NO];
        NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        NSError *coordinationError = nil;
        auto operation = [&] {
            publish(result, targetURL, destinationURL, info, options);
        };
        [coordinator coordinateWritingItemAtURL:destinationURL
                                       options:NSFileCoordinatorWritingForReplacing
                                         error:&coordinationError
                                    byAccessor:^(NSURL *) { operation(); }];
        if (coordinationError)
            error(result, "IO_ERROR", "Coordinating alias publication: " +
                  utf8(coordinationError.localizedDescription), coordinationError);
        result.ok = result.errors.empty();
        return result;
    }
}

} // namespace alias_plugin
