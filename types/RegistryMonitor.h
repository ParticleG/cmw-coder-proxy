#pragma once

#include <chrono>

#include <singleton_dclp.hpp>

#include <types/CursorPosition.h>

namespace types {
    class RegistryMonitor : public SingletonDclp<RegistryMonitor> {
    public:
        RegistryMonitor();

        ~RegistryMonitor() override;

        void acceptByTab(unsigned int);

        void cancelByCursorNavigate(CursorPosition, CursorPosition);

        void cancelByDeleteBackward(CursorPosition oldPosition, CursorPosition newPosition);

        void cancelByKeycodeNavigate(unsigned int);

        void cancelByModifyLine(unsigned int keycode);

        void cancelBySave();

        void cancelByUndo();

        void retrieveEditorInfo(unsigned int);

    private:
        const std::string _subKey = R"(SOFTWARE\Source Dynamics\Source Insight\3.0)";
        std::string _projectId, _projectHash;
        std::atomic<bool> _isRunning = true, _hasCompletion = false, _justInserted = false;
        std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>> _lastTriggerTime;
    };
}
