#pragma once

// Debug overlays as data (CLAUDE.md, "Core never renders"): any module may
// produce points, lines and labels; only the viewer draws them (M6,
// docs/tasks/0016). Standard library only.

#include "opas/math/Vec3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opas::debug {

struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
    friend bool operator==(const Color&, const Color&) = default;
};

struct Point {
    math::Vec3 positionM;
    double radiusM = 0.02;
    Color color;
};

struct Line {
    math::Vec3 fromM;
    math::Vec3 toM;
    Color color;
};

struct Label {
    math::Vec3 positionM;
    std::string text;
    Color color;
};

// One layer the viewer can show or hide by name ("centre of mass",
// "support area", ...).
struct Overlay {
    std::string layer;
    std::vector<Point> points;
    std::vector<Line> lines;
    std::vector<Label> labels;
};

} // namespace opas::debug
