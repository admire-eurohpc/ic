CC := gcc

PREFIX ?=/usr/local
INSTALL_PATH_LIB := $(PREFIX)/lib
INSTALL_PATH_BIN := $(PREFIX)/bin
INSTALL_PATH_INCLUDE := $(PREFIX)/include
INSTALL := install
MKDIR_P := mkdir -p
PKG_CONFIG ?= pkg-config

includedir := include
sourcedir := src
exampledir := examples

icc_header := icc.h

ICC_MAJOR := $(shell grep ICC_MAJOR $(includedir)/$(icc_header) | awk '{print $$3}')
ICC_MINOR := $(shell grep ICC_MINOR $(includedir)/$(icc_header) | awk '{print $$3}')

libicc_so := libicc.so
libicc_soname :=  $(libicc_so).$(ICC_MAJOR)
libicc_realname := $(libicc_soname).$(ICC_MINOR)

icc_server_bin := icc_server
icc_client_bin := icc_client
icc_jobcleaner_bin := icc_jobcleaner

libslurmadhoccli_so := libslurmadhoccli.so
libslurmjobmon_so := libslurmjobmon.so
testapp_bin := testapp

sources := server.c rpc.c cb.c cbcommon.c icdb.c icrm.c icc.c adhoccli.c cbserver.c reconfig.c
sources += slurmjobmon.c slurmadhoccli.c client.c testapp.c spawn.c jobcleaner.c

# keep libicc in front
binaries := $(libicc_so) server client jobcleaner $(libslurmjobmon_so) $(libslurmadhoccli_so) spawn

objects := $(sources:.c=.o)
depends := $(sources:.c=.d)

vpath %.c $(sourcedir) $(exampledir)

CPPFLAGS := -I$(includedir) -MMD
CFLAGS := -std=gnu99 -Wall -Wextra -O2 -g


.PHONY: all clean install uninstall

all: $(binaries)

clean:
	$(RM) $(binaries)
	$(RM) $(objects)
	$(RM) $(depends)

install: all
	$(MKDIR_P) $(INSTALL_PATH_LIB) $(INSTALL_PATH_BIN)
	$(INSTALL) -m 644 $(libicc_so) $(INSTALL_PATH_LIB)/$(libicc_realname)
	ln -rsf $(INSTALL_PATH_LIB)/$(libicc_realname) $(INSTALL_PATH_LIB)/$(libicc_soname)
	ln -rsf $(INSTALL_PATH_LIB)/$(libicc_realname) $(INSTALL_PATH_LIB)/$(libicc_so)
	$(INSTALL) -m 644 $(includedir)/$(icc_header) $(INSTALL_PATH_INCLUDE)
	$(INSTALL) -m 755 server $(INSTALL_PATH_BIN)/$(icc_server_bin)
	$(INSTALL) -m 755 client $(INSTALL_PATH_BIN)/$(icc_client_bin)
	$(INSTALL) -m 755 jobcleaner $(INSTALL_PATH_BIN)/$(icc_jobcleaner_bin)
	$(INSTALL) -m 755 scripts/icc_server.sh $(INSTALL_PATH_BIN)/icc_server.sh
	$(INSTALL) -m 755 scripts/icc_client.sh $(INSTALL_PATH_BIN)/icc_client.sh
	$(INSTALL) -m 755 scripts/icc_prolog.sh $(INSTALL_PATH_BIN)/icc_prolog.sh
	# $(INSTALL) -m 755 $(testapp_bin) $(INSTALL_PATH_BIN)

uninstall:
	$(RM) $(INSTALL_PATH_INCLUDE)/$(icc_header)
	$(RM) $(INSTALL_PATH_LIB)/$(libicc_soname) $(INSTALL_PATH_LIB)/$(libicc_so)
	$(RM) $(INSTALL_PATH_LIB)/$(libicc_realname)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_server_bin)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_client_bin)
	$(RM) $(INSTALL_PATH_BIN)/$(icc_jobcleaner_bin)
	$(RM) $(INSTALL_PATH_BIN)/icc_server.sh
	$(RM) $(INSTALL_PATH_BIN)/icc_client.sh
	$(RM) $(INSTALL_PATH_BIN)/icc_prolog.sh
	# $(RM) $(INSTALL_PATH_BIN)/$(testapp_bin)


# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

icdb.o: CFLAGS += `$(PKG_CONFIG) --cflags hiredis uuid`

server: icdb.o icrm.o rpc.o cbcommon.o cbserver.o
server: CFLAGS += `$(PKG_CONFIG) --cflags margo uuid`
server: LDLIBS += `$(PKG_CONFIG) --libs margo hiredis` -lslurm -Wl,--no-undefined

$(libicc_so): rpc.o cb.o cbcommon.o reconfig.o icrm.o
$(libicc_so): CFLAGS += `$(PKG_CONFIG) --cflags margo uuid`
$(libicc_so): LDLIBS += `$(PKG_CONFIG) --libs margo uuid` -lslurm -ldl -Wl,--no-undefined,-h$(libicc_soname)

client: LDLIBS += -L. `$(PKG_CONFIG) --libs margo` -licc -Wl,--no-undefined

jobcleaner: LDLIBS += -L. `$(PKG_CONFIG) --libs margo` -licc -Wl,--no-undefined

spawn: CFLAGS += `$(PKG_CONFIG) --cflags mpich`
spawn: LDLIBS += `$(PKG_CONFIG) --libs mpich margo` -L. -licc

# $(testapp_bin): CFLAGS += `$(PKG_CONFIG) --cflags mpi`
# $(testapp_bin): LDLIBS += `$(PKG_CONFIG) --libs mpi margo` -L. -licc

$(libslurmjobmon_so) $(libslurmadhoccli_so): LDLIBS += -L. -licc -lslurm

-include $(depends)
