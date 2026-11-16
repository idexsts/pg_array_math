MODULES = pg_array_math
EXTENSION = pg_array_math
DATA = pg_array_math--1.0.sql

REGRESS = pg_array_math

TAP_TESTS = t/001_basic.pl t/002_parallel.pl t/003_large_data.pl t/004_errors.pl

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
PG_CFLAGS += -Wall -Werror -Wextra
subdir = contrib/pg_array_math
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

