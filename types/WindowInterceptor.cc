#include <magic_enum.hpp>

#include <helpers/KeyHelper.h>
#include <types/common.h>
#include <types/CursorMonitor.h>
#include <types/RegistryMonitor.h>
#include <types/WindowInterceptor.h>
#include <utils/logger.h>
#include <utils/window.h>

#include <windows.h>

using namespace helpers;
using namespace magic_enum;
using namespace std;
using namespace types;
using namespace utils;

WindowInterceptor::WindowInterceptor() {
    _windowHook = shared_ptr<void>(SetWindowsHookEx(
            WH_CALLWNDPROC,
            WindowInterceptor::_windowProcedureHook,
            nullptr,
            GetCurrentThreadId()
    ), UnhookWindowsHookEx);
    if (!_windowHook) {
        throw runtime_error("Failed to set window hook.");
    }
}

void WindowInterceptor::addHandler(UserAction userAction, WindowInterceptor::CallBackFunction function) {
    _handlers[userAction] = std::move(function);
}

long WindowInterceptor::_windowProcedureHook(int nCode, unsigned int wParam, long lParam) {
    GetInstance()->_processWindowMessage(lParam);
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void WindowInterceptor::_processWindowMessage(long lParam) {
    const auto windowProcData = reinterpret_cast<PCWPSTRUCT>(lParam);
    const auto currentWindow = windowProcData->hwnd;
    if (window::getWindowClassName(currentWindow) == "si_Sw") {
        switch (windowProcData->message) {
            case WM_KILLFOCUS: {
                const auto targetWindowClass = window::getWindowClassName(
                        reinterpret_cast<HWND>(windowProcData->wParam));
                if (_codeWindow >= 0 && targetWindowClass != "si_Poplist") {
                    /*logger::log(format(
                            "Coding window '{}' lost focus. (0x{:08X} '{}') to (0x{:08X} '{}')",
                            window::getWindowText(currentWindow),
                            reinterpret_cast<uint64_t>(currentWindow),
                            window::getWindowClassName(currentWindow),
                            static_cast<uint64_t>(windowProcData->wParam),
                            targetWindowClass
                    ));*/
                    _handlers.at(UserAction::Navigate)(-1);
                    _codeWindow.store(-1);
                } else if (targetWindowClass == "si_Poplist") {
                    _popListWindow.store(windowProcData->wParam);
                    logger::log("PopList show up.");
                }
                break;
            }
            case WM_MOUSEACTIVATE: {
                CursorMonitor::GetInstance()->setAction(UserAction::Navigate);
                break;
            }
            case WM_SETFOCUS: {
                /*logger::log(format(
                        "Coding window '{}' set focus. (0x{:08X} '{}') from (0x{:08X} '{}')",
                        window::getWindowText(currentWindow),
                        reinterpret_cast<uint64_t>(currentWindow),
                        window::getWindowClassName(currentWindow),
                        static_cast<uint64_t>(windowProcData->wParam),
                        window::getWindowClassName(reinterpret_cast<HWND>(windowProcData->wParam))
                ));*/
                if (_codeWindow < 0) {
                    _codeWindow.store(reinterpret_cast<int64_t>(currentWindow));
                }
                if (_popListWindow > 0) {
                    logger::log("PopList disappeared.");
                    _popListWindow.store(-1);
                    sendCancelCompletion();
                    /*thread([this] {
                        this_thread::sleep_for(chrono::milliseconds(10));
                        sendCancelCompletion();
                    }).detach();*/
                }
                break;
            }
            case UM_KEYCODE: {
                _handleKeycode(windowProcData->wParam);
                logger::log("Processing keycode...");
                break;
            }
            default: {
                break;
            }
        }
    }
}

void WindowInterceptor::_handleKeycode(unsigned int keycode) noexcept {
    try {
        switch (keycode) {
            case enum_integer(Key::BackSpace): {
                window::sendKeycode(_codeWindow, KeyHelper::toKeycode(Key::Insert));
                window::sendKeycode(_codeWindow, KeyHelper::toKeycode(Key::Insert));
                CursorMonitor::GetInstance()->setAction(UserAction::DeleteBackward);
                break;
            }
            case enum_integer(Key::Tab): {
                _handlers.at(UserAction::Accept)(keycode);
                break;
            }
            case enum_integer(Key::Enter): {
                _handlers.at(UserAction::ModifyLine)(keycode);
                break;
            }
            case enum_integer(Key::Escape): {
                _handlers.at(UserAction::Navigate)(keycode);
                break;
            }
            case enum_integer(Key::Delete): {
                _handlers.at(UserAction::DeleteForward)(keycode);
                break;
            }
            case enum_integer(Modifier::Ctrl) + enum_integer(Key::S): {
                RegistryMonitor::GetInstance()->cancelBySave();
                break;
            }
            case enum_integer(Modifier::Ctrl) + enum_integer(Key::Z): {
                RegistryMonitor::GetInstance()->cancelByUndo();
                break;
            }
            default: {
                if (keycode >= enum_integer(Key::Space) && keycode <= enum_integer(Key::Tilde) &&
                    keycode != enum_integer(Key::RightCurlyBracket)) {
                    window::sendKeycode(_codeWindow, KeyHelper::toKeycode(Key::Insert));
                    window::sendKeycode(_codeWindow, KeyHelper::toKeycode(Key::Insert));
                    _handlers.at(UserAction::Normal)(keycode);
                } else if (((keycode & 0x802F) >= 0x8021 && (keycode & 0x802F) <= 0x8029)) {
                    /// See "WinUser.h" Line 515
                    CursorMonitor::GetInstance()->setAction(UserAction::Navigate);
                }
                break;
            }
        }
    } catch (...) {}
}

bool WindowInterceptor::sendAcceptCompletion() {
    return window::postKeycode(
            _codeWindow,
            KeyHelper::toKeycode(Key::F10, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowInterceptor::sendCancelCompletion() {
    return window::postKeycode(
            _codeWindow,
            KeyHelper::toKeycode(Key::F9, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowInterceptor::sendInsertCompletion() {
    return window::postKeycode(
            _codeWindow,
            KeyHelper::toKeycode(Key::F12, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowInterceptor::sendRetrieveInfo() {
    return window::postKeycode(
            _codeWindow,
            KeyHelper::toKeycode(Key::F11, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowInterceptor::sendSave() {
    return window::postKeycode(
            _codeWindow,
            KeyHelper::toKeycode(Key::S, Modifier::Ctrl)
    );
}

bool WindowInterceptor::sendUndo() {
    return window::postKeycode(
            _codeWindow,
            KeyHelper::toKeycode(Key::Z, Modifier::Ctrl)
    );
}