#define _POSIX_C_SOURCE 200809L // for nanosleep
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <tracering/receiver.h>

#define SLEEP_NS(ns)                  \
    do                                \
    {                                 \
        struct timespec ts = {0, ns}; \
        nanosleep(&ts, NULL);         \
    } while (0)

static volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig)
{
    (void)sig;
    keep_running = 0;
}

void trace_event_handler(const trace_event_t *event)
{
    printf("Received event: %s (timestamp: %lu, thread_id: %u)\n",
           event->data, event->timestamp, event->thread_id);
    fflush(stdout);
}

int main(void)
{
    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    tracer_receiver_init();
    tracer_receiver_register_handler(trace_event_handler);

    while (keep_running)
    {
        tracer_receiver_poll();
        SLEEP_NS(1000000); // Sleep for 1ms
    }

    tracer_receiver_shutdown();
    return 0;
}
