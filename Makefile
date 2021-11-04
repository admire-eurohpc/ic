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

libicc_so := libicc.so
libicc_soname :=  $(libicc_so).$(ICC_MAJOR)
libicc_minorname := $(libicc_soname).$(ICC_MINOR).$(ICC_PATCH)

icc_server_bin := icc_server
icc_client_bin := icc_client
libadhoccli_so := libadhoccli.so
libjobmon_so := libjobmon.so


sources := icc_server.c icc_client.c icdb.c icc.c adhoccli.c jobmon.c
# keep libicc in front
binaries := $(libicc_so) $(icc_server_bin) $(icc_client_bin) $(libadhoccli_so) $(libjobmon_so)

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
	$(INSTALL) -m 644 $(libicc_so) $(INSTALL_PATH_LIB)/$(libicc_minorname)
	cd $(INSTALL_PATH_LIB) && ln -sf $(libicc_minorname) $(libicc_soname)
	$(INSTALL) -m 755 $(icc_server_bin) $(INSTALL_PATH_BIN)
	$(INSTALL) -m 755 $(icc_client_bin) $(INSTALL_PATH_BIN)
	$(INSTALL) -m 755 scripts/icc_server.sh $(INSTALL_PATH_BIN)/icc_server.sh
	$(INSTALL) -m 755 scripts/icc_client.sh $(INSTALL_PATH_BIN)/icc_client.sh

uninstall:
	$(RM) $(INSTALL_PATH_LIB)/$(libicc_soname) $(INSTALL_PATH_LIB)/$(libicc_minorname)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_server_bin)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_client_bin)
	$(RM) $(INSTALL_PATH_BIN)/icc_server.sh
	$(RM) $(INSTALL_PATH_BIN)/icc_client.sh


# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

icdb.o: CFLAGS += `$(PKG_CONFIG) --cflags hiredis`

$(icc_server_bin): icdb.o
$(icc_server_bin): CFLAGS += `$(PKG_CONFIG) --cflags margo`
$(icc_server_bin): LDLIBS += `$(PKG_CONFIG) --libs margo` `$(PKG_CONFIG) --libs hiredis` -Wl,--no-undefined

$(libicc_so): CFLAGS += `$(PKG_CONFIG) --cflags margo`
$(libicc_so): LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined,-h$(libicc_minorname)

$(icc_client_bin): LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined
$(icc_client_bin): LDLIBS += -L. -licc

$(libjobmon_so) $(libadhoccli_so): LDLIBS += -L. -licc -lslurm

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

-include $(depends)
