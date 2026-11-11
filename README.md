# Create Alias

English | [日本語](README.ja.md)

Native 4D plug-in for creating genuine Finder aliases on macOS and Shell Link
shortcuts on Windows. One command, one structured result; no component, helper
CLI, shell invocation, symlinks, hardlinks, launchers, or bookmark BLOB API.

## Requirements

- 4D v20 or later, in a cooperative process.
- macOS 11 or later (universal arm64/x86_64), or Windows x64.
- The target must be an existing ordinary file or folder.

The host test project uses the available **20.6 LTS** tool4d runtime. This is
not a claim that every earlier v20 patch has been executed.

## Installation

Build from source, or use a maintainer-published distribution when available.
Copy `create-alias.bundle` into the application's or project's **Plugins**
folder and restart 4D. The combined distribution contains both platforms.
There is no published release implied by this repository's initial version.

## Command

```4d
var $status : Object
$status:=CREATE_ALIAS_EX($targetPath; $aliasPath {; $options})
```

The braces show an optional parameter and are not literal 4D syntax.

| Parameter | Type | Meaning |
|---|---|---|
| `$targetPath` | Text | Existing file or folder to reference; empty is `INVALID_TARGET`. |
| `$aliasPath` | Text | New alias/shortcut path; its parent must already exist. |
| `$options` | Object | Optional options below; omitted or null means no options. Never mutated. |
| `$status` | Object | `OK` Boolean and `errors` Collection; success also includes paths and `kind`. |

```4d
var $status : Object
var $folder : 4D.Folder
$folder:=Folder(fk desktop folder)
$status:=CREATE_ALIAS_EX($folder.file("report.pdf").platformPath; $folder.file("report alias").platformPath)
If (Not($status.OK))
    LOG EVENT(Into system standard outputs; JSON Stringify($status.errors)+Char(Line feed); Error message)
End if
```

The command **never reads or sets the `OK` system variable**. Check
`$status.OK`, not `OK`. Validation and native errors are returned rather than
displaying a dialog. Runtime type errors in the declared command signature
remain 4D errors before the plug-in can run.

### Options

All names and enum values are case-sensitive. Unknown keys are errors, including
misspellings. Values are not coerced; null property values are not defaults.
All validation precedes filesystem publication. Errors are fail-fast (the
collection is not an exhaustive list of every invalid field).

| Option | Type / default | Contract |
|---|---|---|
| `overwrite` | Boolean / `false` | Allow replacing an existing genuine alias/shortcut only. Never replace an unrelated file, directory, or symbolic link/reparse point. |
| `pathFormat` | Text / `"system"` | Traditional 4D platform syntax. `"posix"` explicitly selects POSIX paths on macOS; unsupported on Windows. |
| `arguments` | Collection of Text / absent | Windows only. Ordered arguments encoded with Windows CRT-style quoting. Empty collection means no arguments. |
| `argumentsText` | Text / absent | Windows only. Raw Windows argument string, verbatim. Mutually exclusive with `arguments`, even if either is empty. |
| `workingDirectory` | Text / absent | Windows only. Existing directory; same path rules as the target. |
| `description` | Text / absent | Windows only. Shortcut description. |
| `iconPath` | Text / absent | Windows only. Existing icon file/resource path, using the same path rules. |
| `iconIndex` | Integer number / `0` with `iconPath` | Windows only. Signed resource index; requires `iconPath`. Fractional or out-of-range values are errors. |
| `showCommand` | Integer number / `1` | Windows only. `1` = normal, `3` = maximized, `7` = minimized without activation. Other values are errors. |

Every Windows-only field is `UNSUPPORTED_OPTION` on macOS, **even an empty
value or zero**. A Finder alias cannot carry launch arguments. No `.app`
launcher is silently generated.

#### Windows arguments

```4d
var $options; $status : Object
$options:=New object("arguments"; New collection("a b"; "日本語"; ""; "quoted \"text\""; "C:\\ending\\"))
$status:=CREATE_ALIAS_EX("C:\\Tools\\viewer.exe"; "C:\\Users\\Public\\Desktop\\viewer"; $options)
```

`arguments` treats every item as one argv element. Double quotes surround
arguments; backslashes before a quote are doubled and the quote escaped;
backslashes at the end of a quoted argument are doubled. An empty item is
`""`. This matches the Microsoft C/C++ runtime convention, not `cmd.exe`,
PowerShell, a shell command line, or every application's custom parser.
Arguments are **never path-converted**, expanded, or interpreted as shell
syntax. `argumentsText` bypasses serialization; the caller owns its quoting.
The target controls argv parsing and execution behavior.

Windows Shell Link fields have conservative supported bounds: paths to 259
UTF-16 code units, description to 1,023, and the serialized/raw argument string
to 1,023 (including quoting). These are not claims about the OS's absolute
maximum. Over-limit values are errors, never silently truncated. The backend
reloads the staged shortcut into a fresh COM object and verifies arguments
exactly before publication; an in-memory setter alone is not acceptance.

