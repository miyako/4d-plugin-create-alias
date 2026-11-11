//%attributes = {"invisible":true}
var $info : Object
$info:=Application info:C1599
If ($info.headless)
    LOG EVENT(Into system standard outputs; "Host: contract"+Char(Line feed); Information message)
    test_contract
    LOG EVENT(Into system standard outputs; "Host: absolute paths"+Char(Line feed); Information message)
    test_paths
    LOG EVENT(Into system standard outputs; "Host: relative paths"+Char(Line feed); Information message)
    test_relative
    LOG EVENT(Into system standard outputs; "PASS"+Char(Line feed); Information message)
End if
