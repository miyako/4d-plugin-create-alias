//%attributes = {"invisible":true}
// 4D v20 syntax. Pass traditional platform paths on both platforms.
#DECLARE($targetPath : Text; $aliasPath : Text)->$status : Object
$status:=CREATE_ALIAS_EX($targetPath; $aliasPath; New object("overwrite"; False))
If (Not($status.OK))
    LOG EVENT(Into system standard outputs; JSON Stringify($status.errors)+Char(Line feed); Error message)
End if
