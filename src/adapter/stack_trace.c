#include "tracering/adapter/stack_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_STACK_DEPTH 32
#define MAX_HANDLERS 16
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
    int stack_top; // -1 means empty stack
    int active;    // 1 if this slot is in use
} thread_stack_t;

static thread_stack_t thread_stacks[MAX_THREADS];
static trace_span_handler_t handlers[MAX_HANDLERS];
static int handler_count = 0;
static pthread_mutex_t adapter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Fast thread stack lookup - linear search is fine for small MAX_THREADS
static thread_stack_t *get_thread_stack(uint32_t thread_id)
{
    // First try to find existing thread stack
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (thread_stacks[i].active && thread_stacks[i].thread_id == thread_id)
        {
            return &thread_stacks[i];
        }
    }

    // Find empty slot for new thread
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (!thread_stacks[i].active)
        {
            thread_stacks[i].thread_id = thread_id;
            thread_stacks[i].stack_top = -1;
            thread_stacks[i].active = 1;
            return &thread_stacks[i];
        }
    }

    return NULL; // No available slots
}

static void notify_handlers(const trace_span_t *span)
{
    for (int i = 0; i < handler_count; i++)
    {
        if (handlers[i])
        {
            handlers[i](span);
        }
    }
}

int stack_trace_adapter_init(void)
{
    pthread_mutex_lock(&adapter_mutex);

    // Initialize all thread stacks
    for (int i = 0; i < MAX_THREADS; i++)
    {
        thread_stacks[i].active = 0;
        thread_stacks[i].stack_top = -1;
    }

    // Clear handlers
    handler_count = 0;
    for (int i = 0; i < MAX_HANDLERS; i++)
    {
        handlers[i] = NULL;
    }

    pthread_mutex_unlock(&adapter_mutex);
    return 0;
}

void stack_trace_adapter_shutdown(void)
{
    pthread_mutex_lock(&adapter_mutex);

    // Clear all thread stacks
    for (int i = 0; i < MAX_THREADS; i++)
    {
        thread_stacks[i].active = 0;
    }

    // Clear handlers
    handler_count = 0;

    pthread_mutex_unlock(&adapter_mutex);
}

void stack_trace_adapter_register_handler(trace_span_handler_t handler)
{
    if (!handler)
        return;

    pthread_mutex_lock(&adapter_mutex);

    if (handler_count < MAX_HANDLERS)
    {
        handlers[handler_count++] = handler;
    }

    pthread_mutex_unlock(&adapter_mutex);
}

void stack_trace_adapter_unregister_handler(trace_span_handler_t handler)
{
    if (!handler)
        return;

    pthread_mutex_lock(&adapter_mutex);

    // Find and remove handler, shift remaining handlers down
    for (int i = 0; i < handler_count; i++)
    {
        if (handlers[i] == handler)
        {
            for (int j = i; j < handler_count - 1; j++)
            {
                handlers[j] = handlers[j + 1];
            }
            handler_count--;
            handlers[handler_count] = NULL;
            break;
        }
    }

    pthread_mutex_unlock(&adapter_mutex);
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
        return; // No available thread slots
    }

    // Check if this is an end event (matches top of stack)
    if (ts->stack_top >= 0 && strcmp(ts->stack[ts->stack_top].label, event->data) == 0)
    {
        // End event - pop stack and emit span
        stack_entry_t popped = ts->stack[ts->stack_top];
        ts->stack_top--;

        trace_span_t span;
        strncpy(span.full_path, popped.full_path, sizeof(span.full_path) - 1);
        span.start_timestamp = popped.start_timestamp;
        span.end_timestamp = event->timestamp;
        span.thread_id = event->thread_id;

        pthread_mutex_unlock(&adapter_mutex);
        notify_handlers(&span);
    }
    else
    {
        // Start event - push onto stack
        if (ts->stack_top < MAX_STACK_DEPTH - 1)
        {
            ts->stack_top++;
            stack_entry_t *entry = &ts->stack[ts->stack_top];

            strncpy(ts->stack[ts->stack_top].label, event->data, sizeof(ts->stack[ts->stack_top].label) - 1);
            ts->stack[ts->stack_top].start_timestamp = event->timestamp;
            if (ts->stack_top == 0)
            {
                // Top-level: full path is just the label
                strncpy(entry->full_path, event->data, sizeof(entry->full_path) - 1);
                entry->full_path[sizeof(entry->full_path) - 1] = '\0';
            }
            else
            {
                // Build from parent's full_path + ";" + label
                const char *parent_path = ts->stack[ts->stack_top - 1].full_path;
                snprintf(entry->full_path, sizeof(entry->full_path), "%s;%s", parent_path, entry->label); // todo: fix truncation warning
                entry->full_path[sizeof(entry->full_path) - 1] = '\0';
            }
        }
        pthread_mutex_unlock(&adapter_mutex);
    }
}