#include <tracering/receiver.hpp>
#include <tracering/adapter/stack_trace.hpp>

#include <ncurses.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>

struct SpanData
{
    std::string full_path;
    uint64_t start_timestamp;
    uint64_t end_timestamp;
    uint32_t thread_id;
};

class StackTraceGUI
{
private:
    std::vector<SpanData> spans;
    std::map<uint32_t, std::vector<size_t>> thread_spans; // thread_id -> span indices
    std::vector<uint32_t> thread_ids;

    bool recording = false;
    bool gui_active = false;
    int selected_thread_idx = 0;

    // Visualization parameters
    uint64_t min_timestamp = 0;
    uint64_t max_timestamp = 0;
    double zoom_factor = 1.0;
    double pan_offset = 0.0; // as fraction of total time range

    std::atomic<bool> keep_running{true};

public:
    void add_span(const trace_span_t *span)
    {
        if (!recording)
            return;

        SpanData data;
        data.full_path = span->full_path;
        data.start_timestamp = span->start_timestamp;
        data.end_timestamp = span->end_timestamp;
        data.thread_id = span->thread_id;

        spans.push_back(data);

        // Update thread mapping
        if (thread_spans.find(data.thread_id) == thread_spans.end())
        {
            thread_ids.push_back(data.thread_id);
            std::sort(thread_ids.begin(), thread_ids.end());
        }
        thread_spans[data.thread_id].push_back(spans.size() - 1);

        // Update timestamp range
        if (spans.size() == 1)
        {
            min_timestamp = data.start_timestamp;
            max_timestamp = data.end_timestamp;
        }
        else
        {
            min_timestamp = std::min(min_timestamp, data.start_timestamp);
            max_timestamp = std::max(max_timestamp, data.end_timestamp);
        }
    }

    void signal_handler()
    {
        keep_running = false;
    }

    void start_recording()
    {
        clear();
        printw("Recording traces... Press any key to stop recording.\n");
        refresh();

        spans.clear();
        thread_spans.clear();
        thread_ids.clear();
        recording = true;

        // Wait for key press
        getch();
        recording = false;

        if (spans.empty())
        {
            printw("No spans recorded!\n");
            printw("Press any key to exit...\n");
            refresh();
            getch();
            return;
        }

        // Sort thread IDs and set default selection
        std::sort(thread_ids.begin(), thread_ids.end());
        selected_thread_idx = 0;

        gui_active = true;
        run_gui();
    }

    void run_gui()
    {
        while (keep_running && gui_active)
        {
            draw_gui();
            handle_input();
        }
    }

