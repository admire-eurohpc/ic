cmake_minimum_required(VERSION 3.12)
project(ADMIREIntelligentController)

# We want to use clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99)

# Run grep and awk to capture ICC_MAJOR and ICC_MINOR
execute_process(COMMAND grep ICC_MAJOR ${CMAKE_CURRENT_SOURCE_DIR}/include/icc.h
                COMMAND awk "{print $3}"
                OUTPUT_VARIABLE ICC_MAJOR)
execute_process(COMMAND grep ICC_MINOR ${CMAKE_CURRENT_SOURCE_DIR}/include/icc.h
                COMMAND awk "{print $3}"
                OUTPUT_VARIABLE ICC_MINOR)

include_directories(include)

# Set compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Wextra -Werror=uninitialized -O0 -g")

#/***************************
# * SEARCH FOR DEPENDENCIES *
# ***************************/

# Find libslurm.so
find_library(SLURM_LIBRARY NAMES slurm PATHS /usr/lib /usr/lib64)

if(NOT SLURM_LIBRARY)
    message(FATAL_ERROR "libslurm not found")
endif()

# Find slurm/slurm.h
find_path(SLURM_INCLUDE_DIR slurm/slurm.h PATHS /usr/include)

if(NOT SLURM_INCLUDE_DIR)
    message(FATAL_ERROR "slurm/slurm.h not found")
endif()


find_package(PkgConfig REQUIRED)
pkg_search_module(MARGO REQUIRED IMPORTED_TARGET margo)
pkg_search_module(UUID REQUIRED IMPORTED_TARGET uuid)
pkg_search_module(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

# Show what was found

# Display information about libraries, include directories, and compile options
message(STATUS "Libraries:")
message(STATUS "  SLURM_LIBRARY: ${SLURM_LIBRARY}")
message(STATUS "  MARGO_LIBRARIES: ${MARGO_LIBRARIES}")
message(STATUS "  MARGO_LIBRARIES_DIRS: ${MARGO_LIBRARY_DIRS}")

message(STATUS "  UUID_LIBRARIES: ${UUID_LIBRARIES}")
message(STATUS "  UUID_LIBRARIES DIRS: ${UUID_LIBRARY_DIRS}")


message(STATUS "Include Directories:")
message(STATUS "  SLURM_INCLUDE_DIR: ${SLURM_INCLUDE_DIR}")
message(STATUS "  SLURM_INCLUDE_DIRS: ${PKG_CONFIG_SLURM_INCLUDE_DIRS}")
message(STATUS "  MARGO_INCLUDE_DIRS: ${MARGO_INCLUDE_DIRS}")
message(STATUS "  UUID_INCLUDE_DIRS: ${UUID_INCLUDE_DIRS}")

message(STATUS "Compile Options:")
message(STATUS "  SLURM_CFLAGS_OTHER: ${PKG_CONFIG_SLURM_CFLAGS_OTHER}")
message(STATUS "  MARGO_CFLAGS_OTHER: ${MARGO_CFLAGS_OTHER}")
message(STATUS "  UUID_CFLAGS_OTHER: ${UUID_CFLAGS_OTHER}")

#/**********
# * LIBICC *
# **********/

# Add the shared library target
add_library(icc SHARED
    src/rpc.c
    src/cb.c
    src/icc.c
    src/cbcommon.c
    src/flexmpi.c
    src/icrm.c
    src/hashmap.c
)

# We want to rpath it all
set_target_properties(icc PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
)

# Add lib search path

# Add library search paths
link_directories(${MARGO_LIBRARY_DIRS} ${UUID_LIBRARY_DIRS})

# Add Libraries

target_link_libraries(icc
PRIVATE
m
${SLURM_LIBRARY}
PkgConfig::MARGO
PkgConfig::UUID
dl
)

# Add Includes

target_include_directories(icc PUBLIC
                           ${SLURM_INCLUDE_DIR}
                           ${PKG_CONFIG_SLURM_INCLUDE_DIRS}
                           ${MARGO_INCLUDE_DIRS}
                           ${UUID_INCLUDE_DIRS})

# Add other flags

target_compile_options(icc PUBLIC
                        ${PKG_CONFIG_SLURM_CFLAGS_OTHER}
                        ${MARGO_CFLAGS_OTHER}
                        ${UUID_CFLAGS_OTHER})

#/**********
# * SERVER *
# **********/

# Add source files
add_executable(icc_server src/icdb.c src/icrm.c src/rpc.c src/cbcommon.c src/cbserver.c src/hashmap.c src/flexmpi.c
src/icc.c src/cb.c src/server.c )


# Add libraries and linker flags
target_link_libraries(icc_server PRIVATE
    m
    dl
    PkgConfig::MARGO
    PkgConfig::UUID
    PkgConfig::HIREDIS
    ${SLURM_LIBRARY}
)

set_target_properties(icc_server PROPERTIES LINK_FLAGS "-Wl,--no-undefined")

set_target_properties(icc_server PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
)



#/**********
# * CLIENT *
# **********/

# Add source files
add_executable(icc_client examples/client.c)

# Add libraries and linker flags
target_link_libraries(icc_client PRIVATE
    icc
)

set_target_properties(icc_client PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
)

#/**********
# * CLEANER *
# **********/

# Add source files
add_executable(icc_jobcleaner examples/jobcleaner.c)

# Add libraries and linker flags
target_link_libraries(icc_jobcleaner PRIVATE
    icc
)

#/*******************
# * INSTALL TARGETS *
# *******************/

# Libicc
install(TARGETS icc DESTINATION lib)
# Headers
install(FILES include/icc.h DESTINATION include)
# Scripts
install(PROGRAMS scripts/afs ./scripts/areg scripts/icc_server.sh scripts/icc_client.sh scripts/admire_mpiexec.sh scripts/admire_srun.sh DESTINATION bin)
# Binaries
install(TARGETS icc_server icc_client icc_jobcleaner DESTINATION bin)
