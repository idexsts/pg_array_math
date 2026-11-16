#!/usr/bin/perl

use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

plan tests => 4;

my $node = PostgreSQL::Test::Cluster->new('main');
$node->init;
$node->start;

$node->safe_psql('postgres', 'CREATE EXTENSION pg_array_math;');

# массив из 1000 элементов: от 1 до 1000
my $large_array = 'ARRAY[' . join(',', (1..1000)) . ']';

# 1.проверяем array_add
my $add_result = $node->safe_psql(
    'postgres',
    "SELECT cardinality(array_add($large_array, $large_array));"
);

is($add_result, '1000', 'array_add processes 1000 elements');

my $add_check = $node->safe_psql(
    'postgres',
    "SELECT (array_add($large_array, $large_array))[1000] = 2000;" #здесь смотрим только какой получился последний
                                                                 #элемент , ожидаем что будет 1000+1000=2000
);

is($add_check, 't', 'array_add returns correct values');

# 2.проверяем array_mul
my $mul_result = $node->safe_psql(
    'postgres',
    "SELECT cardinality(array_mul($large_array, $large_array));"
);

is($mul_result, '1000', 'array_mul processes 1000 elements');

my $mul_check = $node->safe_psql(
    'postgres',
    "SELECT (array_mul($large_array, $large_array))[1000] = 1000000;" #тут также, только с умножением
);

is($mul_check, 't', 'array_mul returns correct values');

$node->stop;
