#!/usr/bin/perl

use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

plan tests => 11;

my $node = PostgreSQL::Test::Cluster->new('main');
$node->init;
$node->start;

$node->safe_psql('postgres', 'CREATE EXTENSION pg_array_math;');

# вспомогат. функция для проверки работы
# функции расширения внутри транзакции
sub test_in_transaction
{
    my ($query, $description) = @_;

    my $result = $node->safe_psql(
        'postgres',
        "BEGIN; $query ROLLBACK;"
    );

    is($result, 't', $description);
}

# 1. array_add
test_in_transaction(
    'SELECT array_add(ARRAY[1,2,3], ARRAY[4,5,6]) = ARRAY[5,7,9];',
    'array_add works inside a transaction'
);

# 2. array_sub
test_in_transaction(
    'SELECT array_sub(ARRAY[5,7,9], ARRAY[1,2,3]) = ARRAY[4,5,6];',
    'array_sub works inside a transaction'
);

# 3. array_mul
test_in_transaction(
    'SELECT array_mul(ARRAY[2,3], ARRAY[4,5]) = ARRAY[8,15];',
    'array_mul works inside a transaction'
);

# 4. array_div
test_in_transaction(
    'SELECT array_div(ARRAY[8,9], ARRAY[2,3]) = ARRAY[4,3];',
    'array_div works inside a transaction'
);

# 5. array_add_scalar
test_in_transaction(
    'SELECT array_add_scalar(ARRAY[1,2,3], 5) = ARRAY[6,7,8];',
    'array_add_scalar works inside a transaction'
);

# 6. array_mul_scalar
test_in_transaction(
    'SELECT array_mul_scalar(ARRAY[1,2,3], 3) = ARRAY[3,6,9];',
    'array_mul_scalar works inside a transaction'
);

# 7. array_power
test_in_transaction(
    'SELECT array_power(ARRAY[2,3,4], 2::numeric) = ARRAY[4,9,16];',
    'array_power works inside a transaction'
);

# 8. array_sqrt
test_in_transaction(
    'SELECT array_sqrt(ARRAY[4,9,16]) = ARRAY[2,3,4];',
    'array_sqrt works inside a transaction'
);

# 9. array_abs
test_in_transaction(
    'SELECT array_abs(ARRAY[-2,-5,3]) = ARRAY[2,5,3];',
    'array_abs works inside a transaction'
);

# 10. array_log
test_in_transaction(
    q{
        SELECT
            abs((array_log(ARRAY[1.0, exp(1.0)]::float8[]))[1]) < 1e-12
            AND
            abs((array_log(ARRAY[1.0, exp(1.0)]::float8[]))[2] - 1.0) < 1e-12;
    },
    'array_log works inside a transaction'
);

# 11. array_exp
test_in_transaction(
    q{
        SELECT
            abs((array_exp(ARRAY[0.0, 1.0]::float8[]))[1] - 1.0) < 1e-12
            AND
            abs((array_exp(ARRAY[0.0, 1.0]::float8[]))[2] - exp(1.0)) < 1e-12;
    },
    'array_exp works inside a transaction'
);

$node->stop;

