#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include "coro.h"

struct coro_pthread {
	pthread_t thread;
	pthread_cond_t *cond;
	pthread_mutex_t *mutex;
	void (*fun)(Coro, void *);
	void **arg_ptr;
};

static _Thread_local pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static _Thread_local pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local void *my_arg;

static void *
trampoline(void *thread_arg)
{
	struct coro_pthread **coros = (struct coro_pthread **)thread_arg;
	void (*fun)(Coro, void *);
	void *arg;
	struct coro_pthread parent_coro = *coros[1];

	pthread_mutex_lock(&mutex);
	{
		pthread_mutex_lock(parent_coro.mutex);
		coros[0]->cond = &cond;
		coros[0]->mutex = &mutex;
		coros[0]->arg_ptr = &my_arg;
		fun = coros[0]->fun;
		pthread_cond_signal(parent_coro.cond);
		pthread_mutex_unlock(parent_coro.mutex);
	}
	pthread_cond_wait(&cond, &mutex);
	arg = my_arg;
	pthread_mutex_unlock(&mutex);

	fun(&parent_coro, arg);

	return (NULL);
}

Coro
coro_create(size_t size, void (*fun)(Coro, void *))
{
	struct coro_pthread *coros[2];
	struct coro_pthread my_coro = {
		.cond = &cond, .mutex = &mutex, .arg_ptr = &my_arg
	};

	(void)size;

	coros[0] = malloc(sizeof(struct coro_pthread));
	if (!coros[0]) {
		return (NULL);
	}

	coros[1] = &my_coro;

	pthread_mutex_lock(&mutex);
	{
		coros[0]->fun = fun;
		if (pthread_create(&coros[0]->thread, NULL, /**/
		        trampoline, coros) < 0) {
			pthread_mutex_unlock(&mutex);
			free(coros[0]);
			return (NULL);
		}
	}
	pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	return (coros[0]);
}

void *
coro_transfer(Coro coro_p, void *arg)
{
	struct coro_pthread *coro = (struct coro_pthread *)coro_p;
	void *arg_local;

	pthread_mutex_lock(&mutex);
	{
		pthread_mutex_lock(coro->mutex);
		*coro->arg_ptr = arg;
		pthread_cond_signal(coro->cond);
		pthread_mutex_unlock(coro->mutex);
	}
	pthread_cond_wait(&cond, &mutex);
	arg_local = my_arg;
	pthread_mutex_unlock(&mutex);

	return (arg_local);
}

void
coro_destroy(Coro coro_p)
{
	struct coro_pthread *coro = (struct coro_pthread *)coro_p;

	{
		pthread_mutex_lock(coro->mutex);
		pthread_cond_signal(coro->cond);
		pthread_mutex_unlock(coro->mutex);
	}
	pthread_join(coro->thread, NULL);
	free(coro_p);
}