    void draw_gui()
    {
        clear();

        if (thread_ids.empty())
        {
            printw("No threads to display\n");
            refresh();
            return;
        }

        uint32_t current_thread = thread_ids[selected_thread_idx];
        auto &current_spans = thread_spans[current_thread];

        // Calculate visible time range
        uint64_t total_duration = max_timestamp - min_timestamp;
        uint64_t visible_duration = (uint64_t)(total_duration / zoom_factor);
        uint64_t visible_start = min_timestamp + (uint64_t)(pan_offset * total_duration);
        uint64_t visible_end = visible_start + visible_duration;

        // Ensure we don't pan beyond bounds
        if (visible_end > max_timestamp)
        {
            visible_end = max_timestamp;
            visible_start = visible_end - visible_duration;
        }
        if (visible_start < min_timestamp)
        {
            visible_start = min_timestamp;
            visible_end = visible_start + visible_duration;
        }

        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Header
        mvprintw(0, 0, "Stack Trace Visualizer - Thread %u (%d/%d)",
                 current_thread, selected_thread_idx + 1, (int)thread_ids.size());

        // Time scale
        double start_ms = (visible_start - min_timestamp) / 1e6;
        double end_ms = (visible_end - min_timestamp) / 1e6;
        mvprintw(max_y - 2, 0, "%.3f ms", start_ms);
        mvprintw(max_y - 2, max_x - 20, "%.3f ms", end_ms);

        // Group spans by full_path
        std::map<std::string, std::vector<const SpanData *>> grouped_spans;
        std::vector<std::string> span_order;
        for (size_t span_idx : current_spans)
        {
            const SpanData &span = spans[span_idx];
            // Skip spans outside visible window
            if (span.end_timestamp < visible_start || span.start_timestamp > visible_end)
                continue;
            if (grouped_spans.find(span.full_path) == grouped_spans.end())
                span_order.push_back(span.full_path); // Remember first-seen order

            grouped_spans[span.full_path].push_back(&span);
        }

        // Draw spans for current thread
        int y_pos = 2;
        for (const auto &path : span_order)
        {
            const auto &span_group = grouped_spans[path];
            if (span_group.empty())
                continue;
            if (y_pos >= max_y - 3)
                break;

            // Draw label
            std::string label = path.length() > (size_t)(max_x - 1) ? path.substr(0, max_x - 4) + "..." : path;
            mvprintw(y_pos, 0, "%s", label.c_str());

            y_pos++;

            // Draw all spans for this path on the same line
            for (const SpanData *span : span_group)
            {
                double start_ratio = (double)(span->start_timestamp - visible_start) / visible_duration;
                double end_ratio = (double)(span->end_timestamp - visible_start) / visible_duration;

                int bar_start = std::max(0, std::min(max_x - 1, static_cast<int>(start_ratio * max_x)));
                int bar_end = std::max(bar_start, std::min(max_x - 1, static_cast<int>(end_ratio * max_x)));
                int bar_width = std::max(1, bar_end - bar_start);

                // Draw timing bar
                for (int x = bar_start; x < bar_start + bar_width && x < max_x; x++)
                {
                    mvaddch(y_pos, x, '=');
                }
                if (bar_start < max_x)
                    mvaddch(y_pos, bar_start, '|');
                if (bar_start + bar_width - 1 < max_x)
                    mvaddch(y_pos, bar_start + bar_width - 1, '|');
            }

            y_pos += 2; // leave space between grouped lines
        }

        // Instructions
        mvprintw(max_y - 1, 0, "Up/Down:Thread +/-:Zoom Left/Right:Pan Q:Quit");

        refresh();
    }

    void handle_input()
    {
        int ch = getch();

        switch (ch)
        {
        case 'q':
        case 'Q':
        case 27: // ESC
            gui_active = false;
            break;

        case KEY_UP:
            if (selected_thread_idx > 0)
            {
                selected_thread_idx--;
            }
            break;

        case KEY_DOWN:
            if (selected_thread_idx < (int)thread_ids.size() - 1)
            {
                selected_thread_idx++;
            }
            break;

        case '+':
        case '=':
            zoom_factor *= 1.5;
            break;

        case '-':
        case '_':
            zoom_factor /= 1.5;
            if (zoom_factor < 1.0)
                zoom_factor = 1.0;
            break;

        case KEY_LEFT:
            pan_offset -= 0.1 / zoom_factor;
            if (pan_offset < 0)
                pan_offset = 0;
            break;

        case KEY_RIGHT:
            pan_offset += 0.1 / zoom_factor;
            if (pan_offset > 1.0 - 1.0 / zoom_factor)
            {
                pan_offset = 1.0 - 1.0 / zoom_factor;
            }
            break;
        }
    }

    int run()
    {
        // Initialize ncurses
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        nodelay(stdscr, FALSE);

        // Initialize tracering
        tracering::receiver::init();
        if (tracering::adapter::stack_trace::init() != 0)
        {
            endwin();
            fprintf(stderr, "Failed to initialize stack trace adapter\n");
            return 1;
        }

        // Register span handler
        tracering::adapter::stack_trace::register_handler(
            [this](const trace_span_t *span)
            {
                this->add_span(span);
            });

        // Start receiver thread
        std::thread receiver_thread([this]()
                                    {
            while (keep_running) {
                tracering::receiver::poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } });

        // Show initial prompt
        clear();
        printw("Stack Trace Visualizer\n");
        printw("Press any key to start recording traces...\n");
        refresh();
        getch();

        start_recording();

        // Cleanup
        keep_running = false;
        receiver_thread.join();

        tracering::adapter::stack_trace::shutdown();
        tracering::receiver::shutdown();

        endwin();
        return 0;
    }
};

static StackTraceGUI *gui_instance = nullptr;

void signal_handler(int)
{
    if (gui_instance)
    {
        gui_instance->signal_handler();
    }
}

int main()
{
    StackTraceGUI gui;
    gui_instance = &gui;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    return gui.run();
}