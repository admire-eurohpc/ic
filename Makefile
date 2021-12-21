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
app_manager_bin := app_manager
libadhoccli_so := libadhoccli.so
libjobmon_so := libjobmon.so
testapp_bin := testapp

sources := icc_server.c icc_client.c icc_rpc.c icdb.c icc.c adhoccli.c jobmon.c flexmpi.c app_manager.c testapp.c

# keep libicc in front
binaries := $(libicc_so) $(icc_server_bin) $(icc_client_bin) $(libadhoccli_so) $(libjobmon_so) $(app_manager_bin) $(testapp_bin)

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
	$(INSTALL) -m 755 $(app_manager_bin) $(INSTALL_PATH_BIN)
	$(INSTALL) -m 755 scripts/icc_server.sh $(INSTALL_PATH_BIN)/icc_server.sh
	$(INSTALL) -m 755 scripts/icc_client.sh $(INSTALL_PATH_BIN)/icc_client.sh
	$(INSTALL) -m 755 $(testapp_bin) $(INSTALL_PATH_BIN)

uninstall:
	$(RM) $(INSTALL_PATH_LIB)/$(libicc_soname) $(INSTALL_PATH_LIB)/$(libicc_minorname)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_server_bin)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_client_bin)
	$(RM) $(INSTALL_PATH_BIN)/$(app_manager_bin)
	$(RM) $(INSTALL_PATH_BIN)/icc_server.sh
	$(RM) $(INSTALL_PATH_BIN)/icc_client.sh
	$(RM) $(INSTALL_PATH_BIN)/$(testapp_bin)


# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

icdb.o: CFLAGS += `$(PKG_CONFIG) --cflags hiredis uuid`

$(icc_server_bin): icc_rpc.o icdb.o
$(icc_server_bin): CFLAGS += `$(PKG_CONFIG) --cflags margo uuid`
$(icc_server_bin): LDLIBS += `$(PKG_CONFIG) --libs margo hiredis` -pthread -Wl,--no-undefined

$(libicc_so): icc_rpc.o flexmpi.o
$(libicc_so): CFLAGS += `$(PKG_CONFIG) --cflags margo uuid`
$(libicc_so): LDLIBS += `$(PKG_CONFIG) --libs margo uuid` -Wl,--no-undefined,-h$(libicc_minorname)

$(icc_client_bin): LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined -L. -licc

$(app_manager_bin): CFLAGS += `$(PKG_CONFIG) --cflags margo`
$(app_manager_bin): LDLIBS += `$(PKG_CONFIG) --libs margo` -Wl,--no-undefined
$(app_manager_bin): LDLIBS += -L. -licc -pthread

$(libjobmon_so) $(libadhoccli_so): LDLIBS += -L. -licc -lslurm

$(testapp_bin): CFLAGS += `$(PKG_CONFIG) --cflags mpi`
$(testapp_bin): LDLIBS += `$(PKG_CONFIG) --libs mpi margo` -L. -licc

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

-include $(depends)
