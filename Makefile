CC := gcc
PKG_CONFIG := pkg-config

sources := icc_server.c icc_client.c
# keep libicc in front
binaries := libicc.so $(sources:.c=) libadhoccli.so
sources += icc.c adhoccli.c

objects := $(sources:.c=.o)
depends := $(sources:.c=.d)

includedir := include
sourcedir := src

vpath %.c $(sourcedir)

CPPFLAGS := -I$(includedir) -MMD
CFLAGS := -Wall -Wextra -O2 -g
LDFLAGS := -Wl,-rpath,"\$$ORIGIN"
LDLIBS :=


.PHONY: all clean

all: $(binaries)

clean:
	$(RM) $(binaries)
	$(RM) $(objects)
	$(RM) $(depends)

# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

libicc.so icc_server: CFLAGS += `$(PKG_CONFIG) --cflags margo`
libicc.so icc_server icc_client: LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined

icc_client: LDLIBS += -L. -licc

libadhoccli.so: LDLIBS += -L. -licc -lslurm

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

-include $(depends)
