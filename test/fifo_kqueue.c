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

ATF_TC_WITHOUT_HEAD(fifo_kqueue__writes);
ATF_TC_BODY(fifo_kqueue__writes, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(mkfifo("testfifo", 0600) == 0);

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);
	ATF_REQUIRE((p[1] = open("testfifo",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[1], EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, 0);
	EV_SET(&kev[1], p[1], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);

	ATF_REQUIRE(kevent(kq, kev, 2, NULL, 0, NULL) == 0);

	/* A new writer should immediately get a EVFILT_WRITE event. */

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

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
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

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev), NULL) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kev[0].flags == (EV_CLEAR | EV_EOF));
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == PIPE_BUF + 1);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(fifo_kqueue__connecting_reader);
ATF_TC_BODY(fifo_kqueue__connecting_reader, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(mkfifo("testfifo", 0600) == 0);

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);
	ATF_REQUIRE((p[1] = open("testfifo",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[1], EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, 0);
	EV_SET(&kev[1], p[1], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);

	ATF_REQUIRE(kevent(kq, kev, 2, NULL, 0, NULL) == 0);

	/* A new writer should immediately get a EVFILT_WRITE event. */

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/*
	 * Filling the pipe, reading (PIPE_BUF + 1) bytes, then closing the
	 * read end leads to a EVFILT_WRITE with EV_EOF set.
	 */

	char c = 0;
	ssize_t r;
	while ((r = write(p[1], &c, 1)) == 1) {
	}
	ATF_REQUIRE(r < 0);
	ATF_REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);

	for (int i = 0; i < PIPE_BUF + 1; ++i) {
		ATF_REQUIRE(read(p[0], &c, 1) == 1);
	}

	ATF_REQUIRE(close(p[0]) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev), NULL) == 1);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE((kev[0].flags & EV_EOF) != 0);
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/* Opening the reader again must trigger the EVFILT_WRITE. */

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	r = kevent(kq, NULL, 0, kev, nitems(kev), &(struct timespec) { 1, 0 });
	ATF_REQUIRE(r == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[1]);
	ATF_REQUIRE(kev[0].filter == EVFILT_WRITE);
	ATF_REQUIRE(kev[0].flags == EV_CLEAR);
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == PIPE_BUF + 1);
	ATF_REQUIRE(kev[0].udata == 0);
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(fifo_kqueue__reads);
ATF_TC_BODY(fifo_kqueue__reads, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(mkfifo("testfifo", 0600) == 0);

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);
	ATF_REQUIRE((p[1] = open("testfifo",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	/* Check that EVFILT_READ behaves sensibly on a FIFO reader. */

	char c = 0;
	ssize_t r;
	while ((r = write(p[1], &c, 1)) == 1) {
	}
	ATF_REQUIRE(r < 0);
	ATF_REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);

	for (int i = 0; i < PIPE_BUF + 1; ++i) {
		ATF_REQUIRE(read(p[0], &c, 1) == 1);
	}

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];
	EV_SET(&kev[0], p[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);

	ATF_REQUIRE(kevent(kq, kev, 1, NULL, 0, NULL) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_READ);
	ATF_REQUIRE(kev[0].flags == EV_CLEAR);
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 65023);
	ATF_REQUIRE(kev[0].udata == 0);

	while ((r = read(p[0], &c, 1)) == 1) {
	}
	ATF_REQUIRE(r < 0);
	ATF_REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(fifo_kqueue__read_eof_wakeups);
ATF_TC_BODY(fifo_kqueue__read_eof_wakeups, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(mkfifo("testfifo", 0600) == 0);

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);
	ATF_REQUIRE((p[1] = open("testfifo",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];

	EV_SET(&kev[0], p[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
	ATF_REQUIRE(kevent(kq, kev, 1, NULL, 0, NULL) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/*
	 * Closing the writer must trigger a EVFILT_READ edge with EV_EOF set.
	 */

	ATF_REQUIRE(close(p[1]) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_READ);
	ATF_REQUIRE(kev[0].flags == (EV_EOF | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	/*
	 * Trying to read from a closed pipe should not trigger EVFILT_READ
	 * edges.
	 */

	char c;
	ATF_REQUIRE(read(p[0], &c, 1) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
}

ATF_TC_WITHOUT_HEAD(fifo_kqueue__read_eof_state_when_reconnecting);
ATF_TC_BODY(fifo_kqueue__read_eof_state_when_reconnecting, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(mkfifo("testfifo", 0600) == 0);

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);
	ATF_REQUIRE((p[1] = open("testfifo",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	struct kevent kev[32];

	EV_SET(&kev[0], p[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
	ATF_REQUIRE(kevent(kq, kev, 1, NULL, 0, NULL) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/*
	 * Closing the writer must trigger a EVFILT_READ edge with EV_EOF set.
	 */

	ATF_REQUIRE(close(p[1]) == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_READ);
	ATF_REQUIRE(kev[0].flags == (EV_EOF | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	/* A new reader shouldn't see the EOF flag. */

	{
		int new_reader;
		ATF_REQUIRE((new_reader = open("testfifo",
				 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

		int new_kq = kqueue();
		ATF_REQUIRE(new_kq >= 0);

		struct kevent new_kev[32];
		EV_SET(&new_kev[0], new_reader, EVFILT_READ, EV_ADD | EV_CLEAR,
		    0, 0, 0);
		ATF_REQUIRE(kevent(new_kq, new_kev, 1, NULL, 0, NULL) == 0);

		ATF_REQUIRE(kevent(new_kq, NULL, 0, new_kev, nitems(new_kev),
				&(struct timespec) { 0, 0 }) == 0);

		ATF_REQUIRE(close(new_kq) == 0);
		ATF_REQUIRE(close(new_reader) == 0);
	}

	/*
	 * Simply reopening the writer does not trigger the EVFILT_READ again --
	 * EV_EOF should be cleared, but there is no data yet so the filter
	 * does not trigger.
	 */

	ATF_REQUIRE((p[1] = open("testfifo",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/* Writing a byte should trigger a EVFILT_READ. */

	char c = 0;
	ATF_REQUIRE(write(p[1], &c, 1) == 1);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_READ);
	ATF_REQUIRE(kev[0].flags == EV_CLEAR);
	ATF_REQUIRE(kev[0].fflags == 0);
	ATF_REQUIRE(kev[0].data == 1);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
	ATF_REQUIRE(close(p[1]) == 0);
}

ATF_TC_WITHOUT_HEAD(fifo_kqueue__evfilt_vnode);
ATF_TC_BODY(fifo_kqueue__evfilt_vnode, tc)
{
	int p[2] = { -1, -1 };

	ATF_REQUIRE(mkfifo("testfifo", 0600) == 0);

	ATF_REQUIRE((p[0] = open("testfifo",
			 O_RDONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

	int kq = kqueue();
	ATF_REQUIRE(kq >= 0);

	unsigned int fflags = NOTE_DELETE | NOTE_RENAME | NOTE_WRITE | NOTE_LINK
#ifdef NOTE_CLOSE
	    | NOTE_CLOSE
#endif
#ifdef NOTE_CLOSE_WRITE
	    | NOTE_CLOSE_WRITE
#endif
#ifdef NOTE_READ
	    | NOTE_READ
#endif
#ifdef NOTE_OPEN
	    | NOTE_OPEN
#endif
	    ;

	struct kevent kev[32];
	EV_SET(&kev[0], p[0], EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, 0);

	ATF_REQUIRE(kevent(kq, kev, 1, NULL, 0, NULL) == 0);
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	int SPURIOUS_EV_ADD = 0
#ifdef __DragonFly__
	    | EV_ADD
#endif
	    ;

	/* Renaming a FIFO should trigger EVFILT_VNODE/NOTE_RENAMED. */

	ATF_REQUIRE(rename("testfifo", "testfifo_renamed") == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_VNODE);
	ATF_REQUIRE(kev[0].flags == (SPURIOUS_EV_ADD | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == NOTE_RENAME);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE((p[1] = open("testfifo_renamed",
			 O_WRONLY | O_CLOEXEC | O_NONBLOCK)) >= 0);

#ifdef NOTE_OPEN
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_VNODE);
	ATF_REQUIRE(kev[0].flags == (SPURIOUS_EV_ADD | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == NOTE_OPEN);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);
#endif

	/*
	 * Writing to a FIFO does *not* trigger EVFILT_VNODE/NOTE_WRITE
	 * currently. This is probably intentional as EVFILT_READ/EVFILT_WRITE
	 * can be used instead.
	 */

	char c = 0;
	ATF_REQUIRE(write(p[1], &c, 1) == 1);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	/* Similarly, EVFILT_VNODE/NOTE_READ is not triggered on read. */

	ATF_REQUIRE(read(p[0], &c, 1) == 1);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);

	ATF_REQUIRE(close(p[1]) == 0);

#ifdef NOTE_CLOSE_WRITE
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_VNODE);
	ATF_REQUIRE(kev[0].flags == (SPURIOUS_EV_ADD | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == NOTE_CLOSE_WRITE);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);
#else
	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 0);
#endif

	ATF_REQUIRE(link("testfifo_renamed", "testfifo_link") == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_VNODE);
	ATF_REQUIRE(kev[0].flags == (SPURIOUS_EV_ADD | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == NOTE_LINK);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	/* Unlinking a FIFO should trigger EVFILT_VNODE/NOTE_DELETE. */

	ATF_REQUIRE(unlink("testfifo_link") == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_VNODE);
	ATF_REQUIRE(kev[0].flags == (SPURIOUS_EV_ADD | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == NOTE_DELETE);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(unlink("testfifo_renamed") == 0);

	ATF_REQUIRE(kevent(kq, NULL, 0, kev, nitems(kev),
			&(struct timespec) { 0, 0 }) == 1);
	ATF_REQUIRE(kev[0].ident == (uintptr_t)p[0]);
	ATF_REQUIRE(kev[0].filter == EVFILT_VNODE);
	ATF_REQUIRE(kev[0].flags == (SPURIOUS_EV_ADD | EV_CLEAR));
	ATF_REQUIRE(kev[0].fflags == NOTE_DELETE);
	ATF_REQUIRE(kev[0].data == 0);
	ATF_REQUIRE(kev[0].udata == 0);

	ATF_REQUIRE(close(kq) == 0);
	ATF_REQUIRE(close(p[0]) == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fifo_kqueue__writes);
	ATF_TP_ADD_TC(tp, fifo_kqueue__connecting_reader);
	ATF_TP_ADD_TC(tp, fifo_kqueue__reads);
	ATF_TP_ADD_TC(tp, fifo_kqueue__read_eof_wakeups);
	ATF_TP_ADD_TC(tp, fifo_kqueue__read_eof_state_when_reconnecting);
	ATF_TP_ADD_TC(tp, fifo_kqueue__evfilt_vnode);

	return atf_no_error();
}
