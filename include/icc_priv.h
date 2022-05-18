#ifndef ADMIRE_ICC_PRIV_H
#define ADMIRE_ICC_PRIV_H

#include <stdint.h>
#include <margo.h>
#include "uuid_admire.h"

#include "icc.h"
#include "rpc.h"
#include "icrm.h"
#include "flexmpi.h"           /* flexmpi function signature */

struct icc_context {
  /* read-only after initialization */
  margo_instance_id mid;
  hg_addr_t         addr;               /* server address */
  hg_id_t           rpcids[RPC_COUNT];  /* RPCs ids */
  uint16_t          provider_id;        /* Margo provider ID (unused) */
  uint8_t           bidirectional;
  char              registered;         /* has the client been registered? */
  uint32_t          jobid;              /* resource manager jobid */
  char              clid[UUID_STR_LEN]; /* client uuid */
  enum icc_client_type type;            /* client type */

  /* modified on alloc/release, use lock to access */
  ABT_rwlock hostlock;
  hm_t       *hostalloc;                /* map of host:ncpus allocated */
  hm_t       *hostrelease;              /* map of host:ncpus released */
  enum icc_reconfig_type reconfig_flag; /* pending reconfiguration order */
  hm_t       *reconfigalloc;            /* map of host:ncpus to use for reconfig */

  /* XX fixme icrm not thread-safe */
  char              icrm_terminate;     /* terminate flag */
  ABT_pool          icrm_pool;          /* pool for blocking RM requests */
  ABT_xstream       icrm_xstream;       /* exec stream to associate to the pool */

  icc_reconfigure_func_t reconfig_func;
  void                   *reconfig_data;
  /* XX TMP: flexmpi specific */
  void                   *flexhandle;   /* dlopen handle to FlexMPI lib */
  int                   flexmpi_sock;
  flexmpi_reconfigure_t flexmpi_func;
};

#endif
