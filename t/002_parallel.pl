#!/usr/bin/perl

use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::BackgroundPsql;
use Test::More;

plan tests => 4;

my $node = PostgreSQL::Test::Cluster->new('main');
$node->init;
$node->start;

$node->safe_psql('postgres', 'CREATE EXTENSION pg_array_math;');

# создаём два независимых соединения
my $session1 = $node->background_psql('postgres');
my $session2 = $node->background_psql('postgres');

# параллельно вызвали add
$session1->query_until(
    qr/ADD1_START/,
    "\\echo ADD1_START\nSELECT pg_sleep(1);\n"
    . "SELECT array_add(ARRAY[1,2,3], ARRAY[4,5,6]);\n"
    . "\\echo ADD1_DONE\n"
);

$session2->query_until(
    qr/ADD2_START/,
    "\\echo ADD2_START\nSELECT pg_sleep(1);\n"
    . "SELECT array_add(ARRAY[10,20], ARRAY[30,40]);\n"
    . "\\echo ADD2_DONE\n"
);

my $add1 = $session1->query_until(qr/ADD1_DONE/, "\\echo WAIT1\n");
my $add2 = $session2->query_until(qr/ADD2_DONE/, "\\echo WAIT2\n");

like($add1, qr/\{5,7,9\}/, 'array_add returns correct result in session 1');
like($add2, qr/\{40,60\}/, 'array_add returns correct result in session 2');

# также и с mul 
$session1->query_until(
    qr/MUL1_START/,
    "\\echo MUL1_START\nSELECT pg_sleep(1);\n"
    . "SELECT array_mul(ARRAY[2,3], ARRAY[4,5]);\n"
    . "\\echo MUL1_DONE\n"
);

$session2->query_until(
    qr/MUL2_START/,
    "\\echo MUL2_START\nSELECT pg_sleep(1);\n"
    . "SELECT array_mul(ARRAY[3,4], ARRAY[5,6]);\n"
    . "\\echo MUL2_DONE\n"
);

my $mul1 = $session1->query_until(qr/MUL1_DONE/, "\\echo WAIT3\n");
my $mul2 = $session2->query_until(qr/MUL2_DONE/, "\\echo WAIT4\n");

like($mul1, qr/\{8,15\}/, 'array_mul returns correct result in session 1');
like($mul2, qr/\{15,24\}/, 'array_mul returns correct result in session 2');

$session1->quit;
$session2->quit;

$node->stop;
