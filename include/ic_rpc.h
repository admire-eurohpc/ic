#ifndef ADMIRE_IC_RPC_H
#define ADMIRE_IC_RPC_H

#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#define IC_ADDR_MAX_SIZE 128
#define IC_ADDR_FILE "/tmp/ic.addr"
#define IC_HG_PROVIDER "ofi+tcp"


MERCURY_GEN_PROC(hello_out_t,
		 ((int64_t)(rc)) ((hg_string_t)(msg)))

#endif
