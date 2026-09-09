#include "AppWindow.h"
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    mycobot::AppWindow::RegisterClass(hInstance);

    mycobot::AppWindow app;
    HWND hwnd = app.Create(hInstance, nCmdShow);
    if (!hwnd) return 0;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
