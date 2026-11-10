#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_type_d.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/fmgrprotos.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/*Вспомогательные функции(!)*/

/*Проверка поддерживаемого типа элементов*/
static void check_supported_type(Oid elemtype)
{
    if (elemtype != INT2OID &&
        elemtype != INT4OID &&
        elemtype != INT8OID &&
        elemtype != FLOAT4OID &&
        elemtype != FLOAT8OID &&
        elemtype != NUMERICOID)
    {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("unsupported array element type")));
    }
}

/*Разбор входного массива*/
static void deconstruct_input_array(
    ArrayType *arr,
    Datum **values,
    bool **nulls,
    int *count,
    Oid *elemtype)
{
    int16 typlen;
    bool typbyval;
    char typalign;

    *elemtype = ARR_ELEMTYPE(arr);

    check_supported_type(*elemtype);

    if (ARR_NDIM(arr) > 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("only one-dimensional arrays are supported")));
    }

    get_typlenbyvalalign(
        *elemtype,
        &typlen,
        &typbyval,
        &typalign
    );

    deconstruct_array(
        arr,
        *elemtype,
        typlen,
        typbyval,
        typalign,
        values,
        nulls,
        count
    );
}

/*Создание результата с сохранением нижней границы массива*/
static ArrayType *construct_result_array(
    Datum *values,
    bool *nulls,
    int count,
    Oid elemtype,
    int lower_bound)
{
    int dims[1];
    int lbs[1];

    int16 typlen;
    bool typbyval;
    char typalign;

    if (count == 0)
        return construct_empty_array(elemtype);

    get_typlenbyvalalign(
        elemtype,
        &typlen,
        &typbyval,
        &typalign
    );

    dims[0] = count;
    lbs[0] = lower_bound;

    return construct_md_array(
        values,
        nulls,
        1,
        dims,
        lbs,
        elemtype,
        typlen,
        typbyval,
        typalign
    );
}

/*Проверка переполнения при умножении bigint*/
static bool int8_mul_overflow(int64 a, int64 b, int64 *result)
{
    if (a == 0 || b == 0)
    {
        *result = 0;
        return false;
    }

    if (a == -1 && b == PG_INT64_MIN)
        return true;

    if (b == -1 && a == PG_INT64_MIN)
        return true;

    if (a > 0)
    {
        if (b > 0)
        {
            if (a > PG_INT64_MAX / b)
                return true;
        }
        else
        {
            if (b < PG_INT64_MIN / a)
                return true;
        }
    }
    else
    {
        if (b > 0)
        {
            if (a < PG_INT64_MIN / b)
                return true;
        }
        else
        {
            if (a < PG_INT64_MAX / b)
                return true;
        }
    }

    *result = a * b;
    return false;
}

/* 
Целочисленный квадратный корень.

Возвращает целую часть корня.
Например: sqrt(10) = 3.
 */
  static uint64 integer_sqrt(uint64 n)
{
    uint64 root = (uint64)sqrtl((long double)n);

    //Коррекция результата из-за округления 
    if (root > 0 && root > n / root)
        root--;

    if (root < 4294967295ULL &&
        root + 1 <= n / (root + 1))
        root++;

    return root;
}

//Возведение в степень быстрое
static bool int8_pow_overflow(int64 base, uint64 exponent, int64 *result)
{
    int64 res = 1;
    int64 factor = base;
    int64 temp;

    while (exponent > 0)
    {
        if (exponent & 1)
        {
            if (int8_mul_overflow(res, factor, &temp))
                return true;

            res = temp;
        }

        exponent >>= 1;

        if (exponent > 0)
        {
            if (int8_mul_overflow(factor, factor, &temp))
                return true;

            factor = temp;
        }
    }

    *result = res;
    return false;
}

//Перевод нумерика в инт для целочисленного возведения в степень
static int64 get_integer_exponent(Datum exponent)
{
    int64 exp;
    Datum converted;
    int cmp;

    exp = DatumGetInt64(
        DirectFunctionCall1(numeric_int8, exponent)
    );

    converted = DirectFunctionCall1(
        int8_numeric,
        Int64GetDatum(exp)
    );

    cmp = DatumGetInt32(
        DirectFunctionCall2(numeric_cmp, exponent, converted)
    );

    if (cmp != 0 || exp < 0)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("integer array exponent must be a non-negative integer")));

    return exp;
}



