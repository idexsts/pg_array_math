CREATE EXTENSION pg_array_math;

--тестовые запросы на функцию array_add

SELECT array_add(ARRAY[1, 2, 3], ARRAY[4, 5, 6]); --5,7,9
SELECT array_add(ARRAY[-1, -2, -3], ARRAY[4, 5, 6]); --3,3,3
SELECT array_add(ARRAY[0, 0, 0], ARRAY[0, 0, 0]); --0,0,0
SELECT array_add(
    ARRAY[1.5, 2.5]::double precision[],
    ARRAY[0.5, 1.5]::double precision[]
);--2,4
SELECT array_add(
    ARRAY[10000000000, 20000000000]::bigint[],
    ARRAY[30000000000, 40000000000]::bigint[]
);
SELECT array_add(
    ARRAY[1.123456789, 2.987654321]::numeric[],
    ARRAY[3.111111111, 4.222222222]::numeric[]
);
SELECT array_add(ARRAY[1, NULL, 3], ARRAY[4, 5, NULL]); --5,null,null
SELECT array_add(ARRAY[]::integer[], ARRAY[]::integer[]); --empty

--ошибки!
-- Переполнение INTEGER
SELECT array_add(ARRAY[2147483647], ARRAY[1]);
-- Переполнение BIGINT
SELECT array_add(
    ARRAY[9223372036854775807]::bigint[],
    ARRAY[1]::bigint[]
);
-- NaN
SELECT array_add(ARRAY['NaN'::double precision], ARRAY[1.0::double precision]);
-- Infinity
SELECT array_add(ARRAY['Infinity'::double precision], ARRAY[1.0::double precision]);
-- Переполнение DOUBLE PRECISION
SELECT array_add(ARRAY[1.7e308]::double precision[], ARRAY[1.7e308]::double precision[]);
-- Разное количество элементов
SELECT array_add(ARRAY[1, 2], ARRAY[1, 2, 3]);


-- array_sub

SELECT array_sub(ARRAY[10, 20, 30], ARRAY[1, 2, 3]); --9,18,27
SELECT array_sub(ARRAY[0, 0, 0], ARRAY[0, 0, 0]); --0,0,0
SELECT array_sub(
    ARRAY[5.5, 10.5]::double precision[],
    ARRAY[1.5, 2.5]::double precision[]
); --4,8
SELECT array_sub(
    ARRAY[50000000000, 60000000000]::bigint[],
    ARRAY[10000000000, 20000000000]::bigint[]
);
SELECT array_sub(
    ARRAY[5.75, 8.5]::numeric[],
    ARRAY[2.25, 3.5]::numeric[]
);
SELECT array_sub(ARRAY[10, NULL, 30], ARRAY[1, 2, NULL]); --9,null,null
SELECT array_sub(ARRAY[]::integer[], ARRAY[]::integer[]); --empty

--ошибки!
-- Переполнение BIGINT: максимальное значение - (-1)
SELECT array_sub(
    ARRAY[9223372036854775807]::bigint[],
    ARRAY[-1]::bigint[]
);
-- NaN
SELECT array_sub(
    ARRAY['NaN'::double precision],
    ARRAY[1.0::double precision]
);
-- Infinity
SELECT array_sub(
    ARRAY['Infinity'::double precision],
    ARRAY[1.0::double precision]
);
-- длина
SELECT array_sub(ARRAY[1, 2], ARRAY[1, 2, 3]);



-- array_mul
SELECT array_mul(ARRAY[2, 3, 4], ARRAY[5, 6, 7]);
SELECT array_mul(ARRAY[2, -3, 0], ARRAY[4, 2, 100]);
SELECT array_mul(
    ARRAY[100000]::bigint[],
    ARRAY[200000]::bigint[]
);
SELECT array_mul(
    ARRAY[1.5, 2.0]::double precision[],
    ARRAY[2.0, 3.0]::double precision[]
);
SELECT array_mul(
    ARRAY[1.25, 2.5]::numeric[],
    ARRAY[2.0, 3.0]::numeric[]
);
SELECT array_mul(ARRAY[2, NULL, 4], ARRAY[3, 5, NULL]);
SELECT array_mul(ARRAY[]::integer[], ARRAY[]::integer[]);

--ошибки!
-- Переполнение SMALLINT
SELECT array_mul(
    ARRAY[30000]::smallint[],
    ARRAY[2]::smallint[]
);
-- Переполнение INTEGER
SELECT array_mul(ARRAY[2147483647], ARRAY[2]);
-- Переполнение BIGINT
SELECT array_mul(
    ARRAY[9223372036854775807]::bigint[],
    ARRAY[2]::bigint[]
);
-- NaN
SELECT array_mul(
    ARRAY['NaN'::double precision],
    ARRAY[2.0::double precision]
);
-- Infinity
SELECT array_mul(
    ARRAY['Infinity'::double precision],
    ARRAY[2.0::double precision]
);
-- Переполнение DOUBLE PRECISION
SELECT array_mul(
    ARRAY[1.7e308]::double precision[],
    ARRAY[2.0]::double precision[]
);
-- Разное количество элементов
SELECT array_mul(ARRAY[1, 2], ARRAY[1, 2, 3]);


