//%attributes = {"invisible":true}
var $status; $options; $item : Object
var $i; $sentinel : Integer
var $invalid : Collection
var $before : Text

For ($i; 1; 100)
    OK:=7
    $status:=CREATE_ALIAS_EX(""; "")
    ASSERT(OK=7; "empty call must preserve OK")
    ASSERT(Value type($status)=Is object)
    ASSERT($status.OK=False)
    ASSERT($status.errors.length=1)
    ASSERT($status.errors[0].code="INVALID_TARGET"; JSON Stringify($status))
End for

$options:=New object("pathFormat"; "system"; "overwrite"; False)
$before:=JSON Stringify($options)
For ($i; 1; 100)
    OK:=13
    $status:=CREATE_ALIAS_EX(""; ""; $options)
    ASSERT(OK=13)
    ASSERT($status.errors[0].code="INVALID_TARGET")
    ASSERT(Compare strings(JSON Stringify($options); $before; sk char codes)=0)
End for
$status:=CREATE_ALIAS_EX(""; ""; Null)
ASSERT($status.errors[0].code="INVALID_TARGET")

$invalid:=New collection(\
    New object("bogus"; True); \
    New object("overwrite"; "yes"); \
    New object("pathFormat"; "url"); \
    New object("arguments"; New collection(1)); \
    New object("iconIndex"; 1.5))
For each ($options; $invalid)
    OK:=9
    $status:=CREATE_ALIAS_EX(""; ""; $options)
    ASSERT(OK=9)
    ASSERT($status.OK=False)
    ASSERT($status.errors.length>0)
    ASSERT($status.errors[0].option#"")
End for each

If (Is macOS)
    $invalid:=New collection(\
        New object("arguments"; New collection()); \
        New object("argumentsText"; ""); \
        New object("workingDirectory"; ""); \
        New object("description"; ""); \
        New object("iconPath"; ""); \
        New object("iconIndex"; 0); \
        New object("showCommand"; 1))
    For each ($options; $invalid)
        OK:=11
        $status:=CREATE_ALIAS_EX(""; ""; $options)
        ASSERT(OK=11)
        ASSERT($status.OK=False)
        ASSERT($status.errors[0].code="UNSUPPORTED_OPTION")
    End for each
Else
    $status:=CREATE_ALIAS_EX(""; ""; New object("arguments"; New collection(); "argumentsText"; "raw"))
    ASSERT($status.errors[0].code="CONFLICTING_OPTIONS")
    $status:=CREATE_ALIAS_EX(""; ""; New object("showCommand"; 2))
    ASSERT($status.errors[0].code="INVALID_OPTION")
End if
