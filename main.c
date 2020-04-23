#include <sys/event.h>
#include <sys/stat.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "coro.h"

#define FIFONAME "fifo.tmp"
#define PIPE_SIZE (16384)

static void
pollfd(
    /* two return values: first for the long running kqueue, one for newly
       connecting reader/writer. */
    int *pr, int *pr2,

    /* fifo fd, a long running kqueue and a flag to open(2) if a new
       reader/writer should be tested. */
    int fd, int kq, bool recursion, int recursion_open_flag,

    /* poll events which are expected */
    int expected_nr_poll_events, int expected_revents,
    /* kevents from long running kqueue which are expected */
    int expected_nr_kq_events, int expected_kq_filter, int expected_kq_data,
    int expected_kq_flags,

    /* same as above, but for newly connected readers/writers */
    int new_expected_nr_poll_events, int new_expected_revents,
    int new_expected_nr_kq_events, int new_expected_kq_filter,
    int new_expected_kq_data, int new_expected_kq_flags)
{
	int r = 0;
	int r2 = 0;
	struct pollfd pfd = { .fd = fd, /**/
		.events = POLLIN | POLLPRI | POLLOUT };
	bool is_in_recursion = !pr2;

#define WARN_STR (is_in_recursion ? "new r/w" : "old r/w")

	if (poll(&pfd, 1, 0) != expected_nr_poll_events) {
		warnx("ERROR - %s: poll failed", WARN_STR);
		r = -1;
	}
	if (pfd.revents != expected_revents) {
		warnx("ERROR - %s: expected %d, got %d", WARN_STR,
		    expected_revents, pfd.revents);
		r = -1;
	}

	struct kevent kev[16];

	int n;
	struct timespec ts = { 0, 0 };
	if ((n = kevent(kq, NULL, 0, kev, 16, &ts)) < 0) {
		err(1, "kevent");
	}
#if 0
	if (n == 0) {
		fprintf(stderr, "got no events\n");
	}
#endif
	for (int i = 0; i < n; ++i) {
#if 0
		fprintf(stderr, "got event: %d\n", (int)kev[i].filter);
		fprintf(stderr, " fd: %d\n", (int)kev[i].ident);
		fprintf(stderr, " data: %d\n", (int)kev[i].data);
		fprintf(stderr, " flags: %x\n", (unsigned)kev[i].flags);
		fprintf(stderr, " fflags: %x\n", (unsigned)kev[i].fflags);
#endif

		if ((int)kev[i].filter != expected_kq_filter) {
			warnx("ERROR - %s: expected_kq_filter %d, received %d",
			    WARN_STR, expected_kq_filter, (int)kev[i].filter);
			r = -1;
		}
		if ((int)kev[i].data != expected_kq_data) {
			warnx("ERROR - %s: expected_kq_data %d, received %d",
			    WARN_STR, expected_kq_data, (int)kev[i].data);
			r = -1;
		}
		if ((int)kev[i].flags != (expected_kq_flags | EV_CLEAR)) {
			warnx("ERROR - %s: expected_kq_flags %x, received %x",
			    WARN_STR, (unsigned)(expected_kq_flags | EV_CLEAR),
			    (unsigned)kev[i].flags);
			r = -1;
		}
	}

	if (n != expected_nr_kq_events) {
		warnx("ERROR - %s: expected_nr_kq_events %d, received %d",
		    WARN_STR, expected_nr_kq_events, n);
		r = -1;
	}

	if (recursion) {
		int fd2;
		int kq2;
		struct kevent kev2[2];

		fd2 = open(FIFONAME, recursion_open_flag | O_NONBLOCK);
		if (fd2 < 0) {
			err(1, "open");
		}

		kq2 = kqueue();
		if (kq2 < 0) {
			err(1, "kqueue");
		}

		EV_SET(&kev2[0], fd2, EVFILT_READ, /**/
		    EV_ADD | EV_CLEAR, 0, 0, NULL);
		EV_SET(&kev2[1], fd2, EVFILT_WRITE, /**/
		    EV_ADD | EV_CLEAR, 0, 0, NULL);
		if (kevent(kq2, kev2, 2, NULL, 0, NULL) < 0) {
			err(1, "kevent");
		}

		pollfd(&r2, NULL, fd2, kq2, false, 0,
		    new_expected_nr_poll_events, new_expected_revents,
		    new_expected_nr_kq_events, new_expected_kq_filter,
		    new_expected_kq_data, new_expected_kq_flags, 0, 0, 0, 0, 0,
		    0);

		close(kq2);
		close(fd2);
	}

	*pr = r;
	if (!is_in_recursion) {
		*pr2 = r2;
	}
}

static int test_counter;

#define PRINT_TESTRESULT                                          \
	do {                                                      \
		++test_counter;                                   \
		fprintf(stderr, "test %2d %s/%s\n", test_counter, \
		    r < 0 ? "FAILED" : "SUCCESSFUL",              \
		    r2 < 0 ? "FAILED" : "SUCCESSFUL");            \
	} while (0);

