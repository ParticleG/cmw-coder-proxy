#include <chrono>
#include <format>
#include <regex>

#include <magic_enum.hpp>
#include <nlohmann/json.hpp>

#include <components/CompletionManager.h>
#include <components/ConfigManager.h>
#include <components/InteractionMonitor.h>
#include <components/MemoryManipulator.h>
#include <components/ModificationManager.h>
#include <components/SymbolManager.h>
#include <components/WebsocketManager.h>
#include <components/WindowManager.h>
#include <types/CaretPosition.h>
#include <utils/iconv.h>
#include <utils/logger.h>
#include <utils/system.h>

using namespace components;
using namespace helpers;
using namespace magic_enum;
using namespace models;
using namespace std;
using namespace types;
using namespace utils;

namespace {
    const vector<string> keywords = {"class", "if", "for", "struct", "switch", "union", "while"};

    bool checkNeedRetrieveCompletion(const char character) {
        const auto memoryManipulator = MemoryManipulator::GetInstance();
        const auto currentCaretPosition = memoryManipulator->getCaretPosition();
        const auto currentFileHandle = memoryManipulator->getHandle(MemoryAddress::HandleType::File);
        const auto currentLineContent = memoryManipulator->getLineContent(currentFileHandle, currentCaretPosition.line);
        if (currentLineContent.empty() || currentCaretPosition.character < currentLineContent.size()) {
            return false;
        }
        switch (character) {
            case ';': {
                if (ranges::none_of(keywords, [&currentLineContent](const string& keyword) {
                    return regex_search(currentLineContent, regex(format(R"~(\b{}\b)~", keyword)));
                })) {
                    logger::info("Normal input. Ignore due to ';' without any keyword");
                    return false;
                }
                return true;
            }
            case '{': {
                logger::info("Normal input. Ignore due to '{'");
                return false;
            }
            case '}': {
                logger::info("Normal input. Ignore due to '}'");
                return false;
            }
            // TODO: Support more cases
            default: {
                return true;
            }
        }
    }

    struct {
        int64_t height, x, y;
    } getCaretDimensions() {
        const auto [clientX, clientY] = WindowManager::GetInstance()->getClientPosition();

        auto [height, xPosition, yPosition] = MemoryManipulator::GetInstance()->getCaretDimension();
        while (!height) {
            this_thread::sleep_for(5ms);
            const auto [
                newHeight,
                newXPosition,
                newYPosition
            ] = MemoryManipulator::GetInstance()->getCaretDimension();
            height = newHeight;
            xPosition = newXPosition;
            yPosition = newYPosition;
        }

        return {
            height,
            clientX + xPosition,
            clientY + yPosition - 1,
        };
    }
}

CompletionManager::CompletionManager() {
    _threadCheckAcceptedCompletions();
    _threadDebounceRetrieveCompletion();
    logger::info("CompletionManager is initialized");
}

CompletionManager::~CompletionManager() {
    _isRunning = false;
}

void CompletionManager::interactionCompletionAccept(const any&, bool& needBlockMessage) {
    string actionId, content;
    int64_t cacheIndex; {
        unique_lock lock(_completionCacheMutex);
        tie(content, cacheIndex) = _completionCache.reset();
    } {
        shared_lock lock(_completionsMutex);
        actionId = _completionsOpt.value().actionId;
    }
    if (!content.empty()) {
        const auto memoryManipulator = MemoryManipulator::GetInstance();
        const auto currentPosition = memoryManipulator->getCaretPosition(); {
            unique_lock lock(_editedCompletionMapMutex);
            if (_editedCompletionMap.contains(actionId)) {
                _editedCompletionMap.at(actionId).react(true);
            }
        }

        uint32_t insertedlineCount{0}, lastLineLength{0};
        for (const auto lineRange: content.substr(cacheIndex) | views::split("\n"sv)) {
            auto lineContent = string{lineRange.begin(), lineRange.end()};
            if (insertedlineCount == 0) {
                lastLineLength = currentPosition.character + 1 + lineContent.size();
                memoryManipulator->setSelectionContent(lineContent);
            } else {
                lastLineLength = lineContent.size();
                memoryManipulator->setLineContent(currentPosition.line + insertedlineCount, lineContent, true);
            }
            ++insertedlineCount;
        }
        WindowManager::GetInstance()->sendLeftThenRight();
        memoryManipulator->setCaretPosition({lastLineLength, currentPosition.line + insertedlineCount - 1}); {
            shared_lock lock(_completionsMutex);
            WebsocketManager::GetInstance()->send(CompletionAcceptClientMessage(
                _completionsOpt.value().actionId,
                get<1>(_completionsOpt.value().current())
            ));
        }
        needBlockMessage = true;
    }
}

