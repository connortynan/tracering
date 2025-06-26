#include "tracering/adapter/stack_trace.h"
#include "tracering/adapter/stack_trace_ex.h"
#include "tracering/receiver.h"
#include "../internal/dispatcher.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>

#define MAX_STACK_DEPTH 32
#define MAX_THREADS 64

typedef struct
{
    char label[TRACE_EVENT_PAYLOAD_MAX];
    char full_path[256];
    uint64_t start_timestamp;
} stack_entry_t;

typedef struct
{
    uint32_t thread_id;
    stack_entry_t stack[MAX_STACK_DEPTH];
    int stack_top;
    int active;
} thread_stack_t;

static thread_stack_t thread_stacks[MAX_THREADS];
static pthread_mutex_t adapter_mutex = PTHREAD_MUTEX_INITIALIZER;
static dispatcher_t *span_dispatcher = NULL;

static thread_stack_t *get_thread_stack(uint32_t thread_id)
{
    for (int i = 0; i < MAX_THREADS; ++i)
    {
        if (thread_stacks[i].active && thread_stacks[i].thread_id == thread_id)
            return &thread_stacks[i];
    }
    for (int i = 0; i < MAX_THREADS; ++i)
    {
        if (!thread_stacks[i].active)
        {
            thread_stacks[i].thread_id = thread_id;
            thread_stacks[i].stack_top = -1;
            thread_stacks[i].active = 1;
            return &thread_stacks[i];
        }
    }
    return NULL;
}

static void notify_handlers(const trace_span_t *span)
{
    dispatcher_emit(span_dispatcher, span);
}

void stack_trace_event_handler(const trace_event_t *event)
{
    if (!event || !event->data[0])
        return;

    pthread_mutex_lock(&adapter_mutex);
    thread_stack_t *ts = get_thread_stack(event->thread_id);
    if (!ts)
    {
        pthread_mutex_unlock(&adapter_mutex);
        return;
    }

    if (ts->stack_top >= 0 && strcmp(ts->stack[ts->stack_top].label, event->data) == 0)
    {
        stack_entry_t popped = ts->stack[ts->stack_top--];

        trace_span_t span = {
            .start_timestamp = popped.start_timestamp,
            .end_timestamp = event->timestamp,
            .thread_id = event->thread_id};

        snprintf(span.full_path, sizeof(span.full_path), "%s", popped.full_path);

        pthread_mutex_unlock(&adapter_mutex);
        notify_handlers(&span);
    }
    else if (ts->stack_top < MAX_STACK_DEPTH - 1)
    {
        ts->stack_top++;
        stack_entry_t *entry = &ts->stack[ts->stack_top];
        snprintf(entry->label, sizeof(entry->label), "%s", event->data);
        entry->start_timestamp = event->timestamp;

        if (ts->stack_top == 0)
        {
            strncpy(entry->full_path, entry->label, sizeof(entry->full_path) - 1);
        }
        else
        {
            // Copy to temp buffer to avoid overlap in snprintf
            char temp_path[sizeof(entry->full_path)];
            snprintf(temp_path, sizeof(temp_path), "%s;%s",
                     ts->stack[ts->stack_top - 1].full_path,
                     entry->label);
            memcpy(entry->full_path, temp_path, sizeof(entry->full_path));
        }
        entry->full_path[sizeof(entry->full_path) - 1] = '\0';
        pthread_mutex_unlock(&adapter_mutex);
    }
    else
    {
        pthread_mutex_unlock(&adapter_mutex);
    }
}

int tracer_adapter_stktrce_init(void)
{
    span_dispatcher = dispatcher_create(sizeof(trace_span_t), 0);
    tracer_receiver_register_handler(stack_trace_event_handler);

    pthread_mutex_lock(&adapter_mutex);
    memset(thread_stacks, 0, sizeof(thread_stacks));
    pthread_mutex_unlock(&adapter_mutex);

    return 0;
}

void tracer_adapter_stktrce_shutdown(void)
{
    pthread_mutex_lock(&adapter_mutex);
    memset(thread_stacks, 0, sizeof(thread_stacks));
    pthread_mutex_unlock(&adapter_mutex);

    dispatcher_destroy(span_dispatcher);
    span_dispatcher = NULL;
}

void tracer_adapter_stktrce_register_handler_ex(trace_span_handler_ex_t fn, void *ctx)
{
    dispatcher_register(span_dispatcher, (dispatcher_callback_t)fn, ctx);
}

void tracer_adapter_stktrce_unregister_handler_ex(trace_span_handler_ex_t fn, void *ctx)
{
    dispatcher_unregister(span_dispatcher, (dispatcher_callback_t)fn, ctx);
}

static void adapter(const void *event, void *ctx)
{
    ((trace_event_handler_t)ctx)((const trace_event_t *)event);
}

void tracer_adapter_stktrce_register_handler(trace_span_handler_t fn)
{
    dispatcher_register(span_dispatcher, adapter, (void *)fn);
}

void tracer_adapter_stktrce_unregister_handler(trace_span_handler_t fn)
{
    dispatcher_unregister(span_dispatcher, adapter, (void *)fn);
}
