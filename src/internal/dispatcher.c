#include "dispatcher.h"

#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>

#define MAX_QUEUE 128

typedef struct
{
    dispatcher_callback_t fn;
    void *context;
} handler_entry_t;

typedef struct
{
    const void *payload;
    handler_entry_t handler;
} dispatch_task_t;

struct dispatcher
{
    handler_entry_t *handlers;
    size_t handler_cap;
    size_t handler_count;

    pthread_t *threads;
    size_t num_threads;
    int threaded;

    dispatch_task_t queue[MAX_QUEUE];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_size;

    int pending_tasks;

    pthread_mutex_t mutex;
    pthread_cond_t cv_task;
    pthread_cond_t cv_space;
    pthread_cond_t cv_done;

    atomic_bool running;
};

static void *dispatcher_thread(void *arg)
{
    dispatcher_t *d = (dispatcher_t *)arg;

    while (atomic_load(&d->running))
    {
        pthread_mutex_lock(&d->mutex);
        while (d->queue_size == 0 && atomic_load(&d->running))
        {
            pthread_cond_wait(&d->cv_task, &d->mutex);
        }

        if (!atomic_load(&d->running))
        {
            pthread_mutex_unlock(&d->mutex);
            break;
        }

        // Pop one task
        dispatch_task_t task = d->queue[d->queue_head];
        d->queue_head = (d->queue_head + 1) % MAX_QUEUE;
        d->queue_size--;
        pthread_cond_signal(&d->cv_space);
        pthread_mutex_unlock(&d->mutex);

        task.handler.fn(task.payload, task.handler.context);

        pthread_mutex_lock(&d->mutex);
        d->pending_tasks--;
        if (d->pending_tasks == 0)
        {
            pthread_cond_signal(&d->cv_done);
        }
        pthread_mutex_unlock(&d->mutex);
    }

    return NULL;
}

dispatcher_t *dispatcher_create(size_t max_handlers, size_t num_threads)
{
    dispatcher_t *d = calloc(1, sizeof(dispatcher_t));
    if (!d)
        return NULL;

    d->threaded = (num_threads > 0);
    d->num_threads = num_threads;
    d->handlers = calloc(max_handlers, sizeof(handler_entry_t));
    d->handler_cap = max_handlers;

    pthread_mutex_init(&d->mutex, NULL);
    pthread_cond_init(&d->cv_task, NULL);
    pthread_cond_init(&d->cv_space, NULL);
    pthread_cond_init(&d->cv_done, NULL);
    atomic_store(&d->running, 1);

    if (d->threaded)
    {
        d->threads = calloc(num_threads, sizeof(pthread_t));
        for (size_t i = 0; i < num_threads; i++)
        {
            pthread_create(&d->threads[i], NULL, dispatcher_thread, d);
        }
    }

    return d;
}

void dispatcher_destroy(dispatcher_t *d)
{
    if (!d)
        return;

    atomic_store(&d->running, 0);
    pthread_cond_broadcast(&d->cv_task);
    if (d->threaded)
    {
        for (size_t i = 0; i < d->num_threads; i++)
        {
            pthread_join(d->threads[i], NULL);
        }
    }

    pthread_mutex_destroy(&d->mutex);
    pthread_cond_destroy(&d->cv_task);
    pthread_cond_destroy(&d->cv_space);
    pthread_cond_destroy(&d->cv_done);

    free(d->handlers);
    free(d->threads);
    free(d);
}

int dispatcher_register(dispatcher_t *d, dispatcher_callback_t fn, void *context)
{
    if (!d || !fn)
        return -1;

    pthread_mutex_lock(&d->mutex);

    if (d->handler_count >= d->handler_cap)
    {
        pthread_mutex_unlock(&d->mutex);
        return -1;
    }

    for (size_t i = 0; i < d->handler_count; i++)
    {
        if (d->handlers[i].fn == fn && d->handlers[i].context == context)
        {
            pthread_mutex_unlock(&d->mutex);
            return 0; // already registered
        }
    }

    d->handlers[d->handler_count++] = (handler_entry_t){fn, context};

    pthread_mutex_unlock(&d->mutex);
    return 0;
}

int dispatcher_unregister(dispatcher_t *d, dispatcher_callback_t fn, void *context)
{
    if (!d || !fn)
        return -1;

    pthread_mutex_lock(&d->mutex);

    for (size_t i = 0; i < d->handler_count; i++)
    {
        if (d->handlers[i].fn == fn && d->handlers[i].context == context)
        {
            for (size_t j = i; j < d->handler_count - 1; j++)
            {
                d->handlers[j] = d->handlers[j + 1];
            }
            d->handler_count--;
            pthread_mutex_unlock(&d->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&d->mutex);
    return -1;
}

void dispatcher_emit(dispatcher_t *d, const void *payload)
{
    if (!d || !payload)
        return;

    pthread_mutex_lock(&d->mutex);

    if (!d->threaded)
    {
        // Call all handlers immediately
        for (size_t i = 0; i < d->handler_count; i++)
        {
            d->handlers[i].fn(payload, d->handlers[i].context);
        }
        pthread_mutex_unlock(&d->mutex);
        return;
    }

    // Wait for enough space
    while (d->queue_size + d->handler_count > MAX_QUEUE)
    {
        pthread_cond_wait(&d->cv_space, &d->mutex);
    }

    // Enqueue one task per handler
    for (size_t i = 0; i < d->handler_count; i++)
    {
        d->queue[d->queue_tail] = (dispatch_task_t){
            .payload = payload,
            .handler = d->handlers[i],
        };
        d->queue_tail = (d->queue_tail + 1) % MAX_QUEUE;
        d->queue_size++;
        d->pending_tasks++;
    }

    pthread_cond_broadcast(&d->cv_task);

    // Wait for all handlers to complete
    while (d->pending_tasks > 0)
    {
        pthread_cond_wait(&d->cv_done, &d->mutex);
    }

    pthread_mutex_unlock(&d->mutex);
}
