#pragma once

#include <cstdint>

namespace types {
    class CaretPosition {
    public:
        uint32_t character{};
        uint32_t line{};

        CaretPosition() = default;

        CaretPosition(uint32_t character, uint32_t line);

        CaretPosition& addCharactor(int64_t charactor);

        CaretPosition& addLine(int64_t line);

        bool operator<(const CaretPosition&other) const;

        bool operator<=(const CaretPosition&other) const;

        bool operator==(const CaretPosition&other) const;

        bool operator!=(const CaretPosition&other) const;

        bool operator>(const CaretPosition&other) const;

        bool operator>=(const CaretPosition&other) const;

        CaretPosition& operator+=(const CaretPosition&other);

        CaretPosition& operator-=(const CaretPosition&other);

        friend CaretPosition operator+(const CaretPosition&lhs, const CaretPosition&rhs);

        friend CaretPosition operator-(const CaretPosition&lhs, const CaretPosition&rhs);
    };
}