#!/bin/bash

cd tests;
TEST_PHP_EXECUTABLE=/usr/local/bin/php php /distr/php/run-tests.php -m ./
cd ..;
