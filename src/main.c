#include <Windows.h>
#include <memory.h>
#include <stdio.h>

#include "common.h"
#include "renderer.h"

static int64_t counter_start;
static int64_t counter_freq;

typedef struct {
    bool closed;
} Events;

void message_box(char* msg) {
    MessageBoxA(NULL, msg, "Deez", 0);
}

void debug_message(char* msg) {
    OutputDebugStringA(msg);
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;

    Events* e = (Events*)GetWindowLongPtrA(window, GWLP_USERDATA);

    switch (msg) {
        case WM_CLOSE:
            e->closed = true;
            break;
            
        default:
            result = DefWindowProcA(window, msg, w_param, l_param);
            break;
    }

    return  result;
}

float engine_time() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    int64_t time_i = li.QuadPart - counter_start;
    double time = (double)time_i / (double)counter_freq;
    return (float)time;
}

int CALLBACK WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR cmd_line, int cmd_show) {
    UNUSED(h_prev_instance);
    UNUSED(cmd_line);
    UNUSED(cmd_show);

    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    counter_start = li.QuadPart;
    QueryPerformanceFrequency(&li);
    counter_freq = li.QuadPart;

    WNDCLASSA wnd_class = { 0 };
    wnd_class.hInstance = h_instance;
    wnd_class.lpfnWndProc = window_proc;
    wnd_class.lpszClassName = "deez_window_class";

    RegisterClassA(&wnd_class);

    HWND window = CreateWindowExA(0, wnd_class.lpszClassName, "Deez", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, h_instance, NULL);
    ShowWindow(window, SW_MAXIMIZE);

    Events events;
    SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)&events);

    Renderer* r = rd_init(window);

    while (true) {
        memset(&events, 0, sizeof(events));
        MSG msg;
        while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (events.closed) {
            break;
        }

        rd_render(r);
    }

    rd_free(r);

    return 0;
}