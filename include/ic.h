#ifndef ADMIRE_IC_H
#define ADMIRE_IC_H

struct ic_context;

/* Log levels lifted from Margo */
enum ic_log_level {
    IC_LOG_EXTERNAL,
    IC_LOG_TRACE,
    IC_LOG_DEBUG,
    IC_LOG_INFO,
    IC_LOG_WARNING,
    IC_LOG_ERROR,
    IC_LOG_CRITICAL
};

struct ic_context *ic_init(enum ic_log_level log_level);
int ic_fini(struct ic_context *icc);
int ic_make_rpc(struct ic_context *icc);

#endif
