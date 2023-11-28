#pragma once

#include <any>

#include <singleton_dclp.hpp>

#include <helpers/KeyHelper.h>

namespace components {
    class WindowManager : public SingletonDclp<WindowManager> {
    public:
        WindowManager();

        ~WindowManager() override;

        bool checkNeedCancelWhenLostFocus(int64_t windowHandle);

        bool checkNeedCancelWhenGainFocus(int64_t windowHandle);

        void interactionPaste(const std::any& = {});

        void requestRetrieveInfo();

        bool sendAcceptCompletion();

        bool sendCancelCompletion();

        bool sendDoubleInsert() const;

        bool sendInsertCompletion();

        bool sendSave();

        bool sendUndo();

    private:
        helpers::KeyHelper _keyHelper;
        std::atomic<bool> _isRunning{true}, _needRetrieveInfo{false};
        std::atomic<int64_t> _codeWindowHandle{-1}, _needFocusWindow{-1}, _popListWindowHandle{-1};
        std::atomic<types::Time> _debounceFocusWindowTime, _debounceRetrieveInfoTime;

        void _cancelRetrieveInfo();

        void _threadDebounceFocusWindow();

        void _threadDebounceRetrieveInfo();
    };
}
