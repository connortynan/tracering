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
#include <set>

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
    std::vector<uint32_t> selected_threads;
    std::map<uint32_t, int> thread_colors; // thread_id -> color pair index

    bool recording = false;
    bool gui_active = false;
    bool thread_selection_mode = false;
    int thread_selection_idx = 0;
    int block_scroll_offset = 0;

    struct SpanBlock
    {
        std::string path;
        std::vector<uint32_t> threads_with_span;
    };

    // Visualization parameters
    uint64_t min_timestamp = 0;
    uint64_t max_timestamp = 0;
    double zoom_factor = 1.0;
    double pan_offset = 0.0; // as fraction of total time range

    std::atomic<bool> keep_running{true};

    // Color palette (indices for color pairs)
    const std::vector<int> color_palette = {COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE,
                                            COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE};

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

            // Assign color to new thread
            int color_idx = thread_ids.size() % color_palette.size();
            thread_colors[data.thread_id] = color_idx + 1; // Color pair 1+
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
        thread_colors.clear();
        selected_threads.clear();
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

        // Sort thread IDs and initialize selections
        std::sort(thread_ids.begin(), thread_ids.end());
        selected_threads = thread_ids; // Select all by default
        thread_selection_idx = 0;
        block_scroll_offset = 0;

        gui_active = true;
        run_gui();
    }

    void run_gui()
    {
        while (keep_running && gui_active)
        {
            if (thread_selection_mode)
            {
                draw_thread_selection();
            }
            else
            {
                draw_gui();
            }
            handle_input();
        }
    }

    void draw_thread_selection()
    {
        clear();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        mvprintw(0, 0, "Thread Selection");
        mvprintw(1, 0, "Use Up/Down to navigate, Space to toggle, T/Enter/ESC to exit");

        int y_pos = 3;
        for (size_t i = 0; i < thread_ids.size() && y_pos < max_y - 1; ++i)
        {
            uint32_t thread_id = thread_ids[i];
            bool selected = std::find(selected_threads.begin(), selected_threads.end(), thread_id) != selected_threads.end();
            int color_pair = thread_colors[thread_id];

            if (has_colors())
            {
                attron(COLOR_PAIR(color_pair));
            }

            if ((int)i == thread_selection_idx)
            {
                attron(A_REVERSE);
            }

            mvprintw(y_pos, 2, "[%c] Thread %u", selected ? 'X' : ' ', thread_id);

            if ((int)i == thread_selection_idx)
            {
                attroff(A_REVERSE);
            }
            if (has_colors())
            {
                attroff(COLOR_PAIR(color_pair));
            }

            y_pos++;
        }

        refresh();
    }

    void draw_gui()
    {
        clear();
        if (selected_threads.empty())
        {
            printw("No threads selected. Press 'T' to select threads.\n");
            refresh();
            return;
        }

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
        mvprintw(0, 0, "Stack Trace Visualizer - %d threads selected",
                 (int)selected_threads.size());

        // Time scale
        double start_ms = (visible_start - min_timestamp) / 1e6;
        double end_ms = (visible_end - min_timestamp) / 1e6;
        mvprintw(max_y - 2, 0, "%.3f ms", start_ms);
        mvprintw(max_y - 2, max_x - 20, "%.3f ms", end_ms);

        // Collect all unique span paths across selected threads
        // Build span blocks
        std::vector<SpanBlock> span_blocks;
        std::set<std::string> seen_paths;
        for (uint32_t tid : selected_threads)
        {
            for (size_t idx : thread_spans[tid])
            {
                const std::string &path = spans[idx].full_path;
                if (!seen_paths.count(path))
                {
                    seen_paths.insert(path);
                    SpanBlock block;
                    block.path = path;

                    for (uint32_t inner_tid : selected_threads)
                    {
                        for (size_t inner_idx : thread_spans[inner_tid])
                        {
                            if (spans[inner_idx].full_path == path)
                            {
                                block.threads_with_span.push_back(inner_tid);
                                break;
                            }
                        }
                    }

                    if (!block.threads_with_span.empty())
                        span_blocks.push_back(block);
                }
            }
        }

        // Sort blocks alphabetically by path
        std::sort(span_blocks.begin(), span_blocks.end(),
                  [](const SpanBlock &a, const SpanBlock &b)
                  { return a.path < b.path; });

        // Clamp scroll offset
        int total_lines = 0;
        for (const auto &blk : span_blocks)
            total_lines += 1 + blk.threads_with_span.size();
        int max_offset = std::max(0, (int)span_blocks.size() - 1);
        block_scroll_offset = std::clamp(block_scroll_offset, 0, max_offset);

        // Draw blocks
        int y_pos = 2;
        for (size_t i = block_scroll_offset; i < span_blocks.size(); ++i)
        {
            const auto &block = span_blocks[i];
            if (y_pos >= max_y - 2)
                break;

            std::string label = block.path.length() > (size_t)(max_x - 1)
                                    ? block.path.substr(0, max_x - 4) + "..."
                                    : block.path;
            mvprintw(y_pos++, 0, "%s", label.c_str());

            for (uint32_t tid : block.threads_with_span)
            {
                if (y_pos >= max_y - 2)
                    break;

                int color_pair = thread_colors[tid];
                if (has_colors())
                    attron(COLOR_PAIR(color_pair));
                mvaddch(y_pos, 0, '#');

                for (size_t idx : thread_spans[tid])
                {
                    const SpanData &s = spans[idx];
                    if (s.full_path != block.path)
                        continue;
                    if (s.end_timestamp < visible_start || s.start_timestamp > visible_end)
                        continue;

                    double start_ratio = (double)(s.start_timestamp - visible_start) / visible_duration;
                    double end_ratio = (double)(s.end_timestamp - visible_start) / visible_duration;
                    int bar_start = std::max(0, std::min(max_x - 1, static_cast<int>(start_ratio * max_x)));
                    int bar_end = std::max(bar_start, std::min(max_x - 1, static_cast<int>(end_ratio * max_x)));
                    int bar_width = std::max(1, bar_end - bar_start);

                    for (int x = bar_start; x < bar_start + bar_width && x < max_x; ++x)
                        mvaddch(y_pos, x, '=');
                    if (bar_start < max_x)
                        mvaddch(y_pos, bar_start, '|');
                    if (bar_start + bar_width - 1 < max_x)
                        mvaddch(y_pos, bar_start + bar_width - 1, '|');
                }

                if (has_colors())
                    attroff(COLOR_PAIR(color_pair));
                y_pos++;
            }
        }

        // Instructions
        mvprintw(max_y - 1, 0, "Up/Dn:Scroll  +/-:Zoom  L/R:Pan  T:Threads  Q:Quit");

        refresh();
    }

    void handle_input()
    {
        int ch = getch();

        if (thread_selection_mode)
        {
            switch (ch)
            {
            case 't':
            case 'T':
            case 10: // Enter
            case 27: // ESC
                thread_selection_mode = false;
                break;

            case KEY_UP:
                if (thread_selection_idx > 0)
                {
                    thread_selection_idx--;
                }
                break;

            case KEY_DOWN:
                if (thread_selection_idx < (int)thread_ids.size() - 1)
                {
                    thread_selection_idx++;
                }
                break;

            case ' ': // Space to toggle
                if (!thread_ids.empty())
                {
                    uint32_t thread_id = thread_ids[thread_selection_idx];
                    auto it = std::find(selected_threads.begin(), selected_threads.end(), thread_id);
                    if (it != selected_threads.end())
                    {
                        selected_threads.erase(it);
                    }
                    else
                    {
                        selected_threads.push_back(thread_id);
                        std::sort(selected_threads.begin(), selected_threads.end());
                    }
                }
                break;
            }
        }
        else
        {
            switch (ch)
            {
            case 'q':
            case 'Q':
            case 27: // ESC
                gui_active = false;
                break;

            case 't':
            case 'T':
                thread_selection_mode = true;
                thread_selection_idx = 0;
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

            case KEY_DOWN:
                block_scroll_offset++;
                break;
            case KEY_UP:
                block_scroll_offset = std::max(0, block_scroll_offset - 1);
                break;
            }
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

        // Initialize colors
        if (has_colors())
        {
            start_color();
            for (size_t i = 0; i < color_palette.size(); ++i)
            {
                init_pair(i + 1, color_palette[i], COLOR_BLACK);
            }
        }

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