void CompletionManager::interactionCompletionCancel(const any& data, bool&) {
    _cancelCompletion();
    logger::log("Cancel completion, Send CompletionCancel");
    try {
        if (any_cast<bool>(data)) {
            _updateNeedRetrieveCompletion();
            WindowManager::GetInstance()->sendF13();
        }
    } catch (const bad_any_cast& e) {
        logger::warn(format("Invalid interactionCompletionCancel data: {}", e.what()));
    }
}

void CompletionManager::interactionDeleteInput(const any&, bool&) {
    const auto [character, line, _] = MemoryManipulator::GetInstance()->getCaretPosition();
    try {
        if (character != 0) {
            optional<pair<char, optional<string>>> previousCacheOpt; {
                unique_lock lock(_completionCacheMutex);
                previousCacheOpt = _completionCache.previous();
            }
            if (previousCacheOpt.has_value()) {
                // Has valid cache
                if (const auto [_, completionOpt] = previousCacheOpt.value();
                    completionOpt.has_value()) {
                    WebsocketManager::GetInstance()->send(CompletionCacheClientMessage(true));
                    logger::log("Delete backward. Send CompletionCache due to cache hit");
                } else {
                    _cancelCompletion();
                    logger::log("Delete backward. Send CompletionCancel due to cache miss");
                }
            }
        } else {
            if (_hasValidCache()) {
                _isNewLine = true;
                _cancelCompletion();
                logger::log("Delete backward. Send CompletionCancel due to delete across line");
            }
            unique_lock lock(_editedCompletionMapMutex);
            for (auto& acceptedCompletion: _editedCompletionMap | views::values) {
                acceptedCompletion.removeLine(line);
            }
        }
    } catch (const bad_any_cast& e) {
        logger::log(format("Invalid delayedDelete data: {}", e.what()));
    }
}

void CompletionManager::interactionEnterInput(const any&, bool&) {
    _isNewLine = true;
    // TODO: Support 1st level cache
    if (_hasValidCache()) {
        _cancelCompletion();
        logger::log("Enter Input. Send CompletionCancel");
    }
    _updateNeedRetrieveCompletion(true, '\n');
    uint32_t line; {
        shared_lock lock(_componentsMutex);
        line = _components.caretPosition.line;
    } {
        unique_lock lock(_editedCompletionMapMutex);
        for (auto& acceptedCompletion: _editedCompletionMap | views::values) {
            acceptedCompletion.addLine(line);
        }
    }
}

void CompletionManager::interactionNavigateWithKey(const any& data, bool&) {
    try {
        switch (any_cast<Key>(data)) {
            case Key::PageDown:
            case Key::PageUp:
            case Key::Left:
            case Key::Up:
            case Key::Right:
            case Key::Down: {
                _isNewLine = true;
            }
            default: {
                break;
            }
        }
        if (_hasValidCache()) {
            _cancelCompletion();
            logger::log("Navigate with key. Send CompletionCancel");
        }
    } catch (const bad_any_cast& e) {
        logger::log(format("Invalid interactionNavigateWithKey data: {}", e.what()));
    }
}

