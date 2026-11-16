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

# вспомогательная функция для проверки возникновения ошибки в нужном случае
sub check_error
{
    my ($sql, $test_name) = @_;

    my $success = eval
    {
        $node->safe_psql('postgres', $sql);
        1;
    };

    my $error = $@;

    ok(!$success, $test_name);

    diag("Unexpected error: $error") if $success;
}

# 1.массивы разной длины
check_error(
    'SELECT array_add(ARRAY[1, 2], ARRAY[1, 2, 3]);',
    'array_add rejects arrays with different lengths'
);

# 2.деление на ноль
check_error(
    'SELECT array_div(ARRAY[10, 20], ARRAY[2, 0]);',
    'array_div rejects division by zero'
);

# 3.квадратный корень из отрицательного числа
check_error(
    'SELECT array_sqrt(ARRAY[-1, 4]);',
    'array_sqrt rejects negative numbers'
);

# 4.неподдерживаемый тип массива
check_error(
    "SELECT array_add(ARRAY['a', 'b'], ARRAY['c', 'd']);",
    'array_add rejects unsupported element types'
);

$node->stop;