/*Функции расширения pg_array_math(!)*/

/*
(1) array_add(anyarray, anyarray)-->anyarray

Складывает элементы двух массивов.
Тип элементов массивов должен совпадать (т.к. заданная сигнатура с полиморфным типом 
и PostgreSQL отклонит запрос с разными ещё на этапе разрешения типов функции).
 */
PG_FUNCTION_INFO_V1(array_add);

Datum array_add(PG_FUNCTION_ARGS)
{
    ArrayType *arr1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *arr2 = PG_GETARG_ARRAYTYPE_P(1);

    Datum *values1;
    Datum *values2;
    bool *nulls1;
    bool *nulls2;

    int count1;
    int count2;

    Oid type1;
    Oid type2;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr1, &values1, &nulls1, &count1, &type1
    );

    deconstruct_input_array(
        arr2, &values2, &nulls2, &count2, &type2
    );


    if (count1 != count2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("array lengths must match")));
    }

    result = palloc(sizeof(Datum) * count1);
    result_nulls = palloc(sizeof(bool) * count1);

    lower_bound = ARR_NDIM(arr1) == 1 ? ARR_LBOUND(arr1)[0] : 1; //это чтобы понимать, с какого номера у нас массив резульирующий будет начинаться

    for (int i = 0; i < count1; i++)
    {
        if (nulls1[i] || nulls2[i])       //проверяем на NULL элементы, оставляем их в нужных позициях
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        //SMALLINT 
        if (type1 == INT2OID)
        {
            int32 a = DatumGetInt16(values1[i]); 
            int32 b = DatumGetInt16(values2[i]);
            int32 sum = a + b;  //int32 нужен чтобы помещать, например, сумму двух максимальных int2 и проверять ее потом на переполнение

            if (sum < SHRT_MIN || sum > SHRT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint addition overflow")));

            result[i] = Int16GetDatum((int16) sum);
        }

        //INTEGER 
        else if (type1 == INT4OID)
        {
            int64 a = DatumGetInt32(values1[i]);
            int64 b = DatumGetInt32(values2[i]);
            int64 sum = a + b; //та же история, что с int2, только с int4 и берем целое число побольше

            if (sum < INT_MIN || sum > INT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer addition overflow")));

            result[i] = Int32GetDatum((int32) sum);
        }

        //BIGINT 
        else if (type1 == INT8OID)
        {
            int64 a = DatumGetInt64(values1[i]); //здесь уже проверяем переполнение до сложения
            int64 b = DatumGetInt64(values2[i]);

            if ((b > 0 && a > PG_INT64_MAX - b) ||
                (b < 0 && a < PG_INT64_MIN - b))
            {
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint addition overflow")));
            }

            result[i] = Int64GetDatum(a + b);
        }

        //REAL
        else if (type1 == FLOAT4OID)
        {
            float4 a = DatumGetFloat4(values1[i]);
            float4 b = DatumGetFloat4(values2[i]);
            float4 sum;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("NaN values are not allowed")));
            

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Infinity values are not allowed")));


            sum = a + b;

            if (isinf(sum) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real addition overflow")));

            result[i] = Float4GetDatum(sum);
        }

        //DOUBLE PRECISION
        else if (type1 == FLOAT8OID)
        {
            float8 a = DatumGetFloat8(values1[i]);
            float8 b = DatumGetFloat8(values2[i]);
            float8 sum;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("NaN values are not allowed")));
            

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Infinity values are not allowed")));


            sum = a + b;

            if (isinf(sum) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision addition overflow")));

            result[i] = Float8GetDatum(sum);
        }

        //NUMERIC 
        else if (type1 == NUMERICOID)
        {
            result[i] = DirectFunctionCall2(
                numeric_add,
                values1[i],
                values2[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count1,
            type1,
            lower_bound
        )
    );
}


/*
(2) array_sub(anyarray, anyarray)-->anyarray

Вычитает элементы второго массива из элементов первого.
 */
PG_FUNCTION_INFO_V1(array_sub);

Datum array_sub(PG_FUNCTION_ARGS)
{
    ArrayType *arr1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *arr2 = PG_GETARG_ARRAYTYPE_P(1);

    Datum *values1;
    Datum *values2;
    bool *nulls1;
    bool *nulls2;

    int count1;
    int count2;

    Oid type1;
    Oid type2;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr1, &values1, &nulls1, &count1, &type1
    );

    deconstruct_input_array(
        arr2, &values2, &nulls2, &count2, &type2
    );


    if (count1 != count2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("array lengths must match")));
    }

    result = palloc(sizeof(Datum) * count1);
    result_nulls = palloc(sizeof(bool) * count1);

    lower_bound = ARR_NDIM(arr1) == 1 ? ARR_LBOUND(arr1)[0] : 1;

    for (int i = 0; i < count1; i++)
    {
        if (nulls1[i] || nulls2[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        
        if (type1 == INT2OID)
        {
            int32 a = DatumGetInt16(values1[i]);
            int32 b = DatumGetInt16(values2[i]);
            int32 diff = a - b;

            if (diff < SHRT_MIN || diff > SHRT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint subtraction overflow")));

            result[i] = Int16GetDatum((int16) diff);
        }

        
        else if (type1 == INT4OID)
        {
            int64 a = DatumGetInt32(values1[i]);
            int64 b = DatumGetInt32(values2[i]);
            int64 diff = a - b;

            if (diff < INT_MIN || diff > INT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer subtraction overflow")));

            result[i] = Int32GetDatum((int32) diff);
        }

        
        else if (type1 == INT8OID)
        {
            int64 a = DatumGetInt64(values1[i]);
            int64 b = DatumGetInt64(values2[i]);

            if ((b < 0 && a > PG_INT64_MAX + b) ||
                (b > 0 && a < PG_INT64_MIN + b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint subtraction overflow")));

            result[i] = Int64GetDatum(a - b);
        }

    
        else if (type1 == FLOAT4OID)
        {
            float4 a = DatumGetFloat4(values1[i]);
            float4 b = DatumGetFloat4(values2[i]);
            float4 diff;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("NaN values are not allowed")));
            

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Infinity values are not allowed")));

            diff = a - b;

            if (isinf(diff) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real subtraction overflow")));

            result[i] = Float4GetDatum(diff);
        }

        
        else if (type1 == FLOAT8OID)
        {
            float8 a = DatumGetFloat8(values1[i]);
            float8 b = DatumGetFloat8(values2[i]);
            float8 diff;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("NaN values are not allowed")));
            

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Infinity values are not allowed")));

            diff = a - b;

            if (isinf(diff) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision subtraction overflow")));

            result[i] = Float8GetDatum(diff);
        }

      
        else if (type1 == NUMERICOID)
        {
            result[i] = DirectFunctionCall2(
                numeric_sub,
                values1[i],
                values2[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count1,
            type1,
            lower_bound
        )
    );
}



/*
(3) array_mul(anyarray, anyarray)-->anyarray

Умножает элементы двух массивов.
 */
PG_FUNCTION_INFO_V1(array_mul);

Datum array_mul(PG_FUNCTION_ARGS)
{
    ArrayType *arr1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *arr2 = PG_GETARG_ARRAYTYPE_P(1);

    Datum *values1;
    Datum *values2;
    bool *nulls1;
    bool *nulls2;

    int count1;
    int count2;

    Oid type1;
    Oid type2;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr1, &values1, &nulls1, &count1, &type1
    );

    deconstruct_input_array(
        arr2, &values2, &nulls2, &count2, &type2
    );


    if (count1 != count2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("array lengths must match")));
    }

    result = palloc(sizeof(Datum) * count1);
    result_nulls = palloc(sizeof(bool) * count1);

    lower_bound = ARR_NDIM(arr1) == 1 ? ARR_LBOUND(arr1)[0] : 1;

    for (int i = 0; i < count1; i++)
    {
        if (nulls1[i] || nulls2[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        
        if (type1 == INT2OID)
        {
            int32 a = DatumGetInt16(values1[i]);
            int32 b = DatumGetInt16(values2[i]);
            int32 product = a * b;

            if (product < SHRT_MIN || product > SHRT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint multiplication overflow")));

            result[i] = Int16GetDatum((int16) product);
        }

        
        else if (type1 == INT4OID)
        {
            int64 a = DatumGetInt32(values1[i]);
            int64 b = DatumGetInt32(values2[i]);
            int64 product = a * b;

            if (product < INT_MIN || product > INT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer multiplication overflow")));

            result[i] = Int32GetDatum((int32) product);
        }

        
        else if (type1 == INT8OID)
        {
            int64 a = DatumGetInt64(values1[i]);
            int64 b = DatumGetInt64(values2[i]);
            int64 product;

            if (int8_mul_overflow(a, b, &product))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint multiplication overflow")));

            result[i] = Int64GetDatum(product);
        }

        
        else if (type1 == FLOAT4OID)
        {
            float4 a = DatumGetFloat4(values1[i]);
            float4 b = DatumGetFloat4(values2[i]);
            float4 product;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("NaN values are not allowed")));
            

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Infinity values are not allowed")));

            product = a * b;


            if (isinf(product) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real multiplication overflow")));

            result[i] = Float4GetDatum(product);
        }

        
        else if (type1 == FLOAT8OID)
        {
            float8 a = DatumGetFloat8(values1[i]);
            float8 b = DatumGetFloat8(values2[i]);
            float8 product;
            
            if (isnan(a) || isnan(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("NaN values are not allowed")));
            

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Infinity values are not allowed")));

            product = a * b;


            if (isinf(product) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision multiplication overflow")));

            result[i] = Float8GetDatum(product);
        }

        
        else if (type1 == NUMERICOID)
        {
            result[i] = DirectFunctionCall2(
                numeric_mul,
                values1[i],
                values2[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count1,
            type1,
            lower_bound
        )
    );
}


/*
(4) array_div(anyarray, anyarray) --> anyarray

Делит элементы первого массива на соответствующие элементы второго массива.
*/

PG_FUNCTION_INFO_V1(array_div);

Datum array_div(PG_FUNCTION_ARGS)
{
    ArrayType *arr1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *arr2 = PG_GETARG_ARRAYTYPE_P(1);

    Datum *values1;
    Datum *values2;
    bool *nulls1;
    bool *nulls2;

    int count1;
    int count2;

    Oid type1;
    Oid type2;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr1, &values1, &nulls1, &count1, &type1
    );

    deconstruct_input_array(
        arr2, &values2, &nulls2, &count2, &type2
    );


    if (count1 != count2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("array lengths must match")));
    }

    result = palloc(sizeof(Datum) * count1);
    result_nulls = palloc(sizeof(bool) * count1);

    lower_bound = ARR_NDIM(arr1) == 1 ? ARR_LBOUND(arr1)[0] : 1;

    for (int i = 0; i < count1; i++)
    {
        if (nulls1[i] || nulls2[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        
        if (type1 == INT2OID)
        {
            int32 a = DatumGetInt16(values1[i]);
            int32 b = DatumGetInt16(values2[i]);
            int32 quotient;

            if (b == 0)
                ereport(ERROR,
                        (errcode(ERRCODE_DIVISION_BY_ZERO),
                         errmsg("smallint division by zero")));

            quotient = a / b;

            if (quotient < SHRT_MIN || quotient > SHRT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint division overflow")));

            result[i] = Int16GetDatum((int16) quotient);
        }

        
        else if (type1 == INT4OID)
        {
            int64 a = DatumGetInt32(values1[i]);
            int64 b = DatumGetInt32(values2[i]);
            int64 quotient;

            if (b == 0)
                ereport(ERROR,
                        (errcode(ERRCODE_DIVISION_BY_ZERO),
                         errmsg("integer division by zero")));

            quotient = a / b;

            if (quotient < INT_MIN || quotient > INT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer division overflow")));

            result[i] = Int32GetDatum((int32) quotient);
        }

        
        else if (type1 == INT8OID)
        {
            int64 a = DatumGetInt64(values1[i]);
            int64 b = DatumGetInt64(values2[i]);
            int64 quotient;

            if (b == 0)
                ereport(ERROR,
                        (errcode(ERRCODE_DIVISION_BY_ZERO),
                         errmsg("bigint division by zero")));

            //Особый случай: минимальное значение BIGINT / -1
            if (a == PG_INT64_MIN && b == -1)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint division overflow")));

            quotient = a / b;

            result[i] = Int64GetDatum(quotient);
        }

        
        else if (type1 == FLOAT4OID)
        {
            float4 a = DatumGetFloat4(values1[i]);
            float4 b = DatumGetFloat4(values2[i]);
            float4 quotient;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("NaN values are not allowed")));

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Infinity values are not allowed")));

            if (b == 0.0f)
                ereport(ERROR,
                        (errcode(ERRCODE_DIVISION_BY_ZERO),
                         errmsg("real division by zero")));

            quotient = a / b;

            if (isinf(quotient) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real division overflow")));

            result[i] = Float4GetDatum(quotient);
        }

        
        else if (type1 == FLOAT8OID)
        {
            float8 a = DatumGetFloat8(values1[i]);
            float8 b = DatumGetFloat8(values2[i]);
            float8 quotient;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("NaN values are not allowed")));

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Infinity values are not allowed")));

            if (b == 0.0)
                ereport(ERROR,
                        (errcode(ERRCODE_DIVISION_BY_ZERO),
                         errmsg("double precision division by zero")));

            quotient = a / b;

            if (isinf(quotient) && isfinite(a) && isfinite(b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision division overflow")));

            result[i] = Float8GetDatum(quotient);
        }

        
        else if (type1 == NUMERICOID)
        {
            result[i] = DirectFunctionCall2(
                numeric_div,
                values1[i],
                values2[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count1,
            type1,
            lower_bound
        )
    );
}


/*
(5) array_add_scalar(anyarray, anyelement) --> anyarray

Прибавляет скалярное значение к каждому элементу массива.
*/
PG_FUNCTION_INFO_V1(array_add_scalar);

Datum array_add_scalar(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);
    Datum scalar = PG_GETARG_DATUM(1);

    Datum *values;
    bool *nulls;
    int count;
    Oid type;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr, &values, &nulls, &count, &type
    );

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1 ? ARR_LBOUND(arr)[0] : 1;

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        if (type == INT2OID)
        {
            int32 a = DatumGetInt16(values[i]);
            int32 b = DatumGetInt16(scalar);

            int32 sum = a + b;

            if (sum < SHRT_MIN || sum > SHRT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint addition overflow")));

            result[i] = Int16GetDatum((int16) sum);
        }


        else if (type == INT4OID)
        {
            int64 a = DatumGetInt32(values[i]);
            int64 b = DatumGetInt32(scalar);

            int64 sum = a + b;

            if (sum < INT_MIN || sum > INT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer addition overflow")));

            result[i] = Int32GetDatum((int32) sum);
        }

        
        else if (type == INT8OID)
        {
            int64 a = DatumGetInt64(values[i]);
            int64 b = DatumGetInt64(scalar);
            int64 sum;

            if ((b > 0 && a > PG_INT64_MAX - b) ||
                (b < 0 && a < PG_INT64_MIN - b))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint addition overflow")));

            sum = a + b;

            result[i] = Int64GetDatum(sum);
        }

        
        else if (type == FLOAT4OID)
        {
            float4 a = DatumGetFloat4(values[i]);
            float4 b = DatumGetFloat4(scalar);
            float4 sum;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("NaN values are not allowed")));

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Infinity values are not allowed")));

            sum = a + b;

            if (isinf(sum))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real addition overflow")));

            result[i] = Float4GetDatum(sum);
        }

        
        else if (type == FLOAT8OID)
        {
            float8 a = DatumGetFloat8(values[i]);
            float8 b = DatumGetFloat8(scalar);
            float8 sum;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("NaN values are not allowed")));

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Infinity values are not allowed")));

            sum = a + b;

            if (isinf(sum))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision addition overflow")));

            result[i] = Float8GetDatum(sum);
        }

    
        else if (type == NUMERICOID)
        {
            result[i] = DirectFunctionCall2(
                numeric_add,
                values[i],
                scalar
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            type,
            lower_bound
        )
    );
}


/*
(6) array_mul_scalar(anyarray, anyelement) --> anyarray

Умножает каждый элемент массива на скалярное значение.
*/
PG_FUNCTION_INFO_V1(array_mul_scalar);

Datum array_mul_scalar(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);
    Datum scalar = PG_GETARG_DATUM(1);

    Datum *values;
    bool *nulls;
    int count;
    Oid type;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr, &values, &nulls, &count, &type
    );

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1 ? ARR_LBOUND(arr)[0] : 1;

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        
        if (type == INT2OID)
        {
            int32 a = DatumGetInt16(values[i]);
            int32 b = DatumGetInt16(scalar);

            int32 product = a * b;

            if (product < SHRT_MIN || product > SHRT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint multiplication overflow")));

            result[i] = Int16GetDatum((int16) product);
        }

        
        else if (type == INT4OID)
        {
            int64 a = DatumGetInt32(values[i]);
            int64 b = DatumGetInt32(scalar);

            int64 product = a * b;

            if (product < INT_MIN || product > INT_MAX)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer multiplication overflow")));

            result[i] = Int32GetDatum((int32) product);
        }

    
        else if (type == INT8OID)
        {
            int64 a = DatumGetInt64(values[i]);
            int64 b = DatumGetInt64(scalar);
            int64 product;

            if (int8_mul_overflow(a, b, &product))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint multiplication overflow")));

            result[i] = Int64GetDatum(product);
        }

        
        else if (type == FLOAT4OID)
        {
            float4 a = DatumGetFloat4(values[i]);
            float4 b = DatumGetFloat4(scalar);
            float4 product;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("NaN values are not allowed")));

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Infinity values are not allowed")));

            product = a * b;

            if (isinf(product))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real multiplication overflow")));

            result[i] = Float4GetDatum(product);
        }

        
        else if (type == FLOAT8OID)
        {
            float8 a = DatumGetFloat8(values[i]);
            float8 b = DatumGetFloat8(scalar);
            float8 product;

            if (isnan(a) || isnan(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("NaN values are not allowed")));

            if (isinf(a) || isinf(b))
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Infinity values are not allowed")));

            product = a * b;

            if (isinf(product))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision multiplication overflow")));

            result[i] = Float8GetDatum(product);
        }

        
        else if (type == NUMERICOID)
        {
            result[i] = DirectFunctionCall2(
                numeric_mul,
                values[i],
                scalar
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            type,
            lower_bound
        )
    );
}


/*
(7) array_power(anyarray,numeric)-->anyarray

Возводит в степень каждый элемент массива.
*/
PG_FUNCTION_INFO_V1(array_power);

Datum
array_power(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);
    Datum exponent = PG_GETARG_DATUM(1);

    Datum *values;
    bool *nulls;
    Datum *result;
    bool *result_nulls;

    int count;
    Oid type;
    int lower_bound;

    int64 int_exp = 0;
    float8 float_exp = 0;

    deconstruct_input_array(
        arr,
        &values,
        &nulls,
        &count,
        &type
    );

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1
        ? ARR_LBOUND(arr)[0]
        : 1;

    
    //Для целочисленных массивов степень должна быть неотрицательным целым числом
    if (type == INT2OID ||
        type == INT4OID ||
        type == INT8OID)
    {
        int_exp = get_integer_exponent(exponent);
    }

    
    //Для вещественных массивов степень преобразуем в double precision
    if (type == FLOAT4OID || type == FLOAT8OID)
    {
        float_exp = DatumGetFloat8(
            DirectFunctionCall1(numeric_float8, exponent)
        );

        if (!isfinite(float_exp))
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("exponent must be finite")));
    }

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            continue;
        }

        result_nulls[i] = false;

        switch (type)
        {
            case INT2OID:
            {
                int64 a = DatumGetInt16(values[i]);
                int64 power;

                if (int8_pow_overflow(a, (uint64) int_exp, &power) ||
                    power < SHRT_MIN || power > SHRT_MAX)
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("smallint power overflow")));

                result[i] = Int16GetDatum((int16) power);
                break;
            }

            case INT4OID:
            {
                int64 a = DatumGetInt32(values[i]);
                int64 power;

                if (int8_pow_overflow(a, (uint64) int_exp, &power) ||
                    power < INT_MIN || power > INT_MAX)
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("integer power overflow")));

                result[i] = Int32GetDatum((int32) power);
                break;
            }

            case INT8OID:
            {
                int64 a = DatumGetInt64(values[i]);
                int64 power;

                if (int8_pow_overflow(a, (uint64) int_exp, &power))
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("bigint power overflow")));

                result[i] = Int64GetDatum(power);
                break;
            }

            case FLOAT4OID:
            {
                float4 a = DatumGetFloat4(values[i]);
                float8 power;
                float4 final_power;

                if (!isfinite(a))
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("input value must be finite")));

                if (a == 0.0f && float_exp < 0)
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                             errmsg("zero raised to a negative power is undefined")));

                power = pow((float8) a, float_exp);

                if (isnan(power))
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                             errmsg("invalid argument for power function")));

                if (isinf(power))
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("real power overflow")));

                final_power = (float4) power;

                if (isinf(final_power))
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("real power overflow")));

                result[i] = Float4GetDatum(final_power);
                break;
            }

            case FLOAT8OID:
            {
                float8 a = DatumGetFloat8(values[i]);
                float8 power;

                if (!isfinite(a))
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("input value must be finite")));

                if (a == 0.0 && float_exp < 0)
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                             errmsg("zero raised to a negative power is undefined")));

                power = pow(a, float_exp);

                if (isnan(power))
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                             errmsg("invalid argument for power function")));

                if (isinf(power))
                    ereport(ERROR,
                            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                             errmsg("double precision power overflow")));

                result[i] = Float8GetDatum(power);
                break;
            }

            case NUMERICOID:
            {
                result[i] = DirectFunctionCall2(
                    numeric_power,
                    values[i],
                    exponent
                );
                break;
            }

            default:
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("unsupported array element type")));
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            type,
            lower_bound
        )
    );
}


