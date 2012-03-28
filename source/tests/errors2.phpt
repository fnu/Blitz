--TEST--
errors and warnings: execution
--FILE--
<?

error_reporting(E_ALL);
set_error_handler('my_error_handler');
function my_error_handler($errno, $errstr, $errfile, $errline) {
    echo $errstr."\n";
}

class B extends Blitz {}

class C extends Blitz {
    function test() {
        return "OK";
    }
}

$A = new Blitz();
$A->load('{{ test(); }}');
echo $A->parse();

$B = new Blitz();
$B->load('{{ test(); }}');
echo $B->parse();


$C = new C();
$C->load('{{ test(); }}');
echo $C->parse();


?>
--EXPECTREGEX--
[^ ]+: INTERNAL ERROR: calling user method \"test\" failed, check if this method exists
[^ ]+: INTERNAL ERROR: calling user method \"test\" failed, check if this method exists
OK
