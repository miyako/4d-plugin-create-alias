//%attributes = {"invisible":true}
var $cwd; $name; $targetPath; $aliasPath; $relativeTarget; $relativeAlias : Text
var $root; $database : 4D.Folder
var $target : 4D.File
var $status : Object

// The test runner launches tool4d from the repository root, not the database folder.
$database:=Folder(Folder(fk database folder).platformPath; fk platform path)
$cwd:=$database.parent.parent.platformPath
$name:="relative-test-"+Generate UUID
$root:=Folder($cwd; fk platform path).folder($name)
$root.create()
$root.folder("sub").create()
$target:=$root.file("target.txt")
$target.setText("cwd fixture")
$targetPath:=$target.platformPath
$aliasPath:=$root.file("relative").platformPath
If (Is macOS)
    $relativeTarget:=":"+$name+":target.txt"
    $relativeAlias:=":"+$name+":relative"
Else
    $relativeTarget:=$name+"\\target.txt"
    $relativeAlias:=$name+"\\relative"
    $aliasPath:=$aliasPath+".lnk"
End if
OK:=31
$status:=CREATE_ALIAS_EX($relativeTarget; $relativeAlias)
ASSERT(OK=31)
ASSERT($status.OK=True; JSON Stringify($status))
ASSERT(Compare strings($status.targetPath; $targetPath; sk char codes)=0)
ASSERT(Compare strings($status.aliasPath; $aliasPath; sk char codes)=0)

If (Is macOS)
    OK:=37
    $status:=CREATE_ALIAS_EX($name+"/target.txt"; $name+"/posix"; New object("pathFormat"; "posix"))
    ASSERT(OK=37)
    ASSERT($status.OK=True; JSON Stringify($status))
    ASSERT(Compare strings($status.targetPath; Convert path system to POSIX($targetPath); sk char codes)=0)
    $status:=CREATE_ALIAS_EX(":"+$name+":sub::target.txt"; ":"+$name+":parent")
    ASSERT($status.OK=True; JSON Stringify($status))
    ASSERT(Compare strings($status.targetPath; $targetPath; sk char codes)=0)
End if
$root.delete(Delete with contents)
LOG EVENT(Into system standard outputs; "Relative host tests passed"+Char(Line feed); Information message)
