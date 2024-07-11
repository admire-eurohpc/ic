# IC: The Intelligent Controller

## Description of the Intelligent Controller

The IC is a multi-criteria distributed component that integrates cross-layer data to gain a holistic view of
the system and dynamically steer the system components. This enhances the I/O system behavior and enables
anticipatory decisions for resource allocation. 

From the point of view of a resource manager, the IC is the component responsible for coordinating the
allocation of the available resources for the jobs that request them. To do this, it integrates a module that
allows itself to communicate with Slurm. Slurm is the most widely used resource manager. It provides a
versatile platform for task management, resource allocation, and job scheduling in a distributed environment.
It also facilitates efficient resource management, job monitoring, and the implementation of custom allocation
policies. Slurm uses job queues to manage and prioritize tasks submitted by users, executing them according
to allocation policies that may consider factors such as user priority, resource availability, and time constraints.

The Intelligent Controller (IC) is a software written using the C language. As stated before, it is designed in a
client-server fashion. The connection protocol between the server and the client library is a custom RPC-based
API. client applications can access the IC services by linking to the IC library (libicc). Libicc is broad and
includes the interfaces to all different type of IC clients: applications, batch scheduler, monitoring system, etc.

For faster development and to avoid the pitfalls of a homegrown system, the custom RPC-based API is
based on the use of Mercury library. It is well in use in the HPC community, actively developed under
the umbrella of the Mochi project and portable to different platforms thanks to its use of libfabric,
including platform where TCP sockets are not available. Its main advantage for us is the set of macros it
offers to serialize and deserialize RPC arguments, a tedious task if done from scratch. It is based around an
asynchronous-adjacent programming style, with “callbacks” passed to non-blocking functions and queued for
later execution. Network progress and callback execution have to be made explicitely. While flexible and
allowing for fine-grained control, it is a more exotic programming style, arguably more convoluted and harder
to use. Fortunately, the Mochi project also provides Margo, a library wrapping Mercury’s callback model into a
simpler, more linear programming model with the help of the Argobots user-level threading library. While
not anticipated, this meant that the controller was multi-threaded from the get-go, a fact which simplified later
addition to the software.

The Intelligent Controller is a stateful component, and thus needs to store and organize some information. But
the hard work of collecting, aggregating and sorting through all the data from the various ADMIRE applications
and modules is offloaded to the monitoring infrastructure, the IC needs are thus relatively lightweight: sort and
query applications identifiers and node lists, transiently store some computation results. For this. The Redis
in-memory database was chosen because it provides data structures that are sophisticated enough to serve our
simple query needs and is easy to deploy and use from a C library. As an added benefit, it supports being
distributed which will come in handy should the controller evolve towards a more distributed design.


## Basic architecture of the Inteligent Controller

The basic architecture of the Intelligent Controller suite is a client-server one where the two main elements are
the following:

- IC server: This component is implemented as a centralized server process where all the application,
monitoring systems, etc. connects to provided information of the system/applications and to collect
malleability actions to be executed.

- IC client library: This component is the link between the IC server and the applications, monitoring
systems and other ADMIRE components. It is implemented as a library that is linked with the application
executable. It also includes the Slurm module which uses the Slurm API to execute all the malleability
commands that the IC server has decided to do related to this application.

## Installation Dependencies

The icc depends on Margo, which itself depends on a couple of
libraries. The following versions have been tested and confirmed to
work:
- libfabric 1.12.1 (needed for a stable ofi+tcp provider!)
- mercury   2.0.1
- argobots  1.1
- json-c    0.15
- margo     0.9.5 & 0.9.6
- redis     6.2
- hiredis   1.0.2
- slurm     20.11.4

In the following, it is supposed that the libraries need to be
installed in `/usr/local`.

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
./configure --prefix=/usr/local PKG_CONFIG_PATH=/usr/local:$PKG_CONFIG_PATH
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


## Compiling IC

icc is developed in the ADMIRE Git repository, in the `src/ic`
directory. Assuming all the required libraries are in `/usr/local`,
the library, the server executable and the example clients executables
can be compiled and installed with:
```
make install PREFIX=/usr/local PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
```


## Running the IC

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
the API. Examples client are available in the `example` directory: a
generic client `client.c`, two Slurm SPANK plugins `slurmadhoccli.c` and
`slurmjobmon.c` and an MPI application `testapp.c`

For example, to run the test ICC client, run:
```
./client
```

If the shared library libicc.so is in the same directory as the client
executable, it will get picked up. Otherwise, the environment variable
`LD_LIBRARY_PATH` must be adjusted.

The script `icc_server.sh` in the `ic/scripts` directory launches the
ICC server and the database with the right environment variables. It
can be launched using sbatch or directly within a Slurm allocation

## Using libicc
- See icc.h for the API. Libicc functions are prefixed by `icc_`.
- Libicc functions take an opaque “context” of type `struct
  icc_context` which is *not thread-safe*. Generally speaking, only
  one thread of the application should interact with the library at a
  time.

The workflow is as follow:
- init, which register the application to the IC
- ...
- call `icc_release_register()` to register CPUs for release.
- call `icc_release_nodes()` to actually release the nodes to the
  resource manager.

## Extending libicc
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
follows is an effort to document the “schema” accessible to the
intelligent controller

- IC clients (e.g connecting application) have their information
  stored in `client:<clid>`, where `clid` is an uuid string uniquely
  identifying the client.
- `index:clients` contains the `clid` of all registered clients.
- `index:clients:type:<type>` contains the `clid` of all clients of type `<type>`
- `index:clients:jobid:<jobid>` contains the `clid` of all clients
  registered within the job `<jobid>`

- `job:<jobid>` store the feature of a job as passed by the tasks
  running in the allocation: number of associated cpus and nodes.
