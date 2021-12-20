# libicc: Intelligent controller communication library

## Dependencies

The libicc depends on Margo, which itself depends on a couple of
libraries. The following versions have been tested and confirmed to
work:
- libfabric 1.12.1 (needed for a stable ofi+tcp provider!)
- mercury   2.0.1
- argobots  1.1
- json-c    0.15
- margo     0.9.5
- redis     6.2
- hiredis   1.0.2

In the following, it is supposed that the librarie need to be
installed in `/usr/local`, adapt if necessary.

### Libfabric
Get the release from:
https://github.com/ofiwg/libfabric/releases/download/v1.12.1/libfabric-1.12.1.tar.bz2

The following disables infinipath and psm support. Depending on the
interconnects available on the cluster, they can be enabled.

```
./configure --prefix=/usr/.local --disable-verbs --disable-psm3 --disable-psm2 --disable-psm
make
make install
```

### Mercury

Get the release from:
https://github.com/mercury-hpc/mercury/releases/download/v2.0.1/mercury-2.0.1.tar.bz2

Mercury requires CMake to build. For Margo, a shared library is
needed, and the BOOST preprocessor must be enabled. The OpenFabrics
(OFI) plugin is also activated, which requires linking to libfabric
that was just installed.

From the Mercury checkout, run:

```
mkdir build
cd build
cmake .. -DBUILD_SHARED_LIBS:BOOL=ON \
         -DMERCURY_USE_BOOST_PP:BOOL=ON \
         -DNA_USE_OFI:BOOL=ON \
         -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
         -DOFI_LIBRARY:FILEPATH=/usr/local/lib/libfabric.so \
         -DOFI_INCLUDE_DIR:PATH=/usr/local/include \
         -Dpkgcfg_lib_PC_OFI_fabric:FILEPATH=/usr/local/lib/libfabric.so
make
make install
```

### Argobots
Get the release from:
https://github.com/pmodels/argobots/releases/download/v1.1/argobots-1.1.tar.gz

From the Argobots directory run:

```
./configure --prefix=/usr/local
make
make install
```

### json-c
Get release from:
https://github.com/json-c/json-c/archive/refs/tags/json-c-0.15-20200726.tar.gz

json-c requires CMake to build.

From the json-c directory, run:

```
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
         -DCMAKE_INSTALL_LIBDIR:PATH=lib \
         -DINSTALL_PKGCONFIG_DIR:PATH=lib/pkgconfig
make
make install
```

### Margo

Get release from:
https://github.com/mochi-hpc/mochi-margo/archive/refs/tags/v0.9.5.tar.gz

First, generate the configure script:

```
autoreconf -i
```

Then follow the normal process, adding the local directory where all
the libraries have been installed to the directories searched by
pkg-config:

```
./configure --prefix=/usr/local PKG_CONFIG_PATH=/usr/local
make
make install
```

### Redis

Get stable release from:
http://download.redis.io/redis-stable.tar.gz

Redis only uses requires Make and and a C compiler

```
make
make install PREFIX=/usr/local
```

From there, the Redis server can be run through the binary
`redis-server`.

### Hiredis
Hiredis is the official C client library for Redis.

The latest release can be downloaded from:
https://github.com/redis/hiredis/archive/refs/tags/v1.0.2.tar.gz

The library can be built and installed with:
```
make install PREFIX=/usr/local
```

Note that the PREFIX environment variable must be set at compilation
time, so that the pkg-config file takes it into account.


## libicc compilation

libicc is developed in the ADMIRE Git repository, in the `src/ic`
directory. The library, the server executable and the example client
executable can be compiled compiled with:

```
make all PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
```


## Running the ICC

Note: if the dependent libraries are stored in a non-standard
directory, e.g. `/usr/local/lib`, run with the environment variable
`LD_LIBRARY_PATH=/usr/local/lib`, or run ldconfig before.

The ICC server must be run first. It will write its address to a file
that MUST be on shared storage, so that all clients can read it. To
find the directory, the ICC server will try the environment variable
`AMIRE_DIR`, then `HOME` and finally default to the current directory (".").

To run the ICC server, simply use the executable:

```
./icc_server
```

To communicate with the ICC server, clients must be linked to the
dynamic library `libicc.so`. The header file `include/icc.h` details
the API. Examples client are available in `src/icc_client.c` and
`src/adhoccli.c`.

For example, to run the test ICC client, run:
```
./icc_client
```

If the shared library libicc.so is in the same directory as the client
executable, it will get picked up. Otherwise, the environment variable
`LD_LIBRARY_PATH` must be adjusted.


To make things easier to test two scripts `icc_server.sh` and
`icc_client.sh` are provided. On the
[PlaFRIM](https://www.plafrim.fr/) cluster they are in the
`/projets/admire/local/bin` directory , they respectively launch the
ICC server and an ICC client with the right environment
variables. With these script The ICC server runs for 10 minutes and
the ICC client makes a single RPC and exits. They can be run using the
`sbatch` command or directly:

```
/projets/admire/local/bin/icc_server.sh
```

and once the server is running:

```
/projets/admire/local/bin/icc_client.sh
```


## Developers
### Using libicc
### How to add a public RPC to libicc
1. Define a new RPC code in `icc_rpc_code`.
2. Define the Mercury structures associated with it (note that
   all structure must have a client id `clid` member).
3. Update function `register_rpcs` to include this RPC.
4. Define a callback in the target module (i.e the receiver of the
   RPC). Associate the callback using the REGISTER_PREP macro. In the
   origin module (i.e the sender), the same macro is used, but with
   NULL in lieu of a callback.

### Database
The choice has been made to use Redis, a non-relational database. What
follows is an effort to document the “schema” accessible to the intelligent controller

- IC clients (e.g connecting application) have their information
  stored in `client:<clid>`, where `clid` is an uuid string uniquely
  identifying the client.
- There is a corresponding index `index:clients`, containing the
  `clid` of all registered clients.
