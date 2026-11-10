MODULES = pg_array_math
EXTENSION = pg_array_math
DATA = pg_array_math--1.0.sql
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)