void CompletionManager::interactionNavigateWithMouse(const any& data, bool&) {
    try {
        const auto [newCursorPosition, _] = any_cast<tuple<CaretPosition, CaretPosition>>(data); {
            shared_lock componentsLock(_componentsMutex);
            if (_components.caretPosition.line != newCursorPosition.line) {
                _isNewLine = true;
            }
            if (_components.caretPosition != newCursorPosition) {
                if (_hasValidCache()) {
                    _cancelCompletion();
                    logger::log("Navigate with mouse. Send CompletionCancel");
                }
            }
        }
        unique_lock lock(_componentsMutex);
        _components.caretPosition = newCursorPosition;
    } catch (const bad_any_cast& e) {
        logger::log(format("Invalid interactionNavigateWithMouse data: {}", e.what()));
    }
}

void CompletionManager::interactionNormalInput(const any& data, bool&) {
    try {
        bool needRetrieveCompletion = false;
        const auto character = any_cast<char>(data);
        optional<pair<char, optional<string>>> nextCacheOpt; {
            unique_lock lock(_completionCacheMutex);
            nextCacheOpt = _completionCache.next();
        }
        if (nextCacheOpt.has_value()) {
            // Has valid cache
            if (const auto [currentChar, completionOpt] = nextCacheOpt.value();
                character == currentChar) {
                // Cache hit
                if (completionOpt.has_value()) {
                    // In cache
                    WebsocketManager::GetInstance()->send(CompletionCacheClientMessage(false));
                    logger::log("Normal input. Send CompletionCache due to cache hit");
                } else {
                    // Out of cache
                    _completionCache.reset(); {
                        shared_lock lock(_completionsMutex);
                        WebsocketManager::GetInstance()->send(CompletionAcceptClientMessage(
                            _completionsOpt.value().actionId,
                            get<1>(_completionsOpt.value().current())
                        ));
                    }
                    logger::log("Normal input. Send CompletionAccept due to cache complete");
                }
            } else {
                // Cache miss
                _cancelCompletion();
                logger::log("Normal input. Send CompletionCancel due to cache miss");
                needRetrieveCompletion = true;
            }
        } else {
            // No valid cache
            needRetrieveCompletion = true;
        }
        if (needRetrieveCompletion) {
            _updateNeedRetrieveCompletion(true, character);
        }
    } catch (const bad_any_cast& e) {
        logger::warn(format("Invalid interactionNormalInput data: {}", e.what()));
    } catch (const runtime_error& e) {
        logger::warn(e.what());
    }
}

void CompletionManager::interactionPaste(const any&, bool&) {
    if (_hasValidCache()) {
        _cancelCompletion();
        logger::log("Paste. Send CompletionCancel");
    }

    if (const auto clipboardTextOpt = system::getClipboardText();
        clipboardTextOpt.has_value()) {
        WebsocketManager::GetInstance()->send(EditorPasteClientMessage(
            ranges::count(clipboardTextOpt.value(), '\n') + 1
        ));
    }

    _isNewLine = true;
}

void CompletionManager::interactionSave(const any&, bool&) {
    if (_hasValidCache()) {
        _cancelCompletion();
        logger::log("Save. Send CompletionCancel");
    }
}

void CompletionManager::interactionUndo(const any&, bool&) {
    _isNewLine = true;
    if (_hasValidCache()) {
        _cancelCompletion();
        logger::log("Undo. Send CompletionCancel");
    } else {
        // Invalidate current retrieval
        _updateNeedRetrieveCompletion(false);
    }
}

