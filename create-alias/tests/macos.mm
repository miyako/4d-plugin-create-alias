#include "../native.hpp"

#import <Foundation/Foundation.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <thread>
#include <unistd.h>

namespace {

using alias_plugin::Options;
using alias_plugin::Result;

std::string text(NSString *value) { return value.UTF8String ?: ""; }

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void success(const Result& result) {
    std::string detail;
    for (const auto& error : result.errors)
        detail += error.code + ": " + error.message + "\n";
    require(result.ok && result.errors.empty(), "Expected success: " + detail);
    require(result.kind == "alias", "Incorrect alias kind");
}

void failure(const Result& result, const std::string& code) {
    require(!result.ok && !result.errors.empty(), "Expected failure: " + code);
    for (const auto& error : result.errors) {
        if (error.code == code) return;
    }
    throw std::runtime_error("Missing error " + code + "; got " + result.errors.front().code);
}

NSString *join(NSString *root, NSString *name) {
    return [root stringByAppendingPathComponent:name];
}

void write(NSString *path, NSString *contents) {
    NSError *error = nil;
    require([contents writeToFile:path atomically:NO encoding:NSUTF8StringEncoding error:&error],
            "Writing fixture: " + text(error.localizedDescription));
}

NSData *read(NSString *path) {
    NSData *value = [NSData dataWithContentsOfFile:path];
    require(value != nil, "Reading fixture " + text(path));
    return value;
}

NSSet<NSString *> *entries(NSString *path) {
    NSError *error = nil;
    NSArray *value = [NSFileManager.defaultManager contentsOfDirectoryAtPath:path error:&error];
    require(value != nil, "Reading directory: " + text(error.localizedDescription));
    return [NSSet setWithArray:value];
}

void isAliasTo(NSString *alias, NSString *target) {
    NSURL *url = [NSURL fileURLWithPath:alias];
    NSNumber *isAlias = nil;
    NSError *error = nil;
    require([url getResourceValue:&isAlias forKey:NSURLIsAliasFileKey error:&error] && isAlias.boolValue,
            "Output is not a genuine Finder alias: " + text(error.localizedDescription));
    NSData *bookmark = [NSURL bookmarkDataWithContentsOfURL:url error:&error];
    require(bookmark != nil, "Reading real bookmark: " + text(error.localizedDescription));
    BOOL stale = NO;
    NSURL *resolved = [NSURL URLByResolvingBookmarkData:bookmark
                                              options:NSURLBookmarkResolutionWithoutUI |
                                                      NSURLBookmarkResolutionWithoutMounting
                                        relativeToURL:nil bookmarkDataIsStale:&stale error:&error];
    require(resolved != nil, "Resolving real bookmark: " + text(error.localizedDescription));
    require([resolved.path.stringByResolvingSymlinksInPath
                isEqualToString:target.stringByResolvingSymlinksInPath],
            "Bookmark resolved to wrong target: " + text(resolved.path));
}

void run(NSString *root) {
    NSFileManager *manager = NSFileManager.defaultManager;
    NSString *file = join(root, @"target.txt");
    NSString *folder = join(root, @"folder");
    NSString *alias = join(root, @"file alias");
    write(file, @"original target");
    require([manager createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:nil],
            "Creating target folder");

    Result created = alias_plugin::createAlias(text(file), text(alias), {});
    success(created);
    require(created.targetPath == text(file) && created.aliasPath == text(alias), "Result paths differ");
    isAliasTo(alias, file);
    NSString *folderAlias = join(root, @"folder alias");
    success(alias_plugin::createAlias(text(folder), text(folderAlias), {}));
    isAliasTo(folderAlias, folder);
    NSString *trailingSlashAlias = join(root, @"folder with trailing slash alias");
    success(alias_plugin::createAlias(text(folder) + "/", text(trailingSlashAlias), {}));
    isAliasTo(trailingSlashAlias, folder);

    NSString *unicode = join(root, @"日本語-é-😀.txt");
    NSString *unicodeAlias = join(root, @"別名-é-😀");
    write(unicode, @"Unicode target");
    success(alias_plugin::createAlias(text(unicode), text(unicodeAlias), {}));
    isAliasTo(unicodeAlias, unicode);

    NSSet *before = entries(root);
    NSData *originalAlias = read(alias);
    failure(alias_plugin::createAlias(text(folder), text(alias), {}), "DESTINATION_EXISTS");
    require([read(alias) isEqualToData:originalAlias], "No-clobber changed existing alias");
    require([entries(root) isEqualToSet:before], "No-clobber leaked staging files");

    Options overwrite;
    overwrite.overwrite = true;
    success(alias_plugin::createAlias(text(folder), text(alias), overwrite));
    isAliasTo(alias, folder);
    NSString *newAlias = join(root, @"overwrite without previous destination");
    success(alias_plugin::createAlias(text(file), text(newAlias), overwrite));
    isAliasTo(newAlias, file);

    NSString *unrelated = join(root, @"unrelated.txt");
    write(unrelated, @"Do not overwrite this unrelated regular file!");
    NSData *unrelatedBytes = read(unrelated);
    failure(alias_plugin::createAlias(text(file), text(unrelated), {}), "DESTINATION_EXISTS");
    failure(alias_plugin::createAlias(text(file), text(unrelated), overwrite), "UNSAFE_OVERWRITE");
    require([read(unrelated) isEqualToData:unrelatedBytes], "Unrelated regular file was modified");
    NSString *falseAlias = join(root, @"alias flag without valid bookmark");
    write(falseAlias, @"This is unrelated data, not a Finder bookmark.");
    NSData *falseAliasBytes = read(falseAlias);
    unsigned char finderInfo[32]{};
    finderInfo[8] = 0x80;
    require(setxattr(falseAlias.fileSystemRepresentation, "com.apple.FinderInfo",
                     finderInfo, sizeof(finderInfo), 0, 0) == 0, "Setting Finder alias flag");
    failure(alias_plugin::createAlias(text(file), text(falseAlias), overwrite), "UNSAFE_OVERWRITE");
    require([read(falseAlias) isEqualToData:falseAliasBytes], "Malformed alias data was modified");
    before = entries(folder);
    failure(alias_plugin::createAlias(text(file), text(folder), overwrite), "UNSAFE_OVERWRITE");
    require([entries(folder) isEqualToSet:before], "Unrelated folder was modified");

    NSString *link = join(root, @"symbolic-link");
    require(symlink(file.fileSystemRepresentation, link.fileSystemRepresentation) == 0, "Creating symlink");
    failure(alias_plugin::createAlias(text(link), text(join(root, @"rejected link alias")), {}), "UNSUPPORTED_TARGET");
    NSString *folderLink = join(root, @"folder symbolic-link");
    require(symlink(folder.fileSystemRepresentation, folderLink.fileSystemRepresentation) == 0,
            "Creating folder symlink");
    failure(alias_plugin::createAlias(text(folderLink) + "/", text(join(root, @"rejected folder link")), {}),
            "UNSUPPORTED_TARGET");
    failure(alias_plugin::createAlias(text(folder), text(link), overwrite), "UNSAFE_OVERWRITE");
    struct stat info{};
    require(lstat(link.fileSystemRepresentation, &info) == 0 && S_ISLNK(info.st_mode),
            "Destination symlink was replaced");
    require([read(file) isEqualToData:[@"original target" dataUsingEncoding:NSUTF8StringEncoding]],
            "Symlink referent was modified");
    NSString *dangling = join(root, @"dangling-link");
    require(symlink("missing-target", dangling.fileSystemRepresentation) == 0, "Creating dangling symlink");
    failure(alias_plugin::createAlias(text(file), text(dangling), {}), "DESTINATION_EXISTS");
    failure(alias_plugin::createAlias(text(file), text(dangling), overwrite), "UNSAFE_OVERWRITE");

    NSString *fifo = join(root, @"fifo");
    require(mkfifo(fifo.fileSystemRepresentation, 0600) == 0, "Creating FIFO");
    failure(alias_plugin::createAlias(text(fifo), text(join(root, @"fifo alias")), {}), "UNSUPPORTED_TARGET");
    failure(alias_plugin::createAlias(text(file), text(fifo), overwrite), "UNSAFE_OVERWRITE");

    NSString *absentAlias = join(root, @"must not be created");
    std::vector<std::pair<std::string, Options>> unsupported;
    Options option;
    option.arguments = std::vector<std::string>{}; unsupported.push_back({"arguments", option});
    option = {}; option.argumentsText = ""; unsupported.push_back({"argumentsText", option});
    option = {}; option.workingDirectory = ""; unsupported.push_back({"workingDirectory", option});
    option = {}; option.description = ""; unsupported.push_back({"description", option});
    option = {}; option.iconPath = ""; unsupported.push_back({"iconPath", option});
    option = {}; option.iconIndex = 0; unsupported.push_back({"iconIndex", option});
    option = {}; option.showCommand = 0; unsupported.push_back({"showCommand", option});
    before = entries(root);
    for (const auto& [name, value] : unsupported) {
        Result result = alias_plugin::createAlias(text(file), text(absentAlias), value);
        failure(result, "UNSUPPORTED_OPTION");
        require(result.errors.front().option == name, "Unsupported option is not identified");
        Options replacing = value;
        replacing.overwrite = true;
        NSData *bytes = read(alias);
        failure(alias_plugin::createAlias(text(file), text(alias), replacing), "UNSUPPORTED_OPTION");
        require([read(alias) isEqualToData:bytes], "Unsupported option changed existing alias");
        require([entries(root) isEqualToSet:before], "Unsupported option wrote files");
    }

    for (const std::string& invalid : std::vector<std::string>{
             "", "relative", std::string("/bad\0path", 9), std::string("/bad\xff", 5)}) {
        failure(alias_plugin::createAlias(invalid, text(absentAlias), {}), "INVALID_PATH");
        failure(alias_plugin::createAlias(text(file), invalid, {}), "INVALID_PATH");
    }
    failure(alias_plugin::createAlias(text(file), text(root) + "/", {}), "INVALID_PATH");
    failure(alias_plugin::createAlias(text(file), text(root) + "/.", {}), "INVALID_PATH");
    failure(alias_plugin::createAlias(text(file), text(root) + "/..", {}), "INVALID_PATH");
    Result missing = alias_plugin::createAlias(text(join(root, @"missing")), text(absentAlias), {});
    failure(missing, "TARGET_NOT_FOUND");
    require(missing.errors.front().nativeDomain == "NSPOSIXErrorDomain" &&
            missing.errors.front().nativeCode == ENOENT, "POSIX NSError details were lost");
    failure(alias_plugin::createAlias(text(file), text(join(root, @"no parent/alias")), {}), "IO_ERROR");
    require(![manager fileExistsAtPath:join(root, @"no parent")], "Missing parent was created");
    failure(alias_plugin::createAlias(text(file), text(file), overwrite), "SELF_DESTINATION");
    failure(alias_plugin::createAlias(text(folder), text(folder), overwrite), "SELF_DESTINATION");
    NSString *hardlink = join(root, @"hardlink");
    require(::link(file.fileSystemRepresentation, hardlink.fileSystemRepresentation) == 0, "Creating hardlink");
    failure(alias_plugin::createAlias(text(file), text(hardlink), overwrite), "SELF_DESTINATION");

    originalAlias = read(alias);
    before = entries(root);
    failure(alias_plugin::createAlias(text(join(root, @"missing")), text(alias), overwrite), "TARGET_NOT_FOUND");
    require([read(alias) isEqualToData:originalAlias], "Failed replacement changed original alias bytes");
    isAliasTo(alias, folder);
    require([entries(root) isEqualToSet:before], "Failed replacement leaked staging files");

    require(chflags(alias.fileSystemRepresentation, UF_IMMUTABLE) == 0, "Locking original alias");
    Result locked = alias_plugin::createAlias(text(file), text(alias), overwrite);
    require(chflags(alias.fileSystemRepresentation, 0) == 0, "Unlocking original alias");
    failure(locked, "IO_ERROR");
    require([read(alias) isEqualToData:originalAlias], "Failed exchange changed original alias bytes");
    isAliasTo(alias, folder);
    require([entries(root) isEqualToSet:before], "Failed exchange leaked staging files");

    // Fail after creating the bookmark, at staging creation, not only during validation.
    if (getuid() != 0) {
        require(chmod(root.fileSystemRepresentation, 0500) == 0, "Making parent unwritable");
        Result denied = alias_plugin::createAlias(text(file), text(alias), overwrite);
        require(chmod(root.fileSystemRepresentation, 0700) == 0, "Restoring parent permissions");
        failure(denied, "IO_ERROR");
        require([read(alias) isEqualToData:originalAlias], "Failed publication changed original alias");
        require([entries(root) isEqualToSet:before], "Failed publication leaked staging files");
    }

    NSString *raced = join(root, @"concurrent alias");
    std::string raceTarget = text(file), raceDestination = text(raced);
    std::vector<Result> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i)
        threads.emplace_back([&, i] { results[i] = alias_plugin::createAlias(raceTarget, raceDestination, {}); });
    for (auto& thread : threads) thread.join();
    int successes = 0;
    for (const auto& result : results) {
        if (result.ok) ++successes;
        else failure(result, "DESTINATION_EXISTS");
    }
    require(successes == 1, "Concurrent no-clobber publication did not have exactly one winner");
    isAliasTo(raced, file);
    for (NSString *entry in entries(root))
        require(![entry hasPrefix:@".create-alias-"], "Leaked staging directory: " + text(entry));
}

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        (void)argc;
        std::filesystem::path executable = std::filesystem::absolute(argv[0]);
        NSString *base = @(executable.parent_path().string().c_str());
        NSString *root = join(base, [@"native-test-data-" stringByAppendingString:NSUUID.UUID.UUIDString]);
        NSError *error = nil;
        if (![NSFileManager.defaultManager createDirectoryAtPath:root withIntermediateDirectories:NO
                                                     attributes:@{NSFilePosixPermissions: @0700} error:&error]) {
            std::cerr << "FAIL: Creating fixtures: " << text(error.localizedDescription) << '\n';
            return 1;
        }
        int status = 0;
        try {
            run(root);
        } catch (const std::exception& exception) {
            chmod(root.fileSystemRepresentation, 0700);
            std::cerr << "FAIL: " << exception.what() << '\n';
            status = 1;
        }
        if (![NSFileManager.defaultManager removeItemAtPath:root error:&error]) {
            std::cerr << "FAIL: Cleaning fixtures: " << text(error.localizedDescription) << '\n';
            status = 1;
        }
        if (status == 0) std::cout << "PASS: macOS native Finder alias integration tests\n";
        return status;
    }
}
