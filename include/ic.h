#ifndef ADMIRE_IC_H
#define ADMIRE_IC_H

struct ic_context;

struct ic_context *ic_init(margo_log_level loglevel);
int ic_fini(struct ic_context *icc);
int ic_make_rpc(struct ic_context *icc);

#endif
