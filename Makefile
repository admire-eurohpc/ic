CC := gcc

PREFIX ?=$(HOME)/.local
PKG_CONFIG_PATH := $(PREFIX)/lib/pkgconfig
INSTALL_PATH_LIB := $(PREFIX)/lib
INSTALL_PATH_BIN := $(PREFIX)/bin
INSTALL_PATH_INCLUDE := $(PREFIX)/include
INSTALL := install
MKDIR_P := mkdir -p
PKG_CONFIG ?= pkg-config

export PKG_CONFIG_PATH

includedir := include
sourcedir := src
exampledir := examples

icc_header := icc.h

ICC_MAJOR := $(shell grep ICC_MAJOR $(includedir)/$(icc_header) | awk '{print $$3}')
ICC_MINOR := $(shell grep ICC_MINOR $(includedir)/$(icc_header) | awk '{print $$3}')

# Use Slurm .pc if possible, fallback to default include/lib dirs
PKG_CONFIG_SLURM != $(PKG_CONFIG) --exists slurm

ifeq ($(.SHELLSTATUS),0)
CPPFLAGS_SLURM != $(PKG_CONFIG) --cflags slurm
LIBS_SLURM != $(PKG_CONFIG) --libs slurm
else
CPPFLAGS_SLURM :=
LIBS_SLURM := -lslurm
endif

libicc_so := libicc.so
libicc_soname :=  $(libicc_so).$(ICC_MAJOR)
libicc_realname := $(libicc_soname).$(ICC_MINOR)

icc_server_bin := icc_server
icc_client_bin := icc_client
icc_jobcleaner_bin := icc_jobcleaner

libslurmadmcli_so := libslurmadmcli.so
libslurmadhoccli_so := libslurmadhoccli.so
libslurmjobmon_so := libslurmjobmon.so
libslurmjobmon2_so := libslurmjobmon2.so
testapp_bin := testapp

sources := hashmap.c server.c rpc.c cb.c cbcommon.c icdb.c icrm.c icc.c flexmpi.c
sources += slurmjobmon.c slurmjobmon2.c slurmadhoccli.c jobcleaner.c
sources += client.c testapp.c spawn.c synthio.c writer.c mpitest.c test.c nomem.c

sources += ${ENABLE_SLURMADMCLI:true=slurmadmcli.c}

# keep libicc in front
binaries := $(libicc_so) server client jobcleaner $(libslurmjobmon_so) $(libslurmjobmon2_so) $(libslurmadhoccli_so) spawn synthio writer mpitest nomem

binaries += ${ENABLE_SLURMADMCLI:true=$(libslurmadmcli_so)}

objects := $(sources:.c=.o)
depends := $(sources:.c=.d)

vpath %.c $(sourcedir) $(exampledir)

CPPFLAGS := -I$(includedir) -MMD
CFLAGS := -std=gnu99 -Wall -Wextra -Werror=uninitialized -O0 -g


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
	$(INSTALL) -m 755 scripts/admire_mpiexec.sh $(INSTALL_PATH_BIN)/admire_mpiexec
	$(INSTALL) -m 755 scripts/admire_srun.sh $(INSTALL_PATH_BIN)/admire_srun
	$(INSTALL) -m 755 scripts/afs $(INSTALL_PATH_BIN)/afs
	$(INSTALL) -m 755 scripts/areg $(INSTALL_PATH_BIN)/areg
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
	$(RM) $(INSTALL_PATH_BIN)/admire_mpiexec
	$(RM) $(INSTALL_PATH_BIN)/admire_srun
	$(RM) $(INSTALL_PATH_BIN)/afs
	$(RM) $(INSTALL_PATH_BIN)/areg
	# $(RM) $(INSTALL_PATH_BIN)/$(testapp_bin)


# forces the creation of object files
# necessary for automatic dependency handling
$(objects): %.o: %.c

