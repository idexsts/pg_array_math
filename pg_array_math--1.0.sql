\echo Use "CREATE EXTENSION pg_array_math" to load this file. \quit

CREATE FUNCTION array_add(anyarray, anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_add' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_sub(anyarray, anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_sub' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_mul(anyarray, anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_mul' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_div(anyarray, anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_div' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_add_scalar(anyarray, anyelement) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_add_scalar' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_mul_scalar(anyarray, anyelement) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_mul_scalar' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_power(anyarray, numeric) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_power' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_sqrt(anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_sqrt' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_abs(anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_abs' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_log(anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_log' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION array_exp(anyarray) RETURNS anyarray
AS 'MODULE_PATHNAME', 'array_exp' LANGUAGE C IMMUTABLE STRICT;