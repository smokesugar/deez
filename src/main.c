#include <Windows.h>
#include <memory.h>

#include "common.h"
#include "renderer.h"

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

int CALLBACK WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR cmd_line, int cmd_show) {
    UNUSED(h_prev_instance);
    UNUSED(cmd_line);
    UNUSED(cmd_show);

    WNDCLASSA wnd_class = { 0 };
    wnd_class.hInstance = h_instance;
    wnd_class.lpfnWndProc = window_proc;
    wnd_class.lpszClassName = "deez_window_class";

    RegisterClassA(&wnd_class);

    HWND window = CreateWindowExA(0, wnd_class.lpszClassName, "Deez", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, h_instance, NULL);
    ShowWindow(window, SW_MAXIMIZE);

    Events events;
    SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)&events);

    Renderer* r = rd_init();

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