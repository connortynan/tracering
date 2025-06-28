#include <tracering/receiver.hpp>
#include <tracering/adapter/stack_trace.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
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
#include <cmath>

struct SpanData
{
    std::string full_path;
    uint64_t start_timestamp;
    uint64_t end_timestamp;
    uint32_t thread_id;
};

struct Color
{
    uint8_t r, g, b, a;
    Color(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
};

class StackTraceWindowGUI
{
private:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    TTF_Font *font = nullptr;
    TTF_Font *small_font = nullptr;

    std::vector<SpanData> spans;
    std::map<uint32_t, std::vector<size_t>> thread_spans;
    std::vector<uint32_t> thread_ids;
    std::vector<uint32_t> selected_threads;
    std::map<uint32_t, Color> thread_colors;

    bool recording = false;
    bool gui_active = false;
    bool thread_selection_mode = false;
    int thread_selection_idx = 0;
    int block_scroll_offset = 0;

    // Window dimensions
    int window_width = 1200;
    int window_height = 800;

    // UI layout constants
    const int HEADER_HEIGHT = 60;
    const int TIMELINE_HEIGHT = 40;
    const int THREAD_LABEL_WIDTH = 200;
    const int BUTTON_HEIGHT = 30;
    const int MARGIN = 10;

    struct SpanBlock
    {
        std::string path;
        std::vector<uint32_t> threads_with_span;
    };

    // Visualization parameters
    uint64_t min_timestamp = 0;
    uint64_t max_timestamp = 0;
    double zoom_factor = 1.0;
    double pan_offset = 0.0;

    // Mouse state
    bool mouse_dragging = false;
    int last_mouse_x = 0;
    int last_mouse_y = 0;

    std::atomic<bool> keep_running{true};

    // Color palette
    const std::vector<Color> color_palette = {
        Color(255, 100, 100), Color(100, 255, 100), Color(100, 100, 255),
        Color(255, 255, 100), Color(255, 100, 255), Color(100, 255, 255),
        Color(255, 200, 100), Color(200, 100, 255), Color(100, 255, 200)};

public:
    ~StackTraceWindowGUI()
    {
        cleanup();
    }

    void cleanup()
    {
        if (font)
            TTF_CloseFont(font);
        if (small_font)
            TTF_CloseFont(small_font);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
    }

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
            thread_colors[data.thread_id] = color_palette[color_idx];
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

    bool init_sdl()
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
            return false;
        }

        if (TTF_Init() == -1)
        {
            fprintf(stderr, "SDL_ttf could not initialize! SDL_ttf Error: %s\n", TTF_GetError());
            return false;
        }

        window = SDL_CreateWindow("Stack Trace Visualizer",
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  window_width, window_height,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window)
        {
            fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
            return false;
        }

