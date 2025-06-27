#define _XOPEN_SOURCE 700

#include "tracering/receiver.h"
#include "tracering/receiver_ex.h"
#include "../internal/buffer.h"
#include "../internal/dispatcher.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static trace_shared_buffer_t *shared_buffer = NULL;
static int shm_fd = -1;
static dispatcher_t *receiver_dispatcher = NULL;

void tracer_receiver_init(void)
{
    shm_fd = shm_open(TRACE_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
        return;
    if (ftruncate(shm_fd, sizeof(trace_shared_buffer_t)) == -1)
        return;

    shared_buffer = mmap(NULL, sizeof(trace_shared_buffer_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_buffer == MAP_FAILED)
        return;

    atomic_store_explicit(&shared_buffer->read_index, 0, memory_order_release);
    atomic_store_explicit(&shared_buffer->emit_write_index, 0, memory_order_release);
    atomic_store_explicit(&shared_buffer->rec_write_index, 0, memory_order_release);

    receiver_dispatcher = dispatcher_create(/*max_handlers=*/16, /*num_threads=*/4);
}

void tracer_receiver_shutdown(void)
{
    dispatcher_destroy(receiver_dispatcher);
    receiver_dispatcher = NULL;

    if (shared_buffer)
    {
        munmap(shared_buffer, sizeof(trace_shared_buffer_t));
        shared_buffer = NULL;
    }
    if (shm_fd != -1)
    {
        close(shm_fd);
        shm_unlink(TRACE_SHM_NAME);
        shm_fd = -1;
    }
}

void tracer_receiver_poll(void)
{
    if (!shared_buffer || !receiver_dispatcher)
        return;

    uint32_t read_idx = atomic_load_explicit(&shared_buffer->read_index, memory_order_acquire);
    uint32_t write_idx = atomic_load_explicit(&shared_buffer->rec_write_index, memory_order_acquire);

    while (read_idx != write_idx)
    {
        uint32_t idx = read_idx & (TRACE_BUFFER_SIZE - 1);
        dispatcher_emit(receiver_dispatcher, &shared_buffer->events[idx]);
        atomic_store_explicit(&shared_buffer->read_index, ++read_idx, memory_order_release);
        write_idx = atomic_load_explicit(&shared_buffer->rec_write_index, memory_order_acquire);
    }
}

void tracer_receiver_register_handler_ex(trace_event_handler_ex_t fn, void *ctx)
{
    dispatcher_register(receiver_dispatcher, (dispatcher_callback_t)fn, ctx);
}

void tracer_receiver_unregister_handler_ex(trace_event_handler_ex_t fn, void *ctx)
{
    dispatcher_unregister(receiver_dispatcher, (dispatcher_callback_t)fn, ctx);
}

static void adapter(const void *event, void *ctx)
{
    ((trace_event_handler_t)ctx)((const trace_event_t *)event);
}

void tracer_receiver_register_handler(trace_event_handler_t fn)
{
    dispatcher_register(receiver_dispatcher, adapter, (void *)fn);
}

void tracer_receiver_unregister_handler(trace_event_handler_t fn)
{
    dispatcher_unregister(receiver_dispatcher, adapter, (void *)fn);
}
