--TEST--
errors and warnings
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
--EXPECT--
blitz(): SYNTAX ERROR: invalid method call (errors.tpl: line 2, pos 7)
blitz(): SYNTAX ERROR: invalid method call (errors.tpl: line 4, pos 3)
blitz(): SYNTAX ERROR: invalid method call (errors.tpl: line 5, pos 5)
blitz(): SYNTAX ERROR: invalid method call (errors.tpl: line 6, pos 7)
blitz(): SYNTAX ERROR: close tag without open (errors.tpl: line 7, pos 3)
blitz(): SYNTAX ERROR: open tag without close (errors.tpl: line 8, pos 3)
blitz(): SYNTAX ERROR: invalid method call (errors.tpl: line 9, pos 9)
blitz(): SYNTAX ERROR: invalid method call (errors.tpl: line 10, pos 7)
blitz(): SYNTAX ERROR: end with no begin (errors.tpl: line 11, pos 5)
blitz(): SYNTAX ERROR: invalid <if> syntax, only 2 or 3 arguments allowed (errors.tpl: line 12, pos 3)
blitz(): SYNTAX ERROR: invalid <inlcude> syntax, only 1 argument allowed (errors.tpl: line 13, pos 5)
