CC := gcc

PREFIX ?=/usr/local
INSTALL_PATH_LIB := $(PREFIX)/lib
INSTALL_PATH_BIN := $(PREFIX)/bin
INSTALL := install
MKDIR_P := mkdir -p
PKG_CONFIG ?= pkg-config

includedir := include
sourcedir := src

ICC_MAJOR := $(shell grep ICC_MAJOR $(includedir)/icc.h | awk '{print $$3}')
ICC_MINOR := $(shell grep ICC_MINOR $(includedir)/icc.h | awk '{print $$3}')
ICC_PATCH := $(shell grep ICC_PATCH $(includedir)/icc.h | awk '{print $$3}')


LIBICC_SO := libicc.so
LIBICC_SONAME :=  $(LIBICC_SO).$(ICC_MAJOR)
LIBICC_MINORNAME := $(LIBICC_SONAME).$(ICC_MINOR).$(ICC_PATCH)

ICC_SERVER_BIN := icc-server
ICC_CLIENT_BIN := icc-client
LIBADHOCCLI_BIN := libadhoccli.so
LIBJOBMON_BIN := libjobmon.so


sources := icc_server.c icc_client.c icdb.c icc.c adhoccli.c jobmon.c
# keep libicc in front
binaries := $(LIBICC_SO) icc_server icc_client libadhoccli.so libjobmon.so

objects := $(sources:.c=.o)
depends := $(sources:.c=.d)

vpath %.c $(sourcedir)

CPPFLAGS := -I$(includedir) -MMD
CFLAGS := -Wall -Wextra -O2 -g


.PHONY: all clean install uninstall

all: $(binaries)

clean:
	$(RM) $(binaries)
	$(RM) $(objects)
	$(RM) $(depends)

install: all
	$(MKDIR_P) $(INSTALL_PATH_LIB) $(INSTALL_PATH_BIN)
	$(INSTALL) -m 644 $(LIBICC_SO) $(INSTALL_PATH_LIB)/$(LIBICC_MINORNAME)
	cd $(INSTALL_PATH_LIB) && ln -sf $(LIBICC_MINORNAME) $(LIBICC_SONAME)
	$(INSTALL) -m 755 icc_server $(INSTALL_PATH_BIN)
	$(INSTALL) -m 755 icc_client $(INSTALL_PATH_BIN)
	$(INSTALL) -m 755 icc_server.sh $(INSTALL_PATH_BIN)/icc_server.sh
	$(INSTALL) -m 755 icc_client.sh $(INSTALL_PATH_BIN)/icc_client_test.sh

uninstall:
	$(RM) $(INSTALL_PATH_LIB)/$(LIBICC_SONAME) $(INSTALL_PATH_LIB)/$(LIBICC_MINORNAME)
	$(RM) $(INSTALL_PATH_BIN)/icc_server
	$(RM) $(INSTALL_PATH_BIN)/icc_client
	$(RM) $(INSTALL_PATH_BIN)/icc_server.sh
	$(RM) $(INSTALL_PATH_BIN)/icc_client_test.sh


# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

icdb.o: CFLAGS += `$(PKG_CONFIG) --cflags hiredis`

icc_server: icdb.o
icc_server: CFLAGS += `$(PKG_CONFIG) --cflags margo`
icc_server: LDLIBS += `$(PKG_CONFIG) --libs margo` `$(PKG_CONFIG) --libs hiredis` -Wl,--no-undefined

$(LIBICC_SO): CFLAGS += `$(PKG_CONFIG) --cflags margo`
$(LIBICC_SO): LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined,-h$(LIBICC_MINORNAME)

icc_client: LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined
icc_client: LDLIBS += -L. -licc

libjobmon.so libadhoccli.so: LDLIBS += -L. -licc -lslurm

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

-include $(depends)
