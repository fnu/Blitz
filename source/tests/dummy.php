<?

ini_set('blitz.var_prefix', '@');
ini_set('blitz.node_open',  '<?blitz');
ini_set('blitz.node_close','?>');

$T = new Blitz();
$T->load("<?blitz @name ?>\n");

$T->set(array('name' => 'name_val'));

echo $T->parse();

?>