### Paths

**Default `"system"`** accepts traditional absolute macOS paths such as
`Macintosh HD:Users:me:report.pdf`, and Windows drive/UNC paths such as
`C:\Reports\report.pdf` and `\\server\share\report.pdf`.
macOS POSIX paths such as `/Users/me/report.pdf` require
`New object("pathFormat"; "posix")`.

Relative paths are anchored to **one snapshot of the actual 4D host process
current working directory per invocation**. The plug-in never changes that
directory. It is not necessarily the project/database folder, and is never the
plug-in folder. Failure to obtain it is `CWD_ERROR`; there is no fallback.

| Mode | Relative syntax |
|---|---|
| macOS system | `:folder:file`, `::sibling:file`, or a bare filename. `volume:folder:file` is absolute. |
| macOS POSIX | `folder/file`, `../sibling/file`. A leading `/` is absolute. |
| Windows system | `folder\file`, `..\sibling\file`. Drive-relative `C:file` and root-relative `\file` are rejected as ambiguous. |

The adapter anchors traditional relative macOS syntax **before** calling
4D's path conversion commands. This intentionally differs from calling
[`Convert path system to POSIX`](https://developer.4d.com/docs/commands/convert-path-system-to-posix)
on a relative path, which uses the **database folder**.
It uses that command and
[`Convert path POSIX to system`](https://developer.4d.com/docs/commands/convert-path-posix-to-system)
without URL encoding for absolute conversions, not string replacement.
4D filesystem aliases such as `/PACKAGE` are not native POSIX paths.

These rules apply to target, destination, `workingDirectory`, `iconPath`, and
returned paths, **not arguments or description**. NUL and malformed Unicode
are rejected. Returned paths are absolute in the selected format, normalized
lexically rather than promised to be canonical physical paths.
Windows appends `.lnk` if absent (case-insensitive); the returned `aliasPath`
includes it. macOS does not append an extension.

### Results and errors

Success:

```json
{
  "OK": true,
  "targetPath": "<absolute path in selected format>",
  "aliasPath": "<actual absolute destination>",
  "kind": "alias",
  "errors": []
}
```

Windows returns `"kind": "shortcut"`. Failure:

```json
{
  "OK": false,
  "errors": [
    {"code": "UNSUPPORTED_OPTION", "message": "Launch options are Windows-only", "option": "arguments"}
  ]
}
```

Every error has stable `code` and human-readable `message`. `option` identifies
the pertinent field when available (also `targetPath` / `aliasPath`).
Native/host errors additionally include `nativeDomain` and numeric
`nativeCode`; messages and native values are platform-dependent. Failure
does not promise success-only path/kind fields.

Adapter codes include `INVALID_TARGET`, `INVALID_PATH`, `INVALID_PATH_FORMAT`,
`INVALID_TEXT`, `UNKNOWN_OPTION`, `INVALID_OPTION_TYPE`, `INVALID_OPTION`,
`CONFLICTING_OPTIONS`, `UNSUPPORTED_OPTION`, `CWD_ERROR`,
`PATH_CONVERSION_FAILED`, `HOST_ERROR`, and `INTERNAL_ERROR`.
Native failures also identify target inspection, destination protection,
alias/shortcut construction, publication, and cleanup errors.

| Native code | Meaning |
|---|---|
| `INVALID_PATH`, `INVALID_OPTION` | Invalid native path/field, including Shell Link length limits. |
| `TARGET_NOT_FOUND` | Target does not exist. |
| `UNSUPPORTED_TARGET` | Final target is a symlink/reparse point or a special file, not an ordinary file/folder. |
| `SELF_DESTINATION` | macOS target and destination refer to the same entry. |
| `UNSUPPORTED_OPTION` | A Windows launch option was supplied on macOS. |
| `DESTINATION_EXISTS` | No-clobber publication found an existing entry. |
| `UNSAFE_OVERWRITE` | The existing destination is not a safely replaceable genuine alias/shortcut. |
| `CONCURRENT_MODIFICATION` | macOS detected an entry/version change during replacement. |
| `IO_ERROR` | Native inspection, staging, COM, serialization, or publication failed. |
| `CLEANUP_FAILED` | Staging/backup cleanup failed; inspect the message and any retained recovery path. |

The ordinary error contract requires a functioning 4D allocator/API. If the
host cannot allocate the result object itself, the plug-in writes an explicit
diagnostic to stderr and cannot guarantee an object; it never invents success.

### Publication and overwrite

Creation is staged beside the destination on the same filesystem and then
published without a partially written alias. Default no-clobber publication
does not replace an existing destination, including a broken symlink.
`overwrite:true` is permission to replace a **genuine alias/shortcut**, not a
general-purpose file-overwrite option. Target/destination equality is an error.
Failed native serialization or publication preserves the existing destination.
Concurrent changes are checked and fail closed where the native API permits;
this is not a transaction against a hostile process that can rewrite the parent
directory. Do not use shared writable directories as a security boundary.
No parent folders are created.
Ancestor links follow normal OS resolution; the final target and final
destination cannot be symlinks/reparse points. If rollback cannot be proved
safe, the displaced original is retained at the recovery path reported in the
error message, rather than deleted. A cleanup error may be reported **after
publication**; inspect the actual destination before retrying. These recovery
conditions are distinct from a serialization/publication failure before commit.

## Building from source

Requires CMake 3.20+, Xcode on macOS or Visual Studio with C++ on Windows.
Only operating-system libraries are linked; the SDK is a pinned submodule.

```sh
git clone --recurse-submodules https://github.com/miyako/4d-plugin-create-alias.git
cd 4d-plugin-create-alias
cmake -S create-alias -B create-alias/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build create-alias/build --config Release
ctest --test-dir create-alias/build -C Release --output-on-failure
```

On Windows, use `-A x64` instead of `-DCMAKE_OSX_ARCHITECTURES=...`.
The Windows plugin and tests use the static CRT. Build outputs go into the
test project's ignored `Plugins` folder. macOS creates a `BNDL`, not an `APPL`.

## Validation and host sample

Native macOS tests use real Foundation bookmark files, check Finder alias
metadata, and resolve to the original file/folder. Windows tests use real COM
property readback and **launch a harmless argv-recording executable through the
generated `.lnk`** to compare spaces, Unicode, empty arguments, embedded quotes,
and trailing backslashes. Native tests are not SDK shims or proof of host API
behavior.

The real 4D host project is
`create-alias/create-alias-test/Project/create-alias.4DProject`;
`sample_create_alias` is a v20-compatible callable example.
Host tests exercise structured objects, omitted options, option immutability,
absolute/relative paths, POSIX opt-in, errors, overwrite, and `OK` sentinels.
Launch from the repository root for the controlled relative-path fixture:

```sh
/path/to/tool4d --dataless --startup-method=test_all \
  --project="$PWD/create-alias/create-alias-test/Project/create-alias.4DProject" \
  > host-test.log 2>&1
result=$?
cat host-test.log
test "$result" = 0 && grep -qx PASS host-test.log
```

The fixture derives the expected launch directory from the test project's
location, independently of the plug-in, and creates a uniquely named fixture
there. The generated CI likewise launches from the repository root.
An exact `PASS` line **and** zero exit status are required; a failed assertion
may still exit zero.

The hand-authored runner uses the built-in token `Application info:C1599`:
plain `Application info` evaluated to undefined in this v20.6 fixture.
This observation is not a general statement about v20 parsing or IDE-authored
projects. The exported `CREATE_ALIAS_EX` calls remain plain.

## CI/CD

Generated from the tracked canonical [pipeline](docs/plugin-pipeline.md), not
hand-authored workflow variants:

| Workflow | Trigger | Purpose |
|---|---|---|
| **Bump version** | Manual on `main` | Commit a patch/minor/major `VERSION` change; no tag. |
| **Build and Test** | Push / PR / manual / reusable | Universal macOS and Windows x64 builds, native + real host tests, package acceptance. |
| **Build and Release** | Explicit manual dispatch on `main`, no inputs | Same-revision tests, Developer ID signing/notarization, verified publication of `vVERSION`. |

```sh
python3 scripts/generate_plugin_pipeline.py --config .github/plugin-pipeline.json --output "$PWD" --check
```

Normal tests require no signing secrets. Release signing uses
`APPLE_DEVELOPER_ID_CERTIFICATE`, `APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD`,
`KEYCHAIN_PASSWORD`, `NOTARYTOOL_APPLE_ID`, `NOTARYTOOL_TEAM_ID`, and
`NOTARYTOOL_PASSWORD` in the `release-signing` environment.
Follow the [user-run credential instructions](.github/instructions/plugin-dev.instructions.md#required-github-secrets-for-release);
agents must not inspect/upload credential values. Windows distributions are
explicitly unsigned. Generating this pipeline is **not authorization to release**.

## References

Read-only prior art:
[`4d-plugin-alias-manager`](https://github.com/miyako/4d-plugin-alias-manager)
and [`4d-plugin-bookmark-manager`](https://github.com/miyako/4d-plugin-bookmark-manager).
Neither is a dependency; this command does not preserve their APIs or broaden
Finder aliases into symbolic/hard links or launchers.

## License

[MIT](LICENSE). SDK licensing is included in the pinned
[`4D-Plugin-SDK`](4D-Plugin-SDK) submodule and generated pipeline notices.
