--TEST--
mix #2
--FILE--
<?

$body = "{{ begin test }} hello, {{ \$name }}  \n{{ end }}";
$T = new Blitz();
$T->load($body);
$T->iterate("/test");
$T->context("/test");
$T->set(array("name" => "john"));
echo $T->parse();

?>
--EXPECT--
hello, john
