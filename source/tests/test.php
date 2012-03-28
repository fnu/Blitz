<?

$body = <<<BODY
this is a test: {{ t() }}
this is a test: {{ t }}
this is a test: {{ t; }}
BODY;

class View extends Blitz {
    function t() {
        return "function was called";
    } 
}

$T = new View();
//$T->load($body);
$T->load(NULL);

echo $T->parse();
echo "\n";

?>
