--TEST--
complex fetch
--FILE--
<?

$T = new Blitz('fetch_cmplx.tpl');

for($i=0; $i<10; $i++) {
    $T->context('/root/a');
    $T->set(array('x' => 'x_value#'.$i, 'i'=> $i));
    if($i%2) {
        $T->context('b');
        $T->set(array('y' => 'y_value#'.$i));
    }
    echo "FETCH: /root/a\n";
    echo $T->fetch('/root/a')."\n";
}

echo "\n\n";

$T->set_global(array(
    'x' => 'x_global', 'y'=> 'y_global'
));

$i = 0;
$i_params = NULL;

// fetch doesn't make "iterate": that's why you will not see even iterations of /root/a :)
$paths = array('/root/a', '/root', '/', '/root/a/b'); 
foreach($paths as $i_path) {
    $i_params = ($i++ % 2) ? array('x' => 'x_value#'.$i) : NULL;
    echo "FETCH: ".$i_path."\n";
    echo $T->fetch($i_path, $i_params)."\n";
}

?>
--EXPECT--
FETCH: /root/a

A-begin(0)  ; x=x_value#0, y= A-end

FETCH: /root/a

A-begin(1)  B-begin; y=y_value#1 B-end ; x=x_value#1, y= A-end

FETCH: /root/a

A-begin(2)  B-begin; y=y_value#1 B-end ; x=x_value#2, y= A-end

FETCH: /root/a

A-begin(3)  B-begin; y=y_value#3 B-end ; x=x_value#3, y= A-end

FETCH: /root/a

A-begin(4)  B-begin; y=y_value#3 B-end ; x=x_value#4, y= A-end

FETCH: /root/a

A-begin(5)  B-begin; y=y_value#5 B-end ; x=x_value#5, y= A-end

FETCH: /root/a

A-begin(6)  B-begin; y=y_value#5 B-end ; x=x_value#6, y= A-end

FETCH: /root/a

A-begin(7)  B-begin; y=y_value#7 B-end ; x=x_value#7, y= A-end

FETCH: /root/a

A-begin(8)  B-begin; y=y_value#7 B-end ; x=x_value#8, y= A-end

FETCH: /root/a

A-begin(9)  B-begin; y=y_value#9 B-end ; x=x_value#9, y= A-end



FETCH: /root/a

A-begin(9)  B-begin; y=y_value#9 B-end ; x=x_value#9, y=y_global A-end

FETCH: /root

this is root

A-begin(9)  B-begin; y=y_value#9 B-end ; x=x_value#9, y=y_global A-end

end of root

FETCH: /
complex fetch example

this is root

A-begin(9)  B-begin; y=y_value#9 B-end ; x=x_value#9, y=y_global A-end

end of root


FETCH: /root/a/b
 B-begin; y=y_value#9 B-end