--array_div

SELECT array_div(ARRAY[8, 9, 10], ARRAY[2, 3, 2]);
SELECT array_div(ARRAY[10, -9, 20], ARRAY[2, 2, 5]);
SELECT array_div(
    ARRAY[10000000000]::bigint[],
    ARRAY[2]::bigint[]
);
SELECT array_div(
    ARRAY[5.0, 9.0]::double precision[],
    ARRAY[2.0, 3.0]::double precision[]
);
SELECT array_div(
    ARRAY[5.5, 9.0]::numeric[],
    ARRAY[2.0, 3.0]::numeric[]
);
SELECT array_div(ARRAY[10, NULL, 30], ARRAY[2, 5, NULL]);
SELECT array_div(ARRAY[]::integer[], ARRAY[]::integer[]);

--ошибки!
-- Деление INTEGER на ноль
SELECT array_div(ARRAY[10, 20], ARRAY[2, 0]);
-- Деление SMALLINT на ноль
SELECT array_div(
    ARRAY[10]::smallint[],
    ARRAY[0]::smallint[]
);
-- Деление BIGINT: минимальное значение на -1
SELECT array_div(
    ARRAY[-9223372036854775807 - 1]::bigint[],
    ARRAY[-1]::bigint[]
);
-- NaN
SELECT array_div(
    ARRAY['NaN'::double precision],
    ARRAY[2.0::double precision]
);
-- Infinity
SELECT array_div(
    ARRAY['Infinity'::double precision],
    ARRAY[2.0::double precision]
);
-- Переполнение DOUBLE PRECISION
SELECT array_div(
    ARRAY[1.7e308]::double precision[],
    ARRAY[0.1]::double precision[]
);
-- Разное количество элементов
SELECT array_div(ARRAY[1, 2], ARRAY[1, 2, 3]);
--деление numeric на 0 
SELECT array_div(
    ARRAY[10.0]::numeric[],
    ARRAY[0.0]::numeric[]
);


--array_add_scalar

SELECT array_add_scalar(ARRAY[1, -2, 0], 5);
SELECT array_add_scalar(
    ARRAY[10000000000, 20000000000]::bigint[],
    5000000000::bigint
);
SELECT array_add_scalar(
    ARRAY[1.5, 2.5, 3.0]::double precision[],
    0.5::double precision
);
SELECT array_add_scalar(
    ARRAY[1.25, 2.5]::numeric[],
    0.75::numeric
);
SELECT array_add_scalar(ARRAY[1, NULL, 3], 5);
SELECT array_add_scalar(ARRAY[]::integer[], 5);

--ошибки!
-- Переполнение SMALLINT
SELECT array_add_scalar(
    ARRAY[32767]::smallint[],
    1::smallint
);
-- Переполнение INTEGER
SELECT array_add_scalar(
    ARRAY[2147483647],
    1
);
-- Переполнение BIGINT
SELECT array_add_scalar(
    ARRAY[9223372036854775807]::bigint[],
    1::bigint
);
-- NaN
SELECT array_add_scalar(
    ARRAY[1.0]::double precision[],
    'NaN'::double precision
);
-- Infinity
SELECT array_add_scalar(
    ARRAY[1.0]::double precision[],
    'Infinity'::double precision
);
-- Переполнение DOUBLE PRECISION
SELECT array_add_scalar(
    ARRAY[1.7e308]::double precision[],
    1.7e308::double precision
);


--array_mul_scalar

SELECT array_mul_scalar(ARRAY[1, -2, 0], 5);
SELECT array_mul_scalar(
    ARRAY[100000, 200000]::bigint[],
    2::bigint
);
SELECT array_mul_scalar(
    ARRAY[1.5, 2.5, 3.0]::double precision[],
    2.0::double precision
);
SELECT array_mul_scalar(
    ARRAY[1.25, 2.5]::numeric[],
    2.0::numeric
);
SELECT array_mul_scalar(ARRAY[2, NULL, 4], 3);
SELECT array_mul_scalar(ARRAY[]::integer[], 3);

--ошибки!
-- Переполнение SMALLINT
SELECT array_mul_scalar(
    ARRAY[30000]::smallint[],
    2::smallint
);
-- Переполнение INTEGER
SELECT array_mul_scalar(
    ARRAY[2147483647],
    2
);
-- Переполнение BIGINT
SELECT array_mul_scalar(
    ARRAY[9223372036854775807]::bigint[],
    2::bigint
);
-- NaN
SELECT array_mul_scalar(
    ARRAY[1.0]::double precision[],
    'NaN'::double precision
);
-- Infinity
SELECT array_mul_scalar(
    ARRAY[1.0]::double precision[],
    'Infinity'::double precision
);
-- Переполнение DOUBLE PRECISION
SELECT array_mul_scalar(
    ARRAY[1.7e308]::double precision[],
    2.0::double precision
);

