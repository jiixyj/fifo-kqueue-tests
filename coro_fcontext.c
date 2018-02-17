#include <stddef.h>
#include <stdlib.h>

#include "coro.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "fcontext.h"
#ifdef __cplusplus
}
#endif

struct coro_fcontext {
	fcontext_t coro_ctx;
	void (*fun)(Coro, void *);
	void *arg;
};

static void
trampoline(transfer_t t)
{
	struct coro_fcontext *coro = (struct coro_fcontext *)t.data;
	struct coro_fcontext parent_coro = { t.fctx, NULL, NULL };

	coro->fun(&parent_coro, coro->arg);
}

Coro
coro_create(size_t size, void (*fun)(Coro, void *))
{
	void *sp;
	struct coro_fcontext *coro;

	sp = malloc(size);
	if (!sp) {
		return (NULL);
	}
	coro = (struct coro_fcontext *)sp;
	coro->fun = fun;
	coro->coro_ctx = make_fcontext((char *)sp + size, size, trampoline);

	return (coro);
}

void *
coro_transfer(Coro coro_p, void *arg)
{
	struct coro_fcontext *coro = (struct coro_fcontext *)coro_p;
	transfer_t t;

	coro->arg = arg;
	t = jump_fcontext(coro->coro_ctx, coro);
	coro->coro_ctx = t.fctx;

	return (((struct coro_fcontext *)t.data)->arg);
}

void
coro_destroy(Coro coro)
{

	free(coro);
}
