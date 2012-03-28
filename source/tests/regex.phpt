--TEST--
expect regexp test
--FILE--
<?php

echo "test 2006";

?>
--EXPECTREGEX--
test \d{1,10}
