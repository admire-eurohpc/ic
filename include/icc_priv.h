#ifndef _ADMIRE_ICC_PRIV_H
#define _ADMIRE_ICC_PRIV_H

#include <stdint.h>
#include <margo.h>
#include "uuid_admire.h"

#include "icc.h"
#include "rpc.h"
#include "icrm.h"


struct icc_context {
  margo_instance_id mid;
  hg_addr_t         addr;               /* server address */
  hg_id_t           rpcids[RPC_COUNT];  /* RPCs ids */
  uint16_t          provider_id;        /* Margo provider ID (unused) */
  uint8_t           bidirectional;
  char              registered;         /* has the client been registered? */
  uint32_t          jobid;              /* resource manager jobid */
  char              clid[UUID_STR_LEN]; /* client uuid */
  enum icc_client_type type;            /* client type */

  struct icrm_context *icrm;            /* resource manager comm */
  ABT_xstream       icrm_xstream;       /* blocking execution stream */
  ABT_pool          icrm_pool;

  void              *flexhandle;        /* dlopen handle to FlexMPI library */
};

#endif