/*
(8) array_sqrt(anyarray)-->anyarray

Извлекает квадратный корень из каждого элемента массива.
 */
PG_FUNCTION_INFO_V1(array_sqrt);

Datum array_sqrt(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);

    Datum *values;
    bool *nulls;

    int count;
    Oid elemtype;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr, &values, &nulls, &count, &elemtype
    );

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1 ? ARR_LBOUND(arr)[0] : 1;

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        
        if (elemtype == INT2OID)
        {
            int16 value = DatumGetInt16(values[i]);

            if (value < 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("cannot take square root of a negative number")));

            result[i] = Int16GetDatum(
                (int16) integer_sqrt((uint64) value)
            );
        }

        
        else if (elemtype == INT4OID)
        {
            int32 value = DatumGetInt32(values[i]);

            if (value < 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("cannot take square root of a negative number")));

            result[i] = Int32GetDatum(
                (int32) integer_sqrt((uint64) value)
            );
        }

        
        else if (elemtype == INT8OID)
        {
            int64 value = DatumGetInt64(values[i]);

            if (value < 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("cannot take square root of a negative number")));

            result[i] = Int64GetDatum(
                (int64) integer_sqrt((uint64) value)
            );
        }

        
        else if (elemtype == FLOAT4OID)
        {
            float4 value = DatumGetFloat4(values[i]);

            if (!isfinite(value))
                ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("input value must be finite")));



            if (value < 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("cannot take square root of a negative number")));

            result[i] = Float4GetDatum(sqrtf(value));
        }

        
        else if (elemtype == FLOAT8OID)
        {
            float8 value = DatumGetFloat8(values[i]);

            if (!isfinite(value))
                ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("input value must be finite")));



            if (value < 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("cannot take square root of a negative number")));

            result[i] = Float8GetDatum(sqrt(value));
        }

        
        else if (elemtype == NUMERICOID)
        {
            result[i] = DirectFunctionCall1(
                numeric_sqrt,
                values[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            elemtype,
            lower_bound
        )
    );
}


