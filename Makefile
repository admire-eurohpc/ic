SRC_DIR := src
INC_DIR := include

LIBSRC := ic.c
SERVERSRC := ic_server.c
CLIENTSRC := ic_client.c

LIB := $(SRC_DIR)/lib$(LIBSRC:.c=.so)
SERVER := $(SRC_DIR)/$(SERVERSRC:.c=)
CLIENT := $(SRC_DIR)/$(CLIENTSRC:.c=)


PKG_CONFIG := pkg-config

# requires:
# - libfabric 1.12.1 (/!\ stable with ofi+tcp provider)
# - mercury   2.0.1 (compiled with system libboost-wave-dev preprocessor)
# - argobots  1.1
# - json-c    0.15
# - margo     0.9.5
#
# compile with make all PKG_CONFIG_PATH=~/.local/lib
# run with LD_LIBRARY_PATH=~/.local/lib/ ./intelligent_controller

# XX --as-needed, -rpath?
# XX add .h to deps


CFLAGS := -Wall -Wpedantic -g
CFLAGS += -I$(INC_DIR) `$(PKG_CONFIG) --cflags margo`
LDFLAGS := `$(PKG_CONFIG) --libs margo`
LDFLAGS += -Wl,--no-undefined,-rpath,"\$$ORIGIN"


.PHONY: all clean

all: $(LIB) $(SERVER) $(CLIENT)

$(LIB): $(SRC_DIR)/$(LIBSRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -shared -o $@

$(SERVER): $(SRC_DIR)/$(SERVERSRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(CLIENT): $(SRC_DIR)/$(CLIENTSRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -L$(SRC_DIR) -lic -o $@

clean:
	rm -f $(LIB) $(SERVER) $(CLIENT)
