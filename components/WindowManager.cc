#include <components/Configurator.h>
#include <components/WindowManager.h>
#include <types/Key.h>
#include <utils/logger.h>
#include <utils/window.h>

using namespace components;
using namespace std;
using namespace types;
using namespace utils;

WindowManager::WindowManager() : _keyHelper(Configurator::GetInstance()->version().first) {
    _threadDebounceFocusWindow();
    _threadDebounceRetrieveInfo();
}

WindowManager::~WindowManager() {
    _isRunning.store(false);
}

bool WindowManager::checkNeedCancelWhenLostFocus(const int64_t windowHandle) {
    if (const auto windowClass = window::getWindowClassName(windowHandle);
        windowClass == "si_Poplist") {
        _popListWindowHandle.store(windowHandle);
    }
    else if (_codeWindowHandle >= 0) {
        _codeWindowHandle.store(-1);
        return true;
    }
    return false;
}

bool WindowManager::checkNeedCancelWhenGainFocus(const int64_t windowHandle) {
    if (_codeWindowHandle < 0) {
        _debounceFocusWindowTime.store(chrono::high_resolution_clock::now() + chrono::milliseconds(1000));
        _needFocusWindow.store(windowHandle);
    }
    if (_popListWindowHandle > 0) {
        _popListWindowHandle.store(-1);
        return true;
    }
    return false;
}

void WindowManager::interactionPaste(const std::any&) {
    _cancelRetrieveInfo();
}

void WindowManager::requestRetrieveInfo() {
    _debounceRetrieveInfoTime.store(chrono::high_resolution_clock::now() + chrono::milliseconds(250));
    _needRetrieveInfo.store(true);
}

bool WindowManager::sendAcceptCompletion() {
    _cancelRetrieveInfo();
    return window::postKeycode(
        _codeWindowHandle,
        _keyHelper.toKeycode(Key::F10, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowManager::sendCancelCompletion() {
    _cancelRetrieveInfo();
    return window::postKeycode(
        _codeWindowHandle,
        _keyHelper.toKeycode(Key::F9, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowManager::sendDoubleInsert() const {
    return window::sendKeycode(_codeWindowHandle, _keyHelper.toKeycode(Key::Insert)) &&
           window::sendKeycode(_codeWindowHandle, _keyHelper.toKeycode(Key::Insert));
}

bool WindowManager::sendInsertCompletion() {
    _cancelRetrieveInfo();
    return window::postKeycode(
        _codeWindowHandle,
        _keyHelper.toKeycode(Key::F12, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
    );
}

bool WindowManager::sendSave() {
    _cancelRetrieveInfo();
    return window::postKeycode(
        _codeWindowHandle,
        _keyHelper.toKeycode(Key::S, Modifier::Ctrl)
    );
}

bool WindowManager::sendUndo() {
    _cancelRetrieveInfo();
    return window::postKeycode(
        _codeWindowHandle,
        _keyHelper.toKeycode(Key::Z, Modifier::Ctrl)
    );
}

void WindowManager::_cancelRetrieveInfo() {
    _needRetrieveInfo.store(false);
}

void WindowManager::_threadDebounceFocusWindow() {
    thread([this] {
        while (_isRunning.load()) {
            if (const auto needFocusWindow = _needFocusWindow.load(); needFocusWindow >= 0) {
                if (const auto deltaTime = _debounceFocusWindowTime.load() - chrono::high_resolution_clock::now();
                    deltaTime <= chrono::nanoseconds(0)) {
                    logger::log("Focusing window...");
                    _codeWindowHandle.store(needFocusWindow);
                    _needFocusWindow.store(-1);
                }
                else {
                    this_thread::sleep_for(deltaTime);
                }
            }
            else {
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }
    }).detach();
}

void WindowManager::_threadDebounceRetrieveInfo() {
    thread([this] {
        while (_isRunning.load()) {
            if (_needRetrieveInfo.load()) {
                if (const auto deltaTime = _debounceRetrieveInfoTime.load() - chrono::high_resolution_clock::now();
                    deltaTime <= chrono::nanoseconds(0)) {
                    logger::log("Sending retrieve info...");
                    window::postKeycode(
                        _codeWindowHandle,
                        _keyHelper.toKeycode(Key::F11, {Modifier::Shift, Modifier::Ctrl, Modifier::Alt})
                    );
                    _needRetrieveInfo.store(false);
                }
                else {
                    this_thread::sleep_for(deltaTime);
                }
            }
            else {
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }
    }).detach();
}