lib%.so: CFLAGS += -fpic
lib%.so: LDFLAGS += -shared
lib%.so: %.o
	$(LINK.o) $^ $(LDLIBS) -o $@

icdb.o: CPPFLAGS += `$(PKG_CONFIG) --cflags hiredis uuid`

icrm.o: CPPFLAGS += $(CPPFLAGS_SLURM)

server: icdb.o icrm.o rpc.o cbcommon.o cbserver.o hashmap.o
server: CPPFLAGS += `$(PKG_CONFIG) --cflags margo uuid`
server: LDLIBS += -lm `$(PKG_CONFIG) --libs margo hiredis` $(LIBS_SLURM) -Wl,--no-undefined

$(libicc_so): rpc.o cb.o cbcommon.o flexmpi.o icrm.o hashmap.o
$(libicc_so): CPPFLAGS += `$(PKG_CONFIG) --cflags margo uuid`
$(libicc_so): LDLIBS += `$(PKG_CONFIG) --libs margo uuid` $(LIBS_SLURM) -ldl -Wl,--no-undefined,-h$(libicc_soname)

client: LDLIBS += -L. -licc -Wl,--no-undefined,-rpath-link=${PREFIX}/lib

jobcleaner: LDLIBS += -L. -licc -Wl,--no-undefined,-rpath-link=${PREFIX}/lib

spawn: CPPFLAGS += `$(PKG_CONFIG) --cflags mpich`
spawn: LDLIBS += `$(PKG_CONFIG) --libs mpich` -L. -licc -Wl,--no-undefined,-rpath-link=${PREFIX}/lib

synthio: CPPFLAGS += `$(PKG_CONFIG) --cflags mpich`
synthio: LDLIBS += `$(PKG_CONFIG) --libs mpich` -L. -licc -Wl,--no-undefined,-rpath-link=${PREFIX}/lib

writer: CPPFLAGS += `$(PKG_CONFIG) --cflags mpich`
writer: LDLIBS += `$(PKG_CONFIG) --libs mpich` -L. -licc -Wl,--no-undefined,-rpath-link=${PREFIX}/lib

scord_client: CPPFLAGS +=  `$(PKG_CONFIG) --cflags scord`
scord_client: LDLIBS +=  `$(PKG_CONFIG) --libs scord` -Wl,--no-undefined,-rpath-link=${PREFIX}/lib

# $(testapp_bin): CPPFLAGS += `$(PKG_CONFIG) --cflags mpi`
# $(testapp_bin): LDLIBS += `$(PKG_CONFIG) --libs mpi margo` -L. -licc

test: LDLIBS += `$(PKG_CONFIG) --libs margo` -L. -licc

mpitest: CPPFLAGS += -I$(PREFIX)/include `$(PKG_CONFIG) --cflags mpich`
mpitest: LDLIBS += -L$(PREFIX)/lib -lempi `$(PKG_CONFIG) --libs mpich` -L. -licc -Wl,--allow-shlib-undefined,-rpath-link=${PREFIX}/lib -lpapi

slurmjobmon.o slurmadhoccli.o slurmadmcli.o: CPPFLAGS += $(CPPFLAGS_SLURM)

slurmjobmon2.o: CPPFLAGS += $(CPPFLAGS_SLURM) `$(PKG_CONFIG) --cflags hiredis`

# cannot use -Wl,--no-undefined here because some Spank symbols in
# libslurm have LOCAL binding
$(libslurmjobmon_so) $(libslurmadhoccli_so): LDLIBS += $(LIBS_SLURM) -L. -licc
$(libslurmjobmon2_so): LDLIBS += $(LIBS_SLURM) `$(PKG_CONFIG) --libs hiredis`

$(libslurmadmcli_so): CPPFLAGS += `$(PKG_CONFIG) --cflags scord`
$(libslurmadmcli_so): LDLIBS += `$(PKG_CONFIG) --libs scord` $(LIBS_SLURM)

-include $(depends)
