// WindowHelper.h
#pragma once

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#endif

namespace WindowHelper {

    // 设置无边框窗口（扩展客户区）
    inline void setupFramelessWindow(QWidget* window) {
        Q_ASSERT(window);
        if (!window) return;

#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (!hwnd || !IsWindow(hwnd)) return;

        // 扩展客户区到整个窗口
        MARGINS margins = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
#endif
    }

    // 处理原生窗口消息（边框缩放）
    inline bool handleNativeWindowEvent(QWidget* window,
        const QByteArray& eventType,
        void* message,
        qintptr* result,
        int resizeMargin) {
#ifdef Q_OS_WIN
        // 只处理Windows消息
        if (eventType != "windows_generic_MSG") return false;

        // 窗口未就绪时不处理
        if (!window || !window->isVisible()) return false;
        if (!window->testAttribute(Qt::WA_WState_Created)) return false;

        auto* msg = static_cast<MSG*>(message);

        // 只处理窗口管理相关消息
        if (msg->message != WM_NCCALCSIZE &&
            msg->message != WM_NCHITTEST &&
            msg->message != WM_GETMINMAXINFO) {
            return false;
        }

        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (!hwnd || !IsWindow(hwnd)) return false;

        switch (msg->message) {
        case WM_NCCALCSIZE:
            if (msg->wParam == TRUE) {
                *result = 0;  // 取消非客户区计算
                return true;
            }
            break;

        case WM_NCHITTEST: {
            RECT rect;
            if (!GetWindowRect(hwnd, &rect)) break;
            if (rect.right <= rect.left || rect.bottom <= rect.top) break;

            int border = resizeMargin;
            if (IsZoomed(hwnd)) break;  // 最大化时不需要边框缩放

            int xPos = GET_X_LPARAM(msg->lParam);
            int yPos = GET_Y_LPARAM(msg->lParam);

            // 四个角
            if (xPos <= rect.left + border && yPos <= rect.top + border) {
                *result = static_cast<qintptr>(HTTOPLEFT);
                return true;
            }
            if (xPos >= rect.right - border && yPos <= rect.top + border) {
                *result = static_cast<qintptr>(HTTOPRIGHT);
                return true;
            }
            if (xPos <= rect.left + border && yPos >= rect.bottom - border) {
                *result = static_cast<qintptr>(HTBOTTOMLEFT);
                return true;
            }
            if (xPos >= rect.right - border && yPos >= rect.bottom - border) {
                *result = static_cast<qintptr>(HTBOTTOMRIGHT);
                return true;
            }
            // 四条边
            if (xPos <= rect.left + border) {
                *result = static_cast<qintptr>(HTLEFT);
                return true;
            }
            if (xPos >= rect.right - border) {
                *result = static_cast<qintptr>(HTRIGHT);
                return true;
            }
            if (yPos <= rect.top + border) {
                *result = static_cast<qintptr>(HTTOP);
                return true;
            }
            if (yPos >= rect.bottom - border) {
                *result = static_cast<qintptr>(HTBOTTOM);
                return true;
            }
            break;
        }

        case WM_GETMINMAXINFO: {
            auto* minmax = reinterpret_cast<MINMAXINFO*>(msg->lParam);
            minmax->ptMinTrackSize.x = 600;  // 最小宽度
            minmax->ptMinTrackSize.y = 400;  // 最小高度
            *result = 0;
            return true;
        }

        default:
            break;
        }
#else
        Q_UNUSED(window);
        Q_UNUSED(eventType);
        Q_UNUSED(message);
        Q_UNUSED(result);
        Q_UNUSED(resizeMargin);
#endif
        return false;
    }

} // namespace WindowHelper