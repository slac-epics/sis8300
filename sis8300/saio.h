#ifndef SAIO_INCLUDE_H
#define SAIO_INCLUDE_H

/* Wrapper for linux' io_submit/io_setup/io_cancel etc. */
#include <unistd.h>
#include <linux/aio_abi.h>
#include <time.h>


/* Basic primitives */
int
saio_ctx_create(unsigned n_events, aio_context_t *ctxp);

int
saio_ctx_destroy(aio_context_t ctx);

int
saio_submit(aio_context_t ctx, long n_iocbs, struct iocb **iocbs);

int
saio_cancel(aio_context_t ctx, struct iocb *iocb, struct io_event *result);

int
saio_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events, struct timespec *timeout);
      

/* Helpers: */

/* Submit a single iocb for reading data */
void
saio_setup_for_read(struct iocb *iocb, int fd, void *buf, size_t count, off_t off);

/* Use a iocb on the stack; this is essentially a synchronous read
 * but with a timeout
 */
ssize_t
saio_pread(aio_context_t ctx, int fd, void *buf, size_t count, off_t off, struct timespec *timeout);


#endif