/*
(9) array_abs(anyarray) --> anyarray

Вычисляет модуль каждого элемента массива.
*/
PG_FUNCTION_INFO_V1(array_abs);

Datum array_abs(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);

    Datum *values;
    bool *nulls;

    int count;
    Oid elemtype;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr, &values, &nulls, &count, &elemtype
    );

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1 ? ARR_LBOUND(arr)[0] : 1;

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        if (elemtype == INT2OID)
        {
            int16 value = DatumGetInt16(values[i]);

            if (value == SHRT_MIN)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("smallint absolute value overflow")));

            result[i] = Int16GetDatum(value < 0 ? -value : value);
        }

        else if (elemtype == INT4OID)
        {
            int32 value = DatumGetInt32(values[i]);

            if (value == INT_MIN)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("integer absolute value overflow")));

            result[i] = Int32GetDatum(value < 0 ? -value : value);
        }

        else if (elemtype == INT8OID)
        {
            int64 value = DatumGetInt64(values[i]);

            if (value == PG_INT64_MIN)
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("bigint absolute value overflow")));

            result[i] = Int64GetDatum(value < 0 ? -value : value);
        }

        else if (elemtype == FLOAT4OID)
        {
            float4 value = DatumGetFloat4(values[i]);

            if (isnan(value))
                ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                        errmsg("NaN is not allowed")));

            if (!isfinite(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("input value must be finite")));

            result[i] = Float4GetDatum(fabsf(value));
        }

        else if (elemtype == FLOAT8OID)
        {
            float8 value = DatumGetFloat8(values[i]);

            if (isnan(value))
                ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                        errmsg("NaN is not allowed")));

            if (!isfinite(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("input value must be finite")));

            result[i] = Float8GetDatum(fabs(value));
        }

        else if (elemtype == NUMERICOID)
        {
            result[i] = DirectFunctionCall1(
                numeric_abs,
                values[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            elemtype,
            lower_bound
        )
    );
}

