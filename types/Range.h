//
// Created by ckf9674 on 2023/12/15.
//
#pragma once

#include <any>
#include <types/CaretPosition.h>

namespace types {
    class Range {
    public:
        CaretPosition start, end;

        Range() = default;

        Range(CaretPosition start, CaretPosition end);

        Range(uint32_t startLine, uint32_t startCharacter, uint32_t endLine, uint32_t endCharacter);

        [[nodiscard]] bool isEmpty() const;

        [[nodiscard]] bool isSingleLine() const;

        [[nodiscard]] bool contains(const Range& other) const;

        [[nodiscard]] bool isEqual(const Range& other) const;

        [[nodiscard]] bool isBefore(const Range& other) const;
        // 返回新的范围 start 和 end都有时返回新的范围， 如果 只有一个则在原有的范围上更改
        [[nodiscard]] Range with(const std::any& start = {}, const std::any& end = {}) const;
        // 并集
        [[nodiscard]] Range Union(const Range& other) const;
        // 交集
        [[nodiscard]] Range intersection(const Range& other) const;

    };
}


#include <format>
#include <string>
namespace std {
    template <>
    struct std::formatter<types::Range> : std::formatter<std::string> {
        template <class FormatContext>
        auto format(const types::Range& Range, FormatContext& context) {
            std::string base_str = std::format("Start: {}, End: {}", Range.start, Range.end);
            return formatter<std::string>::format(base_str, context);
        }
    };
}

