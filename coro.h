#ifndef CORO_H_
#define CORO_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Coro;

Coro coro_create(size_t /* size */, void (*/*fun*/)(Coro, void *));
void *coro_transfer(Coro /* coro */, void * /* arg */);
void coro_destroy(Coro /* coro */);

#ifdef __cplusplus
}
#endif

#endif
