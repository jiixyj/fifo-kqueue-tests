#include <stddef.h>
#include <stdlib.h>

#include "coro.h"

#include "libco.h"

struct coro_libco {
	cothread_t coro_ctx;
	void (*fun)(Coro, void *);
	void *arg;
};

static _Thread_local struct coro_libco parent_coro_global;

static void
trampoline()
{
	struct coro_libco parent_coro = parent_coro_global;

	parent_coro.fun(&parent_coro, parent_coro.arg);
}

Coro
coro_create(size_t size, void (*fun)(Coro, void *))
{
	struct coro_libco *coro;

	coro = malloc(sizeof(struct coro_libco));
	if (!coro) {
		return NULL;
	}

	coro->coro_ctx = co_create(size, trampoline);
	if (!coro->coro_ctx) {
		free(coro);
		return (NULL);
	}
	coro->fun = fun;
	coro->arg = NULL;

	return (coro);
}

void *
coro_transfer(Coro coro_p, void *arg)
{
	struct coro_libco *coro = (struct coro_libco *)coro_p;

	parent_coro_global.coro_ctx = co_active();
	parent_coro_global.fun = coro->fun;
	parent_coro_global.arg = arg;

	co_switch(coro->coro_ctx);

	return parent_coro_global.arg;
}

void
coro_destroy(Coro coro_p)
{
	struct coro_libco *coro = (struct coro_libco *)coro_p;

	co_delete(coro->coro_ctx);
	free(coro_p);
}
