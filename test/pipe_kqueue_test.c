#include <sys/param.h>
#include <sys/event.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(pipe_kqueue__write_end);
ATF_TC_BODY(pipe_kqueue__write_end, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0);
	ATF_REQUIRE(p[0] >= 0);
	ATF_REQUIRE(p[1] >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[1], EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, 0);

	ATF_REQUIRE(kevent(kq, kev, 1, NULL, 0, NULL) == 0);

	/* Test that EVFILT_WRITE behaves sensible on the write end. */

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kev[0].flags == EV_CLEAR);
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 16384);
	ATF_REQUIRE(kev[0].udata == 0);

	/* Filling up the pipe should make the EVFILT_WRITE disappear. */

	char c = 0;
	ssize_t r;
	while ((r = write(p[1], &c, 1)) == 1) {
	}
	ATF_REQUIRE(r < 0);
	ATF_REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/* Reading (PIPE_BUF - 1) bytes will not trigger a EVFILT_WRITE yet. */

	for (int i = 0; i < PIPE_BUF - 1; ++i) {
		ATF_REQUIRE(read(p[0], &c, 1) == 1);
	}

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/* Reading one additional byte triggers the EVFILT_WRITE. */

	ATF_REQUIRE(read(p[0], &c, 1) == 1);

	r = kevent(kq, NULL, 0, kev, nitems(kev), &(struct timespec) { 0, 0 });
	ATF_REQUIRE(r == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kev[0].flags == EV_CLEAR);
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == PIPE_BUF);
	ATF_REQUIRE(kev[0].udata == 0);

	/*
	 * Reading another byte triggers the EVFILT_WRITE again with a changed
	 * 'data' field.
	 */

	ATF_REQUIRE(read(p[0], &c, 1) == 1);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kev[0].flags == EV_CLEAR);
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == PIPE_BUF + 1);
	ATF_REQUIRE(kev[0].udata == 0);

	/*
	 * Closing the read end should make a EV_EOF appear but leave the 'data'
	 * field unchanged.
	 */

	ATF_REQUIRE(close(p[0]) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kev[0].flags == (EV_CLEAR | EV_EOF | EV_ONESHOT));
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == PIPE_BUF + 1);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(pipe_kqueue__closed_read_end);
ATF_TC_BODY(pipe_kqueue__closed_read_end, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0);
	ATF_REQUIRE(p[0] >= 0);
	ATF_REQUIRE(p[1] >= 0);

	ATF_REQUIRE(close(p[0]) == 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[1], EVFILT_READ, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);
	EV_SET(&kev[1], p[1], EVFILT_WRITE, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);

	/*
	 * Trying to register EVFILT_WRITE when the pipe is closed leads to an
	 * EPIPE error.
	 */

	ATF_REQUIRE(kevent(kq, kev, 2, kev, 2, NULL) == 2);
	ATF_REQUIRE((kev[0].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE((kev[1].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[1].data == EPIPE);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_READ);
	ATF_REQUIRE(kev[0].flags == (EV_EOF | EV_CLEAR | EV_RECEIPT));
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(pipe_kqueue__closed_read_end_register_before_close);
ATF_TC_BODY(pipe_kqueue__closed_read_end_register_before_close, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0);
	ATF_REQUIRE(p[0] >= 0);
	ATF_REQUIRE(p[1] >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[1], EVFILT_READ, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);
	EV_SET(&kev[1], p[1], EVFILT_WRITE, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);

	/*
	 * Registering EVFILT_WRITE before the pipe is closed leads to a
	 * EVFILT_WRITE event with EV_EOF set.
	 */

	ATF_REQUIRE(kevent(kq, kev, 2, kev, 2, NULL) == 2);
	ATF_REQUIRE((kev[0].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE((kev[1].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[1].data == 0);

	ATF_REQUIRE(close(p[0]) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 2);
	{
		ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
		ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
		ATF_REQUIRE(kev[0].flags ==
		    (EV_EOF | EV_CLEAR | EV_ONESHOT | EV_RECEIPT));
		ATF_REQUIRE(kev[0].fflags == 0);
		ATF_REQUIRE(kev[0].data == 16384);
		ATF_REQUIRE(kev[0].udata == 0);
	}
	{
		ATF_REQUIRE(kev[1].ident == (uintptr_t)p[1]);
		ATF_REQUIRE(kev[1].filter == EVFILT_READ);
		ATF_REQUIRE(kev[1].flags == (EV_EOF | EV_CLEAR | EV_RECEIPT));
		ATF_REQUIRE(kev[1].fflags == 0);
		ATF_REQUIRE(kev[1].data == 0);
		ATF_REQUIRE(kev[1].udata == 0);
	}

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(pipe_kqueue__closed_write_end);
ATF_TC_BODY(pipe_kqueue__closed_write_end, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0);
	ATF_REQUIRE(p[0] >= 0);
	ATF_REQUIRE(p[1] >= 0);

	char c = 0;
	ssize_t r;
	while ((r = write(p[1], &c, 1)) == 1) {
	}
	ATF_REQUIRE(r < 0);
	ATF_REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);

	ATF_REQUIRE(close(p[1]) == 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[0], EVFILT_READ, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);
	EV_SET(&kev[1], p[0], EVFILT_WRITE, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);

	/*
	 * Trying to register EVFILT_WRITE when the pipe is closed leads to an
	 * EPIPE error.
	 */

	ATF_REQUIRE(kevent(kq, kev, 2, kev, 2, NULL) == 2);
	ATF_REQUIRE((kev[0].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE((kev[1].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[1].data == EPIPE);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_READ);
	ATF_REQUIRE(kev[0].flags == (EV_EOF | EV_CLEAR | EV_RECEIPT));
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 65536);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
}

ATF_TC_WITHOUT_HEAD(pipe_kqueue__closed_write_end_register_before_close);
ATF_TC_BODY(pipe_kqueue__closed_write_end_register_before_close, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0);
	ATF_REQUIRE(p[0] >= 0);
	ATF_REQUIRE(p[1] >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[0], EVFILT_READ, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);
	EV_SET(&kev[1], p[0], EVFILT_WRITE, EV_ADD | EV_CLEAR | EV_RECEIPT, /**/
	    0, 0, 0);

	/*
	 * Registering EVFILT_WRITE before the pipe is closed leads to a
	 * EVFILT_WRITE event with EV_EOF set.
	 */

	ATF_REQUIRE(kevent(kq, kev, 2, kev, 2, NULL) == 2);
	ATF_REQUIRE((kev[0].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE((kev[1].flags & EV_ERROR) != 0);
	ATF_REQUIRE(kev[1].data == 0);

	char c = 0;
	ssize_t r;
	while ((r = write(p[1], &c, 1)) == 1) {
	}
	ATF_REQUIRE(r < 0);
	ATF_REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);

	ATF_REQUIRE(close(p[1]) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 2);
	{
		ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
		ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
		ATF_REQUIRE(kev[0].flags ==
		    (EV_EOF | EV_CLEAR | EV_ONESHOT | EV_RECEIPT));
		ATF_REQUIRE(kev[0].fflags == 0);
		ATF_REQUIRE(kev[0].data == 4096 ||
		    kev[0].data == 512 /* on FreeBSD 11.3 */);
		ATF_REQUIRE(kev[0].udata == 0);
	}
	{
		ATF_REQUIRE(kev[1].ident == (uintptr_t)p[0]);
		ATF_REQUIRE(kev[1].filter == EVFILT_READ);
		ATF_REQUIRE(kev[1].flags == (EV_EOF | EV_CLEAR | EV_RECEIPT));
		ATF_REQUIRE(kev[1].fflags == 0);
		ATF_REQUIRE(kev[1].data == 65536);
		ATF_REQUIRE(kev[1].udata == 0);
	}

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
}

ATF_TC_WITHOUT_HEAD(pipe_kqueue__evfilt_vnode);
ATF_TC_BODY(pipe_kqueue__evfilt_vnode, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0);
	ATF_REQUIRE(p[0] >= 0);
	ATF_REQUIRE(p[1] >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	/* Trying to register EVFILT_VNODE on a pipe must fail. */

	struct kevent kev;
	EV_SET(&kev, p[0], EVFILT_VNODE, EV_ADD | EV_CLEAR,
	    NOTE_DELETE | NOTE_RENAME, 0, 0);

	ATF_REQUIRE_ERRNO(EINVAL, kevent(kq, &kev, 1, NULL, 0, NULL) < 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pipe_kqueue__write_end);
	ATF_TP_ADD_TC(tp, pipe_kqueue__closed_read_end);
	ATF_TP_ADD_TC(tp, pipe_kqueue__closed_read_end_register_before_close);
	ATF_TP_ADD_TC(tp, pipe_kqueue__closed_write_end);
	ATF_TP_ADD_TC(tp, pipe_kqueue__closed_write_end_register_before_close);
	ATF_TP_ADD_TC(tp, pipe_kqueue__evfilt_vnode);

	return atf_no_error();
}