/*
(10) array_log(anyarray) --> anyarray

Вычисляет натуральный логарифм каждого элемента массива.
*/
PG_FUNCTION_INFO_V1(array_log);

Datum array_log(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);

    Datum *values;
    bool *nulls;

    int count;
    Oid elemtype;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr, &values, &nulls, &count, &elemtype
    );

    if (elemtype == INT2OID ||
        elemtype == INT4OID ||
        elemtype == INT8OID)
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("integer arrays are not supported by array_log")));

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1 ? ARR_LBOUND(arr)[0] : 1;

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        if (elemtype == FLOAT4OID)
        {
            float4 value = DatumGetFloat4(values[i]);

            if (isnan(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("NaN is not allowed")));

            if (isinf(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("Infinity is not allowed")));

            if (value <= 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("natural logarithm is defined only for positive numbers")));

            result[i] = Float4GetDatum(logf(value));
        }

        else if (elemtype == FLOAT8OID)
        {
            float8 value = DatumGetFloat8(values[i]);

            if (isnan(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("NaN is not allowed")));

            if (isinf(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("Infinity is not allowed")));

            if (value <= 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                         errmsg("natural logarithm is defined only for positive numbers")));

            result[i] = Float8GetDatum(log(value));
        }

        else if (elemtype == NUMERICOID)
        {
            result[i] = DirectFunctionCall1(
                numeric_ln,
                values[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            elemtype,
            lower_bound
        )
    );
}


