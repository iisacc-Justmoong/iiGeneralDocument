#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace ii::document {

struct Point {
    double x{0.0};
    double y{0.0};
};

struct Rect {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    [[nodiscard]] bool isFinite() const noexcept
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width)
            && std::isfinite(height);
    }
};

struct Matrix {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
    double e{0.0};
    double f{0.0};

    [[nodiscard]] Point map(Point point) const noexcept
    {
        return Point{a * point.x + c * point.y + e, b * point.x + d * point.y + f};
    }

    [[nodiscard]] Rect mapUnitSquare() const noexcept
    {
        const std::array points{map(Point{}), map(Point{1.0, 0.0}),
                                map(Point{0.0, 1.0}), map(Point{1.0, 1.0})};
        double minimumX = points.front().x;
        double maximumX = points.front().x;
        double minimumY = points.front().y;
        double maximumY = points.front().y;
        for (const auto& point : points) {
            minimumX = std::min(minimumX, point.x);
            maximumX = std::max(maximumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumY = std::max(maximumY, point.y);
        }
        return Rect{minimumX, minimumY, maximumX - minimumX, maximumY - minimumY};
    }
};

} // namespace ii::document