void CompletionManager::wsCompletionGenerate(nlohmann::json&& data) {
    if (const auto serverMessage = CompletionGenerateServerMessage(move(data));
        serverMessage.result == "success") {
        const auto completions = serverMessage.completions().value();
        if (completions.empty()) {
            logger::log("(WsAction::CompletionGenerate) Ignore due to empty completions");
            return;
        }
        const auto& actionId = completions.actionId;
        if (_needDiscardWsAction.load()) {
            logger::log("(WsAction::CompletionGenerate) Ignore due to debounce");
            WebsocketManager::GetInstance()->send(CompletionCancelClientMessage(actionId, false));
            return;
        }
        const auto [candidate, index] = completions.current(); {
            unique_lock lock(_completionsMutex);
            _completionsOpt.emplace(completions);
        } {
            unique_lock lock(_completionCacheMutex);
            _completionCache.reset(iconv::autoEncode(candidate));
        }
        if (const auto currentWindowHandleOpt = WindowManager::GetInstance()->getCurrentWindowHandle();
            currentWindowHandleOpt.has_value()) {
            unique_lock lock(_editedCompletionMapMutex);
            _editedCompletionMap.emplace(
                actionId,
                EditedCompletion{
                    actionId,
                    currentWindowHandleOpt.value(),
                    MemoryManipulator::GetInstance()->getCaretPosition().line,
                    candidate
                }
            );
        }

        const auto [height, x, y] = getCaretDimensions();
        WebsocketManager::GetInstance()->send(
            CompletionSelectClientMessage(completions.actionId, index, height, x, y)
        );
    } else {
        logger::warn(format(
            "(WsAction::CompletionGenerate) Result: {}\n"
            "\tMessage: {}",
            serverMessage.result,
            serverMessage.message()
        ));
    }
    WindowManager::GetInstance()->unsetMenuText();
}

void CompletionManager::_cancelCompletion() {
    optional<Completions> completionsOpt; {
        shared_lock lock(_completionsMutex);
        if (_completionsOpt.has_value()) {
            completionsOpt.emplace(_completionsOpt.value());
        }
    } {
        unique_lock lock(_completionCacheMutex);
        _completionCache.reset();
    }
    if (completionsOpt.has_value()) {
        WebsocketManager::GetInstance()->send(
            CompletionCancelClientMessage(completionsOpt.value().actionId, true)
        );
        unique_lock lock(_editedCompletionMapMutex);
        if (_editedCompletionMap.contains(completionsOpt.value().actionId)) {
            _editedCompletionMap.at(completionsOpt.value().actionId).react(false);
        }
    }
}

bool CompletionManager::_hasValidCache() const {
    bool hasValidCache; {
        shared_lock lock(_completionCacheMutex);
        hasValidCache = _completionCache.valid();
    }
    return hasValidCache;
}

void CompletionManager::_prolongRetrieveCompletion() {
    _debounceRetrieveCompletionTime.store(chrono::high_resolution_clock::now());
}

void CompletionManager::_updateNeedRetrieveCompletion(const bool need, const char character) {
    _prolongRetrieveCompletion();
    _needDiscardWsAction.store(true);
    _needRetrieveCompletion.store(need && (!character || checkNeedRetrieveCompletion(character)));
}

void CompletionManager::_sendCompletionGenerate() {
    try {
        shared_lock componentsLock(_componentsMutex);
        _needDiscardWsAction.store(false);
        WebsocketManager::GetInstance()->send(CompletionGenerateClientMessage(
            _components.caretPosition,
            _components.path,
            _components.prefix,
            _components.recentFiles,
            _components.suffix,
            _components.symbols
        ));
    } catch (const runtime_error& e) {
        logger::warn(e.what());
    }
}

void CompletionManager::_threadCheckAcceptedCompletions() {
    thread([this] {
        while (_isRunning) {
            vector<EditedCompletion> needReportCompletions{}; {
                const shared_lock lock(_editedCompletionMapMutex);
                for (const auto& acceptedCompletion: _editedCompletionMap | views::values) {
                    if (acceptedCompletion.canReport()) {
                        needReportCompletions.push_back(acceptedCompletion);
                    }
                }
            } {
                const auto interactionLock = InteractionMonitor::GetInstance()->getInteractionLock();
                const unique_lock editedCompletionMapLock(_editedCompletionMapMutex);
                for (const auto& needReportCompletion: needReportCompletions) {
                    _editedCompletionMap.erase(needReportCompletion.actionId);
                    WebsocketManager::GetInstance()->send(needReportCompletion.parse());
                }
            }
            this_thread::sleep_for(1s);
        }
    }).detach();
}

