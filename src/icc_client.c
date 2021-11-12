#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "icc.h"


int main(int argc __attribute__((unused)), char **argv __attribute__((unused))) {
    int rc;

    /* RPC CONNECTORS*/

    /*MALLEABILITY*/
    struct icc_context *icc;
    icc_init(ICC_LOG_INFO, &icc);
    assert(icc != NULL);
    int rpc_retcode;
    struct icc_rpc_malleability_manager_in rpc_in = {.number=4};
    rc = icc_rpc_send(icc, ICC_RPC_MALLEABILITY, &rpc_in, &rpc_retcode);
    if (rc == ICC_SUCCESS)
        printf("RPC successful: retcode=%d\n", rpc_retcode);
    else
        fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);

    rc = icc_fini(icc);
    assert(rc == 0);

    /*SLURM*/
    struct icc_context *icc2;
    icc_init(ICC_LOG_INFO, &icc2);
    assert(icc2 != NULL);
    int rpc_retcode2;
    struct icc_rpc_slurm_manager_in rpc2_in = {.number=8};
    rc = icc_rpc_send(icc2, ICC_RPC_SLURM, &rpc2_in, &rpc_retcode2);
    if (rc == ICC_SUCCESS)
        printf("RPC successful: retcode=%d\n", rpc_retcode2);
    else
        fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);

    rc = icc_fini(icc2);
    assert(rc == 0);


    /*IO Scheduler*/
    struct icc_context *icc3;
    icc_init(ICC_LOG_INFO, &icc3);
    assert(icc3 != NULL);
    int rpc_retcode3;
    struct icc_rpc_io_scheduler_in rpc3_in = {.number=12};
    rc = icc_rpc_send(icc3, ICC_RPC_IOSCHED, &rpc3_in, &rpc_retcode3);
    if (rc == ICC_SUCCESS)
        printf("RPC successful: retcode=%d\n", rpc_retcode3);
    else
        fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);

    rc = icc_fini(icc3);
    assert(rc == 0);


    /*ADHOC manager*/
    struct icc_context *icc4;
    icc_init(ICC_LOG_INFO, &icc4);
    assert(icc4 != NULL);
    int rpc_retcode4;
    struct icc_rpc_adhoc_manager_in rpc4_in = {.number=16};
    rc = icc_rpc_send(icc4, ICC_RPC_ADHOC, &rpc4_in, &rpc_retcode4);
    if (rc == ICC_SUCCESS)
        printf("RPC successful: retcode=%d\n", rpc_retcode4);
    else
        fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);

    rc = icc_fini(icc4);
    assert(rc == 0);


    /*IO Scheduler*/
    struct icc_context *icc5;
    icc_init(ICC_LOG_INFO, &icc5);
    assert(icc5 != NULL);
    int rpc_retcode5;
    struct icc_rpc_monitoring_manager_in rpc5_in = {.number=20};
    rc = icc_rpc_send(icc5, ICC_RPC_MONITOR, &rpc5_in, &rpc_retcode5);
    if (rc == ICC_SUCCESS)
        printf("RPC successful: retcode=%d\n", rpc_retcode5);
    else
        fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);

    rc = icc_fini(icc5);
    assert(rc == 0);


    return EXIT_SUCCESS;
}
