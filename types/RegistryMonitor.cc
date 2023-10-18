#include <chrono>
//#include <ranges>
#include <regex>

#include <nlohmann/json.hpp>
#include <httplib.h>

#include <types/Configurator.h>
#include <types/RegistryMonitor.h>
#include <types/UserAction.h>
#include <types/WindowInterceptor.h>
#include <utils/crypto.h>
#include <utils/inputbox.h>
#include <utils/logger.h>
#include <utils/system.h>

using namespace std;
using namespace std::ranges;
using namespace types;
using namespace utils;

namespace {
    const regex editorInfoRegex(
            R"regex(^cursor="(.*?)";path="(.*?)";project="(.*?)";tabs="(.*?)";type="(.*?)";version="(.*?)";symbols="(.*?)";prefix="(.*?)";suffix="(.*?)"$)regex");
    const regex cursorRegex(
            R"regex(^lnFirst="(.*?)";ichFirst="(.*?)";lnLast="(.*?)";ichLim="(.*?)";fExtended="(.*?)";fRect="(.*?)"$)regex");

    optional<string> generateCompletion(const string &editorInfo, const string &projectId) {
        nlohmann::json requestBody = {
                {"info",      crypto::encode(editorInfo, crypto::Encoding::Base64)},
                {"projectId", projectId},
        };
        auto client = httplib::Client("http://localhost:3000");
        client.set_connection_timeout(5);
        if (auto res = client.Post(
                "/generate",
                requestBody.dump(),
                "application/json"
        )) {
            const auto responseBody = nlohmann::json::parse(res->body);
            const auto result = responseBody["result"].get<string>();
            const auto contents = responseBody["contents"];
            if (result == "success" && contents.is_array() && !contents.empty() && !contents[0].get<string>().empty()) {
                return crypto::decode(contents[0].get<string>(), crypto::Encoding::Base64);
            }
            logger::log(format("HTTP result: {}", result));
        } else {
            logger::log(format("HTTP error: {}", httplib::to_string(res.error())));
        }
        return nullopt;
    }

    void completionReaction(const string &projectId) {
        try {
            nlohmann::json requestBody = {
                    {"code_line",   1},
                    {"mode",        false},
                    {"project_id",  projectId},
                    {"tab_output",  true},
                    {"total_lines", 1},
                    {"text_length", 1},
                    {"username",    Configurator::GetInstance()->username()},
                    {"version",     "SI-0.5.3"},
            };
            auto client = httplib::Client("http://10.113.10.68:4322");
            client.set_connection_timeout(5);
            client.Post("/code/statistical", requestBody.dump(), "application/json");
        } catch (...) {}
    }
}

RegistryMonitor::RegistryMonitor() {
    thread([this] {
        while (this->_isRunning.load()) {
            try {
                const auto editorInfoString = system::getRegValue(_subKey, "editorInfo");

                logger::log(editorInfoString);

                smatch editorInfoRegexResults;
                if (!regex_match(editorInfoString, editorInfoRegexResults, editorInfoRegex) ||
                    editorInfoRegexResults.size() != 10) {
                    logger::log("Invalid editorInfoString");
                    continue;
                }

                {
                    const auto currentProjectHash = crypto::sha1(
                            regex_replace(editorInfoRegexResults[3].str(), regex(R"(\\\\)"), "/")
                    );

                    if (_projectHash != currentProjectHash) {
                        _projectId.clear();
                        _projectHash = currentProjectHash;
                    }
                }

                if (_projectId.empty()) {
                    const auto projectListKey = _subKey + "\\Project List";
                    while (_projectId.empty()) {
                        try {
                            _projectId = system::getRegValue(projectListKey, _projectHash);
                        } catch (...) {
                            _projectId = InputBox("Please input current project's iSoft ID", "Input Project ID");
                            if (_projectId.empty()) {
                                logger::error("Project ID is empty.");
                            } else {
                                system::setRegValue(projectListKey, _projectHash, _projectId);
                            }
                        }
                    }
                }
                // TODO: Finish completionType
                nlohmann::json editorInfo = {
                        {"cursor",          nlohmann::json::object()},
                        {"currentFilePath", regex_replace(editorInfoRegexResults[2].str(), regex(R"(\\\\)"), "/")},
                        {"projectFolder",   regex_replace(editorInfoRegexResults[3].str(), regex(R"(\\\\)"), "/")},
                        {"openedTabs",      nlohmann::json::array()},
                        {"completionType",  stoi(editorInfoRegexResults[5].str()) > 0 ? "snippet" : "line"},
                        {"version",         editorInfoRegexResults[6].str()},
                        {"symbols",         nlohmann::json::array()},
                        {"prefix",          editorInfoRegexResults[8].str()},
                        {"suffix",          editorInfoRegexResults[9].str()},
                };

                {
                    const auto cursorString = regex_replace(editorInfoRegexResults[1].str(), regex(R"(\\)"), "");
                    smatch cursorRegexResults;
                    if (!regex_match(cursorString, cursorRegexResults, cursorRegex) ||
                        cursorRegexResults.size() != 7) {
                        logger::log("Invalid cursorString");
                        continue;
                    }
                    editorInfo["cursor"] = {
                            {"startLine",      cursorRegexResults[1].str()},
                            {"startCharacter", cursorRegexResults[2].str()},
                            {"endLine",        cursorRegexResults[3].str()},
                            {"endCharacter",   cursorRegexResults[4].str()},
                    };
                }

                {
                    const auto symbolString = editorInfoRegexResults[7].str();
                    editorInfo["symbols"] = nlohmann::json::array();
                    if (symbolString.length() > 2) {
                        for (const auto symbol: views::split(symbolString.substr(1, symbolString.length() - 1), "||")) {
                            const auto symbolComponents = views::split(symbol, "|")
                                                          | views::transform(
                                    [](auto &&rng) { return string(&*rng.begin(), ranges::distance(rng)); })
                                                          | to<vector>();

                            editorInfo["symbols"].push_back(
                                    {
                                            {"name",      symbolComponents[0]},
                                            {"path",      symbolComponents[1]},
                                            {"startLine", symbolComponents[2]},
                                            {"endLine",   symbolComponents[3]},
                                    }
                            );
                        }
                    }
                }

                {
                    const auto tabsString = editorInfoRegexResults[4].str();
                    auto searchStart(tabsString.cbegin());
                    smatch tabsRegexResults;
                    while (regex_search(
                            searchStart,
                            tabsString.cend(),
                            tabsRegexResults,
                            regex(R"regex(.*?\.([ch]))regex")
                    )) {
                        editorInfo["openedTabs"].push_back(tabsRegexResults[0].str());
                        searchStart = tabsRegexResults.suffix().first;
                    }
                }

                logger::log(editorInfo.dump());

                _lastTriggerTime = chrono::high_resolution_clock::now();
                system::deleteRegValue(_subKey, "editorInfo");
                thread([this, editorInfoString, currentTriggerName = _lastTriggerTime.load()] {
                    const auto completionGenerated = generateCompletion(editorInfoString, _projectId);
                    if (completionGenerated.has_value() && currentTriggerName == _lastTriggerTime.load()) {
                        system::setRegValue(_subKey, "completionGenerated", completionGenerated.value());
                        WindowInterceptor::GetInstance()->sendInsertCompletion();
                        _hasCompletion = true;
                    }
                }).detach();
            } catch (runtime_error &e) {
            } catch (exception &e) {
                logger::log(e.what());
            } catch (...) {
                logger::log("Unknown exception.");
            }
            this_thread::sleep_for(chrono::milliseconds(5));
        }
    }).detach();
}