/*
(11) array_exp(anyarray) --> anyarray

Вычисляет экспоненту каждого элемента массива.
*/
PG_FUNCTION_INFO_V1(array_exp);

Datum array_exp(PG_FUNCTION_ARGS)
{
    ArrayType *arr = PG_GETARG_ARRAYTYPE_P(0);

    Datum *values;
    bool *nulls;

    int count;
    Oid elemtype;

    Datum *result;
    bool *result_nulls;

    int lower_bound;

    deconstruct_input_array(
        arr, &values, &nulls, &count, &elemtype
    );

    if (elemtype == INT2OID ||
        elemtype == INT4OID ||
        elemtype == INT8OID)
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("integer arrays are not supported by array_exp")));

    result = palloc(sizeof(Datum) * count);
    result_nulls = palloc(sizeof(bool) * count);

    lower_bound = ARR_NDIM(arr) == 1 ? ARR_LBOUND(arr)[0] : 1;

    for (int i = 0; i < count; i++)
    {
        if (nulls[i])
        {
            result_nulls[i] = true;
            result[i] = (Datum) 0;
            continue;
        }

        result_nulls[i] = false;

        if (elemtype == FLOAT4OID)
        {
            float4 value = DatumGetFloat4(values[i]);
            float4 exp_result;

            if (isnan(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("NaN is not allowed")));

            if (isinf(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("Infinity is not allowed")));

            exp_result = expf(value);

            if (isinf(exp_result))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("real exponential overflow")));

            result[i] = Float4GetDatum(exp_result);
        }

        else if (elemtype == FLOAT8OID)
        {
            float8 value = DatumGetFloat8(values[i]);
            float8 exp_result;

            if (isnan(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("NaN is not allowed")));

            if (isinf(value))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("Infinity is not allowed")));

            exp_result = exp(value);

            if (isinf(exp_result))
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("double precision exponential overflow")));

            result[i] = Float8GetDatum(exp_result);
        }

        else if (elemtype == NUMERICOID)
        {
            result[i] = DirectFunctionCall1(
                numeric_exp,
                values[i]
            );
        }
    }

    PG_RETURN_ARRAYTYPE_P(
        construct_result_array(
            result,
            result_nulls,
            count,
            elemtype,
            lower_bound
        )
    );
}