void CompletionManager::_threadDebounceRetrieveCompletion() {
    thread([this] {
        while (_isRunning) {
            if (const auto pastTime = chrono::high_resolution_clock::now() - _debounceRetrieveCompletionTime.load();
                pastTime >= 150ms && _needRetrieveCompletion.load()) {
                WindowManager::GetInstance()->setMenuText("Generating...");
                try {
                    // TODO: Improve performance
                    logger::debug("[_threadDebounceRetrieveCompletion] Try to get interaction unique lock");
                    const auto interactionLock = InteractionMonitor::GetInstance()->getInteractionLock();
                    logger::debug("[_threadDebounceRetrieveCompletion] Successfuly got interaction unique lock");
                    const auto memoryManipulator = MemoryManipulator::GetInstance();
                    const auto currentFileHandle = memoryManipulator->getHandle(MemoryAddress::HandleType::File);
                    const auto caretPosition = memoryManipulator->getCaretPosition();
                    if (auto path = memoryManipulator->getCurrentFilePath();
                        currentFileHandle && !path.empty()) {
                        SymbolManager::GetInstance()->updateRootPath(path);
                        string prefix, prefixForSymbol, suffix; {
                            const auto currentLine = memoryManipulator->getLineContent(
                                currentFileHandle, caretPosition.line
                            );
                            prefix = iconv::autoDecode(currentLine.substr(0, caretPosition.character));
                            suffix = iconv::autoDecode(currentLine.substr(caretPosition.character));
                        }
                        for (uint32_t index = 1; index <= min(caretPosition.line, 100u); ++index) {
                            const auto tempLine = iconv::autoDecode(memoryManipulator->getLineContent(
                                currentFileHandle, caretPosition.line - index
                            )).append("\n");
                            prefix.insert(0, tempLine);
                            if (regex_search(tempLine, regex(R"~(^\/\/.*|^\/\*\*.*)~"))) {
                                prefixForSymbol = prefix;
                            }
                        }
                        for (uint32_t index = 1; index <= 50u; ++index) {
                            const auto tempLine = iconv::autoDecode(
                                memoryManipulator->getLineContent(currentFileHandle, caretPosition.line + index)
                            );
                            suffix.append("\n").append(tempLine);
                        } {
                            unique_lock lock(_componentsMutex);
                            _components.caretPosition = caretPosition;
                            _components.path = move(path);
                            _components.prefix = move(prefix);
                            _components.recentFiles = ModificationManager::GetInstance()->getRecentFiles();
                            _components.symbols = SymbolManager::GetInstance()->getSymbols(prefixForSymbol);
                            _components.suffix = move(suffix);
                        }
                        _isNewLine = false;
                        logger::info("Retrieve completion with full prefix");
                        // TODO: Improve performance
                        //     unique_lock lock(_componentsMutex);
                        //     _components.caretPosition = caretPosition;
                        //     _components.path = InteractionMonitor::GetInstance()->getFileName();
                        //     if (const auto lastNewLineIndex = _components.prefix.find_last_of('\r');
                        //         lastNewLineIndex != string::npos) {
                        //         _components.prefix = _components.prefix.substr(0, lastNewLineIndex).append(prefix);
                        //     } else {
                        //         _components.prefix = prefix;
                        //     }
                        //     if (const auto firstNewLineIndex = suffix.find_first_of('\r');
                        //         firstNewLineIndex != string::npos) {
                        //         _components.suffix = _components.suffix.substr(firstNewLineIndex + 1).insert(0, suffix);
                        //     } else {
                        //         _components.suffix = suffix;
                        //     }
                        //     logger::info("Retrieve completion with current line prefix");
                        _sendCompletionGenerate();
                    }
                } catch (const exception& e) {
                    logger::warn(format("Exception when retrieving completion: {}", e.what()));
                }
                _needRetrieveCompletion.store(false);
            }
            this_thread::sleep_for(10ms);
        }
    }).detach();
}
