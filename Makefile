CC := gcc
PKG_CONFIG := pkg-config
MKDIR_P := mkdir -p
INSTALL := install

PREFIX ?= /usr/local

sources := icc_server.c icc_client.c
# keep libicc in front
binaries := libicc.so $(sources:.c=) libadhoccli.so libjobmon.so
sources += icc.c adhoccli.c jobmon.c

objects := $(sources:.c=.o)
depends := $(sources:.c=.d)

includedir := include
sourcedir := src

vpath %.c $(sourcedir)

CPPFLAGS := -I$(includedir) -MMD
CFLAGS := -Wall -Wextra -O2 -g
LDFLAGS := -Wl,-rpath,"\$$ORIGIN"
LDLIBS :=


.PHONY: all clean install uninstall

all: $(binaries)

clean:
	$(RM) $(binaries)
	$(RM) $(objects)
	$(RM) $(depends)

install: all
	$(MKDIR_P) $(PREFIX)/{lib,bin}
	$(INSTALL) -m 644 libicc.so   $(PREFIX)/lib
	$(INSTALL) -m 755 icc_server  $(PREFIX)/bin
	$(INSTALL) -m 755 icc_client  $(PREFIX)/bin
	$(INSTALL) -m 755 icc_server.sh  $(PREFIX)/bin/icc_server.sh
	$(INSTALL) -m 755 icc_client.sh  $(PREFIX)/bin/icc_client_test.sh

uninstall:
	$(RM) $(PREFIX)/lib/libicc.so
	$(RM) $(PREFIX)/bin/icc_server
	$(RM) $(PREFIX)/bin/icc_client_test


# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

libicc.so icc_server: CFLAGS += `$(PKG_CONFIG) --cflags margo`
libicc.so icc_server icc_client: LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined

icc_client: LDLIBS += -L. -licc

libjobmon.so libadhoccli.so: LDLIBS += -L. -licc -lslurm

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

-include $(depends)