        // Try to load a system font
        font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14);
        if (!font)
        {
            // Fallback to a more common font path
            font = TTF_OpenFont("/usr/share/fonts/TTF/arial.ttf", 14);
        }
        if (!font)
        {
            fprintf(stderr, "Failed to load font! SDL_ttf Error: %s\n", TTF_GetError());
            return false;
        }

        small_font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 10);
        if (!small_font)
        {
            small_font = TTF_OpenFont("/usr/share/fonts/TTF/arial.ttf", 10);
        }

        return true;
    }

    void draw_text(const std::string &text, int x, int y, Color color = Color(255, 255, 255), TTF_Font *use_font = nullptr)
    {
        if (!use_font)
            use_font = font;
        if (!use_font)
            return;

        SDL_Color sdl_color = {color.r, color.g, color.b, color.a};
        SDL_Surface *text_surface = TTF_RenderText_Solid(use_font, text.c_str(), sdl_color);
        if (!text_surface)
            return;

        SDL_Texture *text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
        if (!text_texture)
        {
            SDL_FreeSurface(text_surface);
            return;
        }

        int text_width = text_surface->w;
        int text_height = text_surface->h;
        SDL_FreeSurface(text_surface);

        SDL_Rect render_quad = {x, y, text_width, text_height};
        SDL_RenderCopy(renderer, text_texture, nullptr, &render_quad);
        SDL_DestroyTexture(text_texture);
    }

    void draw_button(const std::string &text, int x, int y, int width, int height, bool pressed = false, Color text_color = Color(255, 255, 255))
    {
        Color bg_color = pressed ? Color(100, 100, 100) : Color(60, 60, 60);
        Color border_color = Color(150, 150, 150);

        SDL_SetRenderDrawColor(renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
        SDL_Rect button_rect = {x, y, width, height};
        SDL_RenderFillRect(renderer, &button_rect);

        SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
        SDL_RenderDrawRect(renderer, &button_rect);

        // Center text in button
        int text_x = x + (width - text.length() * 7.5) / 2; // Rough text width estimation
        int text_y = y + (height - 14) / 2;
        draw_text(text, text_x, text_y, text_color);
    }

    bool is_point_in_rect(int px, int py, int x, int y, int w, int h)
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    void start_recording()
    {
        spans.clear();
        thread_spans.clear();
        thread_ids.clear();
        thread_colors.clear();
        selected_threads.clear();
        recording = true;

        // Reset visualization parameters
        zoom_factor = 1.0;
        pan_offset = 0.0;
        block_scroll_offset = 0;
    }

    void stop_recording()
    {
        recording = false;
        if (!spans.empty())
        {
            std::sort(thread_ids.begin(), thread_ids.end());
            selected_threads = thread_ids; // Select all by default
            thread_selection_idx = 0;
            block_scroll_offset = 0;
        }
    }

    void draw_gui()
    {
        SDL_GetWindowSize(window, &window_width, &window_height);

        // Clear screen
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        // Draw header
        draw_text("Stack Trace Visualizer", MARGIN, MARGIN);

        // Draw control buttons
        int button_y = MARGIN;
        int button_x = window_width - 300;

        draw_button(recording ? "Stop Recording" : "Start Recording", button_x, button_y, 120, BUTTON_HEIGHT);
        // Disable thread button if recording or no spans
        bool thread_button_disabled = recording || spans.empty();
        Color text_color = thread_button_disabled ? Color(128, 128, 128) : Color(255, 255, 255);
        draw_button("Thread Selection", button_x + 130, button_y, 130, BUTTON_HEIGHT, false, text_color);

        if (recording)
        {
            draw_text("Recording... (" + std::to_string(spans.size()) + " spans)",
                      MARGIN, MARGIN + 25, Color(255, 100, 100));
            SDL_RenderPresent(renderer);
            return;
        }

        if (spans.empty())
        {
            draw_text("No spans recorded. Click 'Start Recording' to begin.",
                      MARGIN, window_height / 2, Color(200, 200, 200));
            SDL_RenderPresent(renderer);
            return;
        }

        if (thread_selection_mode)
        {
            draw_thread_selection();
            SDL_RenderPresent(renderer);
            return;
        }

        if (selected_threads.empty())
        {
            draw_text("No threads selected. Click 'Thread Selection' to select threads.",
                      MARGIN, window_height / 2, Color(200, 200, 200));
            SDL_RenderPresent(renderer);
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

        // Draw timeline
        int timeline_y = HEADER_HEIGHT;
        double start_ms = (visible_start - min_timestamp) / 1e6;
        double end_ms = (visible_end - min_timestamp) / 1e6;

        draw_text(std::to_string(start_ms) + " ms", THREAD_LABEL_WIDTH, timeline_y);

        std::string end_label = std::to_string(end_ms) + " ms";
        int text_width = end_label.length() * 10; // crude estimate
        draw_text(end_label, window_width - MARGIN - text_width, timeline_y);

        // Draw timeline scale
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        int timeline_start_x = THREAD_LABEL_WIDTH;
        int timeline_width = window_width - THREAD_LABEL_WIDTH - MARGIN;
        SDL_Rect timeline_rect = {timeline_start_x, timeline_y + 20, timeline_width, 2};
        SDL_RenderFillRect(renderer, &timeline_rect);

        // Collect and draw span blocks
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

        std::sort(span_blocks.begin(), span_blocks.end(),
                  [](const SpanBlock &a, const SpanBlock &b)
                  { return a.path < b.path; });

        // Draw blocks
        int y_pos = HEADER_HEIGHT + TIMELINE_HEIGHT + MARGIN;
        int row_height = 25;

        for (size_t i = block_scroll_offset; i < span_blocks.size(); ++i)
        {
            const auto &block = span_blocks[i];
            if (y_pos >= window_height - MARGIN)
                break;

            // Draw span path label
            std::string label = block.path.length() > 30 ? block.path.substr(0, 27) + "..." : block.path;
            draw_text(label, MARGIN, y_pos, Color(200, 200, 200));
            y_pos += row_height;

            // Draw thread spans
            for (uint32_t tid : block.threads_with_span)
            {
                if (y_pos >= window_height - MARGIN)
                    break;

                Color thread_color = thread_colors[tid];

                // Draw thread ID
                draw_text("T" + std::to_string(tid), MARGIN, y_pos, thread_color);

                // Draw spans for this thread
                for (size_t idx : thread_spans[tid])
                {
                    const SpanData &s = spans[idx];

                    if (s.full_path != block.path)
                        continue;

                    // Clamp span timestamps to visible range
                    uint64_t clamped_start = std::max(s.start_timestamp, visible_start);
                    uint64_t clamped_end = std::min(s.end_timestamp, visible_end);

                    if (clamped_start >= clamped_end)
                        continue;

                    double start_ratio = (double)(clamped_start - visible_start) / visible_duration;
                    double end_ratio = (double)(clamped_end - visible_start) / visible_duration;

                    int bar_start = timeline_start_x + (int)(start_ratio * timeline_width);
                    int bar_end = timeline_start_x + (int)(end_ratio * timeline_width);
                    int bar_width = std::max(2, bar_end - bar_start);

                    SDL_SetRenderDrawColor(renderer, thread_color.r, thread_color.g, thread_color.b, 200);
                    SDL_Rect span_rect = {bar_start + 1, y_pos + 1, bar_width - 2, row_height - 4};
                    SDL_RenderFillRect(renderer, &span_rect);

                    // Draw border
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    SDL_RenderDrawRect(renderer, &span_rect);
                }

                y_pos += row_height;
            }
        }

        // Draw instructions
        draw_text("Mouse: Drag to pan, Wheel to zoom, Right-click for options",
                  MARGIN, window_height - 20, Color(150, 150, 150), small_font);

        SDL_RenderPresent(renderer);
    }

    void draw_thread_selection()
    {
        // Clear screen
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        draw_text("Thread Selection", MARGIN, MARGIN);
        draw_text("Click to toggle thread visibility", MARGIN, MARGIN + 25, Color(150, 150, 150));

        draw_button("Close", window_width - 100, MARGIN, 80, BUTTON_HEIGHT);

        int y_pos = HEADER_HEIGHT + MARGIN;
        int checkbox_size = 20;

        for (size_t i = 0; i < thread_ids.size(); ++i)
        {
            uint32_t thread_id = thread_ids[i];
            bool selected = std::find(selected_threads.begin(), selected_threads.end(), thread_id) != selected_threads.end();
            Color thread_color = thread_colors[thread_id];

            // Draw checkbox
            SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
            SDL_Rect checkbox_rect = {MARGIN, y_pos, checkbox_size, checkbox_size};
            SDL_RenderFillRect(renderer, &checkbox_rect);

            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
            SDL_RenderDrawRect(renderer, &checkbox_rect);

            if (selected)
            {
                SDL_SetRenderDrawColor(renderer, thread_color.r, thread_color.g, thread_color.b, 255);
                SDL_Rect check_rect = {MARGIN + 3, y_pos + 3, checkbox_size - 6, checkbox_size - 6};
                SDL_RenderFillRect(renderer, &check_rect);
            }

            // Draw thread label
            std::string label = "Thread " + std::to_string(thread_id);
            draw_text(label, MARGIN + checkbox_size + 10, y_pos + 2, thread_color);

            y_pos += 30;
        }
    }

    void handle_mouse_click(int x, int y, bool right_click = false)
    {
        (void)right_click;

        if (thread_selection_mode)
        {
            // Check close button
            if (is_point_in_rect(x, y, window_width - 100, MARGIN, 80, BUTTON_HEIGHT))
            {
                thread_selection_mode = false;
                return;
            }

            // Check thread checkboxes
            int y_pos = HEADER_HEIGHT + MARGIN;
            for (size_t i = 0; i < thread_ids.size(); ++i)
            {
                if (is_point_in_rect(x, y, MARGIN, y_pos, 200, 20))
                {
                    uint32_t thread_id = thread_ids[i];
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
                    break;
                }
                y_pos += 30;
            }
            return;
        }

        // Check control buttons
        int button_y = MARGIN;
        int button_x = window_width - 300;

        if (is_point_in_rect(x, y, button_x, button_y, 120, BUTTON_HEIGHT))
        {
            if (recording)
                stop_recording();
            else
                start_recording();
            return;
        }

        if (is_point_in_rect(x, y, button_x + 130, button_y, 120, BUTTON_HEIGHT))
        {
            if (!recording && !spans.empty())
                thread_selection_mode = true;
            return;
        }
    }

    void handle_mouse_wheel(int wheel_y)
    {
        if (thread_selection_mode)
            return;

        double zoom_change = wheel_y > 0 ? 1.2 : 1.0 / 1.2;
        zoom_factor *= zoom_change;
        zoom_factor = std::max(1.0, zoom_factor);
    }

    void handle_mouse_drag(int dx, int dy)
    {
        if (thread_selection_mode)
            return;

        // Pan horizontally
        double pan_change = -(double)dx / (window_width - THREAD_LABEL_WIDTH) / zoom_factor;
        pan_offset += pan_change;
        pan_offset = std::clamp(pan_offset, 0.0, 1.0 - 1.0 / zoom_factor);

        // Scroll vertically
        block_scroll_offset -= dy / 25;
        block_scroll_offset = std::max(0, block_scroll_offset);
    }

    void run_gui()
    {
        SDL_Event e;

        while (keep_running && gui_active)
        {
            while (SDL_PollEvent(&e))
            {
                switch (e.type)
                {
                case SDL_QUIT:
                    gui_active = false;
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT)
                    {
                        handle_mouse_click(e.button.x, e.button.y);
                        mouse_dragging = true;
                        last_mouse_x = e.button.x;
                        last_mouse_y = e.button.y;
                    }
                    else if (e.button.button == SDL_BUTTON_RIGHT)
                    {
                        handle_mouse_click(e.button.x, e.button.y, true);
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT)
                    {
                        mouse_dragging = false;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (mouse_dragging)
                    {
                        int dx = e.motion.x - last_mouse_x;
                        int dy = e.motion.y - last_mouse_y;
                        handle_mouse_drag(dx, dy);
                        last_mouse_x = e.motion.x;
                        last_mouse_y = e.motion.y;
                    }
                    break;

                case SDL_MOUSEWHEEL:
                    handle_mouse_wheel(e.wheel.y);
                    break;

                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym)
                    {
                    case SDLK_ESCAPE:
                        if (thread_selection_mode)
                            thread_selection_mode = false;
                        else
                            gui_active = false;
                        break;
                    case SDLK_r:
                        if (recording)
                            stop_recording();
                        else
                            start_recording();
                        break;
                    case SDLK_t:
                        thread_selection_mode = !thread_selection_mode;
                        break;
                    }
                    break;
                }
            }

            draw_gui();
            SDL_Delay(16); // ~60 FPS
        }
    }

    int run()
    {
        if (!init_sdl())
            return 1;

        tracering::receiver::init();
        if (tracering::adapter::stack_trace::init() != 0)
        {
            fprintf(stderr, "Failed to initialize stack trace adapter\n");
            return 1;
        }

        tracering::adapter::stack_trace::register_handler(
            [this](const trace_span_t *span)
            {
                if (keep_running)
                    this->add_span(span);
            });

        std::thread receiver_thread([this]()
                                    {
        while (keep_running) {
            tracering::receiver::poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } });

        gui_active = true;
        run_gui();

        // Clean shutdown
        keep_running = false;
        receiver_thread.join();

        tracering::adapter::stack_trace::shutdown();
        tracering::receiver::shutdown();

        return 0;
    }
};

static StackTraceWindowGUI *gui_instance = nullptr;

void signal_handler(int)
{
    if (gui_instance)
    {
        gui_instance->signal_handler();
    }
}

int main()
{
    StackTraceWindowGUI gui;
    gui_instance = &gui;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    return gui.run();
}