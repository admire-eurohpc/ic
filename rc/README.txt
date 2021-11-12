ROOT CONTROLLER

This code executes the following pseudo-code (12/11/2021):
1. Define the environment (Margo)
2. Creation of a set of threads. Each one represents a different component connected with the Intelligent Controller.
3. Each thread registers one RCP
    - One for the Malleability Manager
    - One for the Slurm controller
    - One for the I/O scheduler
    - One for the AdHoc storage controller
    - One for the Monitoring manager
4. Each thread (and its RCP) waits for communications
5. If the icc_client is executed, each thread shows the received value.

*** In this stage, each RCP sends uint8_t

TO DO:
1. Implement the required RPCs.
2. Implement the structs needed
3. Implement test_clients to test the communication with structs instead of uint8_t
4. Thread for Analytic Component
5. Thread for Redis Database management
6. For each application, create one thread for the Application Manager and other for the performance metrics collector.
7. Implement the complete logic for each component.


COMPILATION
0. Follow the instructions in /src/ic/README.txt from INRIA to setup the environment.
1. Copy /src/ic/src/icdb.c to /src/ic/rc/src/icdb.c and  update the headers:
    #include <hiredis/hiredis.h>
    #include "../../include/icdb.h"
2. mkdir build
3. cd build
4. cmake ..
5. make

** The copy of the icdb.c file is needed because the compiler do not find the #includes