--array_power
SELECT array_power(ARRAY[2, 3, 4], 2::numeric);
SELECT array_power(
    ARRAY[2, 3, 5]::bigint[],
    3::numeric
);
SELECT array_power(
    ARRAY[2.0, 3.0, 4.0]::double precision[],
    2::numeric
);
SELECT array_power(
    ARRAY[1.5, 2.5, 3.0]::numeric[],
    2::numeric
);
SELECT array_power(
    ARRAY[4.0, 9.0]::double precision[],
    0.5::numeric
);
SELECT array_power(ARRAY[2, NULL, 4], 2::numeric);
SELECT array_power(ARRAY[]::integer[], 2::numeric);

--ошибки!
-- Отрицательная степень для INTEGER
SELECT array_power(ARRAY[2, 3], -2::numeric);
-- Дробная степень для INTEGER
SELECT array_power(ARRAY[2, 3], 1.5::numeric);
-- Переполнение SMALLINT
SELECT array_power(ARRAY[100]::smallint[], 3::numeric);
-- Переполнение INTEGER
SELECT array_power(ARRAY[100000]::integer[], 3::numeric);
-- Переполнение BIGINT
SELECT array_power(
    ARRAY[9223372036854775807]::bigint[],
    2::numeric
);
-- Ноль в отрицательной степени
SELECT array_power(
    ARRAY[0.0]::double precision[],
    -1::numeric
);
-- Отрицательное число в дробной степени (NaN)
SELECT array_power(
    ARRAY[-4.0]::double precision[],
    0.5::numeric
);
-- Переполнение DOUBLE PRECISION
SELECT array_power(
    ARRAY[1e200]::double precision[],
    2::numeric
);
-- Infinity во входном массиве
SELECT array_power(
    ARRAY['Infinity'::double precision],
    2::numeric
);


--array_abs
SELECT array_abs(ARRAY[-5, 0, 10]);
SELECT array_abs(ARRAY[-10000000000, 0, 20000000000]::bigint[]);
SELECT array_abs(ARRAY[-1.5, 0.0, 2.5]::double precision[]);
SELECT array_abs(ARRAY[-1.25, 0, 2.75]::numeric[]);
SELECT array_abs(ARRAY[-5, NULL, 10]);
SELECT array_abs(ARRAY[]::integer[]);

--ошибки!
-- Переполнение SMALLINT: модуль минимального значения
SELECT array_abs(ARRAY[-32768]::smallint[]);
-- Переполнение INTEGER
SELECT array_abs(ARRAY[-2147483648]);
-- Переполнение BIGINT
SELECT array_abs(ARRAY[-9223372036854775807 - 1]::bigint[]);
-- NaN
SELECT array_abs(ARRAY['NaN'::double precision]);
-- Infinity
SELECT array_abs(ARRAY['Infinity'::double precision]);


--array_log
SELECT array_log(ARRAY[1.0, 2.7182818, 10.0]::real[]);
SELECT array_log(ARRAY[1.0, 2.718281828, 10.0]::double precision[]);
SELECT array_log(ARRAY[1.0, 2.718281828, 10.0]::numeric[]);
SELECT array_log(ARRAY[1.0, NULL, 10.0]::double precision[]);
SELECT array_log(ARRAY[]::double precision[]);

--ошибки!
-- INTEGER не поддерживается
SELECT array_log(ARRAY[1, 2, 3]);
-- Ноль
SELECT array_log(ARRAY[0.0]::double precision[]);
-- Отрицательное число
SELECT array_log(ARRAY[-1.0]::double precision[]);
-- NaN
SELECT array_log(ARRAY['NaN'::double precision]);
-- Infinity
SELECT array_log(ARRAY['Infinity'::double precision]);
-- NUMERIC: отрицательное число
SELECT array_log(ARRAY[-1.0]::numeric[]);


--array_exp

SELECT array_exp(ARRAY[0.0, 1.0, 2.0]::real[]);
SELECT array_exp(ARRAY[0.0, 1.0, -1.0]::double precision[]);
SELECT array_exp(ARRAY[0.0, 1.0, -1.0]::numeric[]);
SELECT array_exp(ARRAY[0.0, NULL, 2.0]::double precision[]);
SELECT array_exp(ARRAY[]::double precision[]);

--ошибки!
-- INTEGER не поддерживается
SELECT array_exp(ARRAY[1, 2, 3]);
-- NaN
SELECT array_exp(ARRAY['NaN'::double precision]);
-- Infinity
SELECT array_exp(ARRAY['Infinity'::double precision]);
-- Переполнение DOUBLE PRECISION
SELECT array_exp(ARRAY[1000.0]::double precision[]);
-- Переполнение REAL
SELECT array_exp(ARRAY[100.0]::real[]);


--большое количество элементов
SELECT cardinality(
    array_add(
        ARRAY(SELECT generate_series(1, 2000000)::bigint),
        ARRAY(SELECT generate_series(1, 2000000)::bigint)
    )
);
SELECT cardinality(
    array_add(
        ARRAY(SELECT generate_series(1, 5000000)),
        ARRAY(SELECT generate_series(1, 5000000))
    )
);