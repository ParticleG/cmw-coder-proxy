#pragma once

#include <shared_mutex>

#include <nlohmann/json.hpp>
#include <singleton_dclp.hpp>

#include <types/Modification.h>

namespace components {
    class ModificationManager : public SingletonDclp<ModificationManager> {
    public:
        ModificationManager() = default;

        ~ModificationManager() override = default;

        void addTab(const std::string& tabName, const std::string& path);

        void interactionDelete(const std::any&data);

        std::string getCurrentTabContent();

        void interactionNormal(const std::any&data);

        void reloadTab();

        void removeTab(const std::string&tabName);

        bool switchTab(const std::string&tabName);

    private:
        mutable std::shared_mutex _currentModificationMutex;
        std::string _currentTabName;
        std::unordered_map<std::string, types::Modification> _modificationMap;
    };
}
