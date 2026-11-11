//%attributes = {"invisible":true}
var $root : 4D.Folder
var $target; $alias : 4D.File
var $status; $options; $invalidOption : Object
var $invalidOptions : Collection
var $before; $name; $targetPath; $aliasPath; $resolvedPath : Text
var $i : Integer

$root:=Folder(fk database folder).folder("host-test-"+Generate UUID)
$root.create()
$target:=$root.file("target 日本語.txt")
$target.setText("original")
$alias:=$root.file("created")
$targetPath:=$target.platformPath
$aliasPath:=$alias.platformPath
$options:=New object("overwrite"; False)
$before:=JSON Stringify($options)
OK:=17
$status:=CREATE_ALIAS_EX($targetPath; $aliasPath; $options)
ASSERT(OK=17)
ASSERT($status.OK=True; JSON Stringify($status))
ASSERT($status.errors.length=0)
ASSERT(Compare strings($status.targetPath; $targetPath; sk char codes)=0)
ASSERT(JSON Stringify($options)=$before)
If (Is Windows)
    ASSERT($status.kind="shortcut")
    ASSERT(Compare strings($status.aliasPath; $aliasPath+".lnk"; sk char codes)=0)
Else
    ASSERT($status.kind="alias")
    ASSERT(Compare strings($status.aliasPath; $aliasPath; sk char codes)=0)
End if
$alias:=File($status.aliasPath; fk platform path)
ASSERT($alias.exists)
ASSERT($alias.isAlias)
RESOLVE ALIAS($alias.platformPath; $resolvedPath)
ASSERT(OK=1)
ASSERT(Compare strings($resolvedPath; $target.platformPath; sk char codes)=0)
OK:=19
$status:=CREATE_ALIAS_EX($targetPath; $aliasPath)
ASSERT(OK=19)
ASSERT($status.OK=False)
ASSERT($alias.exists)

$invalidOptions:=New collection(\
    New object("misspelled"; 1); \
    New object("overwrite"; 1); \
    New object("arguments"; New collection("valid"; 2)); \
    New object("iconIndex"; 0.5))
For each ($invalidOption; $invalidOptions)
    OK:=41
    $status:=CREATE_ALIAS_EX($targetPath; $root.file("must-not-exist").platformPath; $invalidOption)
    ASSERT(OK=41)
    ASSERT($status.OK=False)
    ASSERT($root.file("must-not-exist").exists=False)
    ASSERT($root.file("must-not-exist.lnk").exists=False)
End for each

OK:=23
$status:=CREATE_ALIAS_EX($targetPath; $aliasPath; New object("overwrite"; True))
ASSERT(OK=23)
ASSERT($status.OK=True)
ASSERT($alias.exists)

OK:=43
$status:=CREATE_ALIAS_EX($root.file("missing-target").platformPath; $aliasPath; New object("overwrite"; True))
ASSERT(OK=43)
ASSERT($status.OK=False)
ASSERT($alias.exists)

OK:=47
$status:=sample_create_alias($targetPath; $root.file("sample").platformPath)
ASSERT(OK=47)
ASSERT($status.OK=True)

If (Is macOS)
    $targetPath:=Convert path system to POSIX($target.platformPath)
    $aliasPath:=Convert path system to POSIX($root.file("posix alias").platformPath)
    OK:=29
    $status:=CREATE_ALIAS_EX($targetPath; $aliasPath; New object("pathFormat"; "posix"))
    ASSERT(OK=29)
    ASSERT($status.OK=True)
    ASSERT(Compare strings($status.targetPath; $targetPath; sk char codes)=0)
    ASSERT(Compare strings($status.aliasPath; $aliasPath; sk char codes)=0)
    $status:=CREATE_ALIAS_EX($targetPath; $aliasPath)
    ASSERT($status.errors[0].code="INVALID_PATH_FORMAT")
End if

OK:=53
$status:=CREATE_ALIAS_EX($target.platformPath; $target.platformPath; New object("overwrite"; True))
ASSERT(OK=53)
ASSERT($status.OK=False)
ASSERT($target.getText()="original")
$root.delete(Delete with contents)