static void
coro1(Coro c, void *arg)
{
	int fd;
	int kq;
	struct kevent kev[2];
	int r, r2;
	uint8_t buf[16];

	(void)arg;

	fd = open(FIFONAME, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		err(1, "open");
	}

	kq = kqueue();
	if (kq < 0) {
		err(1, "kqueue");
	}

	EV_SET(&kev[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    0, 0, 0, 0, 0, 0, /**/
	    0, 0, 0, 0, 0, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"first reader opened");

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    0, 0, 0, 0, 0, 0, /**/
	    0, 0, 0, 0, 0, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer connected, poll still returns 0");

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN, 1, EVFILT_READ, 1, 0, /**/
	    1, POLLIN, 1, EVFILT_READ, 1, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer wrote first byte, POLLIN expected");

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN | POLLHUP, 1, EVFILT_READ, 1, EV_EOF, /**/
	    1, POLLIN, 1, EVFILT_READ, 1, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer closed, POLLIN|POLLHUP expected");

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN, 1, EVFILT_READ, 1, 0, /**/
	    1, POLLIN, 1, EVFILT_READ, 1, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"new writer connected, POLLIN expected, a kevent with data 1");

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN, 1, EVFILT_READ, 2, 0, /**/
	    1, POLLIN, 1, EVFILT_READ, 2, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer wrote a byte, POLLIN expected, a kevent with data 2");

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN | POLLHUP, 1, EVFILT_READ, 2, EV_EOF, /**/
	    1, POLLIN, 1, EVFILT_READ, 2, 0);
	PRINT_TESTRESULT;

	fprintf(stderr, "writer closed connection\n");

	fprintf(stderr,
	    "now test that read of length one retriggers EVFILT_READ:\n");

	if (read(fd, buf, 1) != 1) {
		warnx("ERROR - read != 1");
	}

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN | POLLHUP, 1, EVFILT_READ, 1, EV_EOF, /**/
	    1, POLLIN, 1, EVFILT_READ, 1, 0);
	PRINT_TESTRESULT;

	fprintf(stderr,
	    "test that another read of length one retriggers EVFILT_READ:\n");

	if (read(fd, buf, sizeof(buf)) != 1) {
		warnx("ERROR - read != 1");
	}

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN | POLLHUP, 1, EVFILT_READ, 0, EV_EOF, /**/
	    0, 0, 0, 0, 0, 0);
	PRINT_TESTRESULT;

	fprintf(stderr,
	    "test that another read does not retrigger EVFILT_READ at EOF:\n");

	if (read(fd, buf, sizeof(buf)) != 0) {
		warnx("ERROR - read != 0");
	}

	pollfd(&r, &r2, fd, kq, true, O_RDONLY, /**/
	    1, POLLIN | POLLHUP, 0, 0, 0, 0, /**/
	    0, 0, 0, 0, 0, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer closed, POLLIN|POLLHUP expected");

	EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	(void)close(fd);

	(void)coro_transfer(c, /**/
	    (void *)"reader closed");

	fd = open(FIFONAME, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		err(1, "open");
	}

	EV_SET(&kev[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	(void)coro_transfer(c, /**/
	    (void *)"reader reopened");

	(void)coro_transfer(c, NULL);
}

static void
coro2(Coro c, void *arg)
{
	int fd;
	int kq;
	struct kevent kev[2];
	int r, r2;

	(void)arg;

	fd = open(FIFONAME, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		err(1, "open");
	}

	kq = kqueue();
	if (kq < 0) {
		err(1, "kqueue");
	}

	EV_SET(&kev[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"FIFO opened");

	if (write(fd, "", 1) != 1) {
		errx(1, "write failed");
	}

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 1, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 1, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"one byte written");

	EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	(void)close(fd);

	(void)coro_transfer(c, /**/
	    (void *)"writer closed");

	fd = open(FIFONAME, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		err(1, "open");
	}

	EV_SET(&kev[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 1, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 1, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer reopened");

	if (write(fd, "", 1) != 1) {
		errx(1, "write failed");
	}

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 2, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 2, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"one byte written");

	EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	(void)close(fd);

	(void)coro_transfer(c, /**/
	    (void *)"writer closed");

	fd = open(FIFONAME, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		err(1, "open");
	}

	EV_SET(&kev[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&kev[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(kq, kev, 2, NULL, 0, NULL) < 0) {
		err(1, "kevent");
	}

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"writer reopened");

	pollfd(&r, &r2, fd, kq,
	    false, /* connecting as a new writer would fail,
		    * as no readers are currently connected */
	    0, /**/
	    1, POLLHUP, 1, EVFILT_WRITE, PIPE_SIZE, EV_EOF, /**/
	    0, 0, 0, 0, 0, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"get EOF when reader closes");

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE, 0);
	PRINT_TESTRESULT;

	if (write(fd, "", 1) != 1) {
		errx(1, "write failed");
	}

	pollfd(&r, &r2, fd, kq, true, O_WRONLY, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 1, 0, /**/
	    1, POLLOUT, 1, EVFILT_WRITE, PIPE_SIZE - 1, 0);
	PRINT_TESTRESULT;

	(void)coro_transfer(c, /**/
	    (void *)"reconnected reader should trigger notification");

	(void)coro_transfer(c, NULL);
}

static void
atexit_unlink()
{

	(void)unlink(FIFONAME);
}

int
main()
{

	atexit_unlink();
	if (mkfifo(FIFONAME, 0666) < 0) {
		err(1, "mkfifo");
	}
	atexit(atexit_unlink);

	Coro c1 = coro_create(4096, coro1);
	Coro c2 = coro_create(4096, coro2);
	if (!c1 || !c2) {
		errx(1, "coro_create failed");
	}

	for (;;) {
		void *nr;

		if (!(nr = coro_transfer(c1, NULL))) {
			break;
		}
		fprintf(stderr, "reader: %s\n\n", (char const *)nr);

		if (!(nr = coro_transfer(c2, NULL))) {
			break;
		}
		fprintf(stderr, "writer: %s\n\n", (char const *)nr);
	}
}
