cmake_minimum_required(VERSION 3.9)
project(rc)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_IMPLICIT_LINK_DIRECTORIES /usr/local/lib ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES})
set(CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES /usr/local/lib ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})


# Add MARGO
find_library(/usr/local/lib NAMES libmargo.so)
message(STATUS ${/usr/local/lib})
add_library(libmargo STATIC IMPORTED)
set_target_properties(libmargo PROPERTIES IMPORTED_LOCATION ${/usr/local/lib})


# Add Mercury
find_library(/usr/local/lib NAMES libmercury.so)
message(STATUS ${/usr/local/lib})
add_library(libmercury STATIC IMPORTED)
set_target_properties(libmercury PROPERTIES IMPORTED_LOCATION ${/usr/local/lib})


# Add Hiredis
find_library(/usr/local/lib NAMES libhiredis.so)
message(STATUS ${/usr/local/lib})
add_library(libhiredis STATIC IMPORTED)
set_target_properties(libhiredis PROPERTIES IMPORTED_LOCATION ${/usr/local/lib})


# Code directory
add_subdirectory(src)


# Link libraries previously compiled by INRIA Makefile
add_library(libicc SHARED IMPORTED)
set_property(TARGET libicc PROPERTY IMPORTED_LOCATION "../libicc.so")
add_library(libadhoccli SHARED IMPORTED)
set_property(TARGET libadhoccli PROPERTY IMPORTED_LOCATION "../libadhoccli.so")
add_library(libjobmon SHARED IMPORTED)
set_property(TARGET libjobmon PROPERTY IMPORTED_LOCATION "../libjobmon.so")
add_library(libabt SHARED IMPORTED)
set_property(TARGET libabt PROPERTY IMPORTED_LOCATION "/usr/local/lib/libabt.so")
add_library(libfabric SHARED IMPORTED)
set_property(TARGET libfabric PROPERTY IMPORTED_LOCATION "/usr/local/lib/libfabric.so")
add_library(libjson-c SHARED IMPORTED)
set_property(TARGET libjson-c PROPERTY IMPORTED_LOCATION "/usr/local/lib/libjson-c.so")
add_library(libmchecksum SHARED IMPORTED)
set_property(TARGET libmchecksum PROPERTY IMPORTED_LOCATION "/usr/local/lib/libmchecksum.so")
add_library(libna SHARED IMPORTED)
set_property(TARGET libna PROPERTY IMPORTED_LOCATION "/usr/local/lib/libna.so")


# object icdb
add_library(icdb STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/icdb.c)
target_link_libraries( icdb /usr/local/lib/libhiredis.so )

# Resources references in headers
file(GLOB_RECURSE SOURCES
        ../include/icc.h
        ../include/icc_rpc.h
        ../include/icdb.h
        src/root_controller.cpp)


# Generate binary and link margo
add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} /usr/local/lib/libmargo.so /usr/local/lib/libmercury.so /usr/local/lib/libabt.so icdb -MMD -pthread -Wl,--no-as-needed)