RegistryMonitor::~RegistryMonitor() {
    this->_isRunning.store(false);
}

void RegistryMonitor::acceptByTab(unsigned int) {
    if (_hasCompletion.load()) {
        _hasCompletion = false;
        WindowInterceptor::GetInstance()->sendAcceptCompletion();
        thread(completionReaction, _projectId).detach();
        logger::log("Accepted completion");
    }
}

void RegistryMonitor::cancelByCursorNavigate(CursorPosition, CursorPosition) {
    cancelByKeycodeNavigate(-1);
}

void RegistryMonitor::cancelByDeleteBackward(CursorPosition oldPosition, CursorPosition newPosition) {
    if (oldPosition.line == newPosition.line) {
        if (!_hasCompletion.load()) {
            return;
        }
        try {
            system::setRegValue(_subKey, "cancelType", to_string(static_cast<int>(UserAction::DeleteBackward)));
            WindowInterceptor::GetInstance()->sendCancelCompletion();
            _hasCompletion = false;
            logger::log("Canceled by delete backward.");
        } catch (runtime_error &e) {
            logger::log(e.what());
        }
    } else {
        cancelByModifyLine(-1);
    }
}

void RegistryMonitor::cancelByKeycodeNavigate(unsigned int) {
    if (!_hasCompletion.load()) {
        return;
    }
    try {
        system::setRegValue(_subKey, "cancelType", to_string(static_cast<int>(UserAction::Navigate)));
        WindowInterceptor::GetInstance()->sendCancelCompletion();
        _hasCompletion = false;
        logger::log("Canceled by toKeycode navigate.");
    } catch (runtime_error &e) {
        logger::log(e.what());
    }
}

void RegistryMonitor::cancelByModifyLine(unsigned int) {
    const auto windowInterceptor = WindowInterceptor::GetInstance();
    if (_hasCompletion.load()) {
        try {
            system::setRegValue(_subKey, "cancelType", to_string(static_cast<int>(UserAction::ModifyLine)));
            windowInterceptor->sendCancelCompletion();
            _hasCompletion = false;
            logger::log("Canceled by modify line.");
        } catch (runtime_error &e) {
            logger::log(e.what());
        }
    }

    windowInterceptor->sendRetrieveInfo();
}

void RegistryMonitor::cancelByUndo() {
    // TODO: Send undo when last accept is a completion
    const auto windowInterceptor = WindowInterceptor::GetInstance();
    if (_hasCompletion.load()) {
        _hasCompletion = false;
        windowInterceptor->sendUndo();
    }
}

void RegistryMonitor::cancelBySave() {
    if (_hasCompletion.load()) {
        const auto windowInterceptor = WindowInterceptor::GetInstance();
        windowInterceptor->sendCancelCompletion();
        _hasCompletion = false;
        windowInterceptor->sendSave();
    }
}
