--TEST--
errors and warnings: syntax
--FILE--
<?

error_reporting(E_ALL);
set_error_handler('my_error_handler');
function my_error_handler($errno, $errstr, $errfile, $errline) {
    echo $errstr."\n";
}

// show all syntax error warnings, that can be thrown by blitz
$T = new Blitz('errors.tpl');

?>
--EXPECTREGEX--
[^ ]+: SYNTAX ERROR: invalid method call \(errors\.tpl\: line 2, pos 7\)
[^ ]+: SYNTAX ERROR: invalid method call \(errors\.tpl\: line 4, pos 3\)
[^ ]+: SYNTAX ERROR: invalid method call \(errors\.tpl\: line 5, pos 5\)
[^ ]+: SYNTAX ERROR: invalid method call \(errors\.tpl\: line 6, pos 7\)
[^ ]+: SYNTAX ERROR: close tag without open \(errors\.tpl\: line 7, pos 3\)
[^ ]+: SYNTAX ERROR: open tag without close \(errors\.tpl\: line 8, pos 3\)
[^ ]+: SYNTAX ERROR: invalid method call \(errors\.tpl\: line 9, pos 9\)
[^ ]+: SYNTAX ERROR: invalid method call \(errors\.tpl\: line 10, pos 7\)
[^ ]+: SYNTAX ERROR: end with no begin \(errors\.tpl\: line 11, pos 5\)
[^ ]+: SYNTAX ERROR: invalid <if> syntax, only 2 or 3 arguments allowed \(errors\.tpl\: line 12, pos 3\)
[^ ]+: SYNTAX ERROR: invalid <inlcude> syntax, only 1 argument allowed \(errors\.tpl\: line 13, pos 5\)
