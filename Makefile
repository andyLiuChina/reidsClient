OBJ=net.o hiredis.o sds.o async.o redisasync.o
LIBNAME=libhiredis

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
INCLUDEDIR=./include
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings

ifeq ("$(MODE)", "")
MODE=debug
OPTIMIZATION?=-O0
DEBUG?= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CFLAGS) $(WARNINGS) $(DEBUG) $(ARCH) -I$(INCLUDEDIR) -DDEBUG
else
MODE=release
OPTIMIZATION?=-O2
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CFLAGS) $(WARNINGS) $(DEBUG) $(ARCH) -I$(INCLUDEDIR)
endif

STLIBSUFFIX=a
STLIBNAME=$(LIBNAME).$(STLIBSUFFIX)
STLIB_MAKE_CMD=ar rcs $(STLIBNAME)

static: clean $(STLIBNAME)

net.o: ./src/net.c
	$(CC) -std=c99 -pedantic -c $(REAL_CFLAGS) $<
async.o: ./src/async.c ./src/dict.c
	$(CC) -std=c99 -pedantic -c $(REAL_CFLAGS) $<
hiredis.o: ./src/hiredis.c
	$(CC) -std=c99 -pedantic -c $(REAL_CFLAGS) $<
sds.o: ./src/sds.c
	$(CC) -std=c99 -pedantic -c $(REAL_CFLAGS) $<
redisasync.o:./src/redisasync.cpp
	g++ -c $(subst -Wstrict-prototypes, , $(REAL_CFLAGS)) $<

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $(OBJ)
	mv *.o ./dep && mv $(STLIBNAME) $(MODE)

clean:
	rm -rf ./$(MODE)/$(STLIBNAME)  ./dep/*.o

distclean:
	rm -rf ./$(MODE)/$(STLIBNAME)  ./dep/*.o

.PHONY: static clean distclean
