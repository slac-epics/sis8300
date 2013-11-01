#include "saio.h"
#include <sys/syscall.h>
#include <stdint.h>
#include <string.h>

#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif

/* Basic primitives */
int
saio_ctx_create(unsigned n_events, aio_context_t *ctxp)
{
	*ctxp = 0;
	return syscall(SYS_io_setup, n_events, ctxp);
}

int
saio_ctx_destroy(aio_context_t ctx)
{
	return syscall(SYS_io_destroy, ctx);
}

int
saio_submit(aio_context_t ctx, long n_iocbs, struct iocb **iocbparr)
{
	return syscall(SYS_io_submit, ctx, n_iocbs, iocbparr);
}

int
saio_cancel(aio_context_t ctx, struct iocb *iocb, struct io_event *result)
{
	return syscall(SYS_io_cancel, ctx, iocb, result);
}

int
saio_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events, struct timespec *timeout)
{
	return syscall(SYS_io_getevents, ctx, min_nr, nr, events, timeout);
}
      

/* Helpers: */

/* Submit a single iocb for reading data */
void
saio_setup_for_read(struct iocb *iocb, int fd, void *buf, size_t count, off_t off)
{
	memset(iocb, 0, sizeof(*iocb));
	iocb->aio_lio_opcode = IOCB_CMD_PREAD;
	iocb->aio_fildes     = fd;
	iocb->aio_buf        = (uintptr_t)buf;
	iocb->aio_nbytes     = count;
	iocb->aio_offset     = off;
}

/* Use a iocb on the stack; this is essentially a synchronous read
 * but with a timeout
 */
ssize_t
saio_pread(aio_context_t ctx, int fd, void *buf, size_t count, off_t off, struct timespec *timeout)
{
struct iocb     b;
struct iocb    *ba[] = { &b };
int             st, rval;
struct io_event e;
	saio_setup_for_read(&b, fd, buf, count, off);
	if ( (st = saio_submit(ctx, sizeof(ba)/sizeof(ba[0]), ba)) < 0 ) {
#ifdef DEBUG
		perror("saio_submit ERROR");
#endif
		return st;
	}
	st = saio_getevents(ctx, 1, 1, &e, timeout);
#ifdef DEBUG
	if ( st <= 0 ) {
		if ( st < 0 )
			perror("saio_getevents ERROR");
		else
			fprintf(stderr,"saio_getevents TIMEOUT\n");
	}
#endif
	rval = e.res;
	if ( st <= 0 ) {
#ifdef DEBUG
		int st1 = 
#endif
		saio_cancel(ctx, &b, &e);
#ifdef DEBUG
		if ( st1 )
			perror("saio_cancel ERROR");
#endif
	}
	return st > 0 ? rval : st;
}

ssize_t
saio_pread_test(int fd, void *buf, size_t count, off_t off, struct timespec *timeout)
{
aio_context_t c = 0;
ssize_t       st;
#ifdef DEBUG
int           st1;
#endif

	if ( saio_ctx_create(1, &c) )  {
#ifdef DEBUG
		perror("saio_ctx_create ERROR");
#endif
		return -1;
	}

	st = saio_pread(c, fd, buf, count, off, timeout);	

#ifdef DEBUG
	st1 =
#endif
	saio_ctx_destroy( c );
#ifdef DEBUG
	if ( st1 )
		perror("saio_ctx_destroy ERROR");
#endif

	return st;
}
