#ifndef ARROW_PATH_H
#define ARROW_PATH_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "NotePath.h"

// Guide-line options and distance-based color gradients for note columns.
namespace ArrowPath {
constexpr int Columns = NotePath::Columns;
enum Property {
  Opacity,
  Width,
  Grain,
  DrawSize,
  DrawSizeBack,
  ColumnOpacity,
  ColumnWidth = ColumnOpacity + Columns,
  Properties = ColumnWidth + Columns
};

inline std::string Name(int property) {
  static const char* names[] = {
      "arrowpath", "arrowpathwidth", "arrowpathgrain", "arrowpathdrawsize",
      "arrowpathdrawsizeback"};
  if (property < ColumnOpacity) {
    return names[property];
  }
  if (property < ColumnWidth) {
    return "arrowpath" + std::to_string(property - ColumnOpacity);
  }
  return "arrowpathwidth" + std::to_string(property - ColumnWidth);
}
inline int FindProperty(const std::string& name) {
  if (name == "arrowpathgirth") {
    return Width;
  }
  for (int i = 0; i < Properties; ++i) {
    if (name == Name(i)) {
      return i;
    }
    if (i >= ColumnWidth &&
        name == "arrowpathgirth" + std::to_string(i - ColumnWidth)) {
      return i;
    }
  }
  return -1;
}
struct Options {
  std::array<float, Properties> values{};
  std::array<float, Properties> speeds;
  Options() { speeds.fill(1); }
  void Update(const Options& target, float elapsed) {
    for (int i = 0; i < Properties; ++i) {
      values[i] = NotePath::Approach(
          values[i], target.values[i], target.speeds[i], elapsed);
    }
  }
  float Alpha(int column) const {
    return values[Opacity] + values[ColumnOpacity + column];
  }
  float Girth(int column) const {
    return (1 + values[Width]) * (1 + values[ColumnWidth + column]);
  }
  float BeatStep() const {
    return (values[Grain] > .05f ? values[Grain] : 4.f) / 48.f;
  }
  bool operator==(const Options& other) const { return values == other.values; }
  bool operator!=(const Options& other) const { return !(*this == other); }
};
struct Color {
  float r = 1, g = 1, b = 1, a = 1;
};
struct Stop {
  float position = 0;
  Color color;
};
struct Display {
  std::array<std::vector<Stop>, Columns> gradients;
  int blend = 0;
  void Reset() { *this = Display(); }
  void SetCount(int column, int count) {
    std::vector<Stop>& stops = gradients[column];
    // A changed size resets EVERY stop, including ones already allocated.
    if (stops.size() != static_cast<std::size_t>(count)) {
      stops.assign(count, Stop());
    }
  }
  Color Evaluate(int column, float distance) const {
    const std::vector<Stop>& stops = gradients[column];
    if (stops.empty()) {
      return Color();
    }
    Color result = stops.front().color;
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
      const Stop& first = stops[i];
      const Stop& next = stops[i + 1];
      const float begin = first.position * 64, end = next.position * 64;
      // Preserve authored order. In particular, an unused trailing zero stop
      // terminates the gradient rather than turning it white again.
      if (end < begin || (begin == 0 && end == 0)) {
        return first.color;
      }
      if (distance <= begin) {
        return result;
      }
      if (end == begin) {
        result = next.color;
        continue;
      }
      const float t = (distance - begin) / (end - begin);
      result = {
          first.color.r + (next.color.r - first.color.r) * t,
          first.color.g + (next.color.g - first.color.g) * t,
          first.color.b + (next.color.b - first.color.b) * t,
          first.color.a + (next.color.a - first.color.a) * t};
      if (distance <= end) {
        return result;
      }
    }
    return result;
  }
};
}  // namespace ArrowPath
#endif
