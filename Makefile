SRC_DIR := src
INC_DIR := include

LIBSRC := icc.c
SERVERSRC := icc_server.c
CLIENTSRC := icc_client.c

LIB := $(SRC_DIR)/lib$(LIBSRC:.c=.so)
SERVER := $(SRC_DIR)/$(SERVERSRC:.c=)
CLIENT := $(SRC_DIR)/$(CLIENTSRC:.c=)

# Ad-hoc storage targets
ADCLI_SRC := $(SRC_DIR)/adhoc_cli.c
ADCLI_LIB := $(SRC_DIR)/adhoccli.so

PKG_CONFIG := pkg-config

# requires:
# - libfabric 1.12.1 (/!\ stable with ofi+tcp provider)
# - mercury   2.0.1 (compiled with system libboost-wave-dev preprocessor)
# - argobots  1.1
# - json-c    0.15
# - margo     0.9.5
#
# compile with make all PKG_CONFIG_PATH=~/.local/lib/pkgconfig
# run with LD_LIBRARY_PATH=~/.local/lib ./intelligent_controller
# (or run ldconfig ~/.local/lib before)

# XX --as-needed, -rpath?
# XX add .h to deps


CFLAGS := -Wall -Wpedantic -g
CFLAGS += -I$(INC_DIR) `$(PKG_CONFIG) --cflags margo`
LDFLAGS := `$(PKG_CONFIG) --libs margo`
LDFLAGS += -Wl,--no-undefined,-rpath,"\$$ORIGIN"


.PHONY: all clean

all: $(LIB) $(SERVER) $(CLIENT) $(ADCLI_LIB)

$(LIB): $(SRC_DIR)/$(LIBSRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -fPIC -shared -o $@

$(SERVER): $(SRC_DIR)/$(SERVERSRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(CLIENT): $(SRC_DIR)/$(CLIENTSRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -L$(SRC_DIR) -licc -o $@


# XX compile separately?
ADCLI_CFLAGS := -Wall -Wpedantic -g -I $(INC_DIR)
ADCLI_LDFLAGS := -fPIC -shared -L$(SRC_DIR) -licc -lslurm -Wl,-rpath,"\$$ORIGIN"
$(ADCLI_LIB): $(ADCLI_SRC)
	$(CC) $(ADCLI_CFLAGS) $< $(ADCLI_LDFLAGS) -o $@


clean:
	rm -f $(LIB) $(SERVER) $(CLIENT) $(ADCLI_LIB)
