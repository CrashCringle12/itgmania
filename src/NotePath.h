#ifndef NOTE_PATH_H
#define NOTE_PATH_H

#include <algorithm>
#include <array>
#include <cmath>

// Distance-based scalar curves, independent of the beat-based NCSplineHandler.
// Supports linear, cubic ease-in/out, and uniform Catmull-Rom interpolation.
// Boundary neighbors are clamped explicitly; no reads outside a curve are used.
namespace NotePath {
constexpr int Columns = 16;
constexpr int Points = 40;
constexpr int Axes = 3;
constexpr float Unit = 64.0f;
constexpr float Immediate = 100000.0f;
enum Axis { X, Y, Z };

inline float Approach(float current, float target, float speed, float elapsed) {
  if (speed < 0 || speed >= 999) {
    return target;
  }
  const float step = std::max(0.0f, elapsed) * speed;
  return current < target ? std::min(current + step, target)
                          : std::max(current - step, target);
}

struct Curve {
  std::array<float, Points> values{};
  std::array<float, Points> offsets{};
  std::array<float, Points> valueSpeeds;
  std::array<float, Points> offsetSpeeds;
  bool active = false;

  Curve() {
    valueSpeeds.fill(1);
    offsetSpeeds.fill(1);
  }

  bool Set(int point, float value, float offset, float speed) {
    if (point < 0 || point >= Points || !std::isfinite(value) ||
        !std::isfinite(offset) || !std::isfinite(speed)) {
      return false;
    }
    values[point] = value / 100.0f;
    offsets[point] = offset / 100.0f;
    valueSpeeds[point] = speed == -1 ? Immediate : speed;
    offsetSpeeds[point] = Immediate;
    active = true;
    return true;
  }

  void Update(const Curve& target, float elapsed) {
    if (!active && !target.active) {
      return;
    }
    if (!target.active) {
      *this = target;
      return;
    }
    active = true;
    for (int i = 0; i < Points; ++i) {
      values[i] =
          Approach(values[i], target.values[i], target.valueSpeeds[i], elapsed);
      offsets[i] = Approach(
          offsets[i], target.offsets[i], target.offsetSpeeds[i], elapsed);
    }
  }

  float Evaluate(float distance, float type) const {
    if (!active || !std::isfinite(distance)) {
      return 0;
    }
    // A descending offset or a wholly zero pair terminates the authored path.
    int count = 1;
    while (count < Points) {
      if (offsets[count] < offsets[count - 1] ||
          (offsets[count] == 0 && offsets[count - 1] == 0 &&
           values[count] == 0 && values[count - 1] == 0)) {
        break;
      }
      ++count;
    }
    if (count == 1 || distance <= offsets[0] * Unit) {
      return values[0] * Unit;
    }
    if (distance >= offsets[count - 1] * Unit) {
      return values[count - 1] * Unit;
    }
    int i = 0;
    while (i + 1 < count && distance >= offsets[i + 1] * Unit) {
      ++i;
    }
    const float span = (offsets[i + 1] - offsets[i]) * Unit;
    const float t =
        span > 0
            ? std::min(
                  1.0f, std::max(0.0f, (distance - offsets[i] * Unit) / span))
            : 1;
    const float p1 = values[i] * Unit, p2 = values[i + 1] * Unit;
    if (type <= 0) {
      return p1 + (p2 - p1) * t;
    }
    if (type <= 1) {
      const float u = t * 2;
      const float ease =
          u < 1 ? 0.5f * u * u * u : 0.5f * ((u - 2) * (u - 2) * (u - 2) + 2);
      return p1 + (p2 - p1) * ease;
    }
    const float p0 = values[std::max(0, i - 1)] * Unit;
    const float p3 = values[std::min(count - 1, i + 2)] * Unit;
    return p1 + 0.5f * (p2 - p0) * t +
           (p0 - 2.5f * p1 + 2 * p2 - 0.5f * p3) * t * t +
           (-0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3) * t * t * t;
  }

  bool operator==(const Curve& other) const {
    return active == other.active && values == other.values &&
           offsets == other.offsets;
  }
};

struct Paths {
  std::array<std::array<Curve, Columns>, Axes> curves;
  std::array<float, Axes> types{};
  std::array<float, Axes> typeSpeeds{{1, 1, 1}};
  bool preserve = false;

  void Init() {
    if (!preserve) {
      for (std::array<Curve, Columns>& axis : curves) {
        for (Curve& curve : axis) {
          curve = Curve();
        }
      }
    }
    types.fill(0);
    typeSpeeds.fill(1);
  }

  bool Set(
      int axis, int point, int column, float value, float offset, float speed,
      int activeColumns = Columns) {
    if (axis < 0 || axis >= Axes || column < -1 || column >= Columns ||
        point < 0 || point >= Points || !std::isfinite(value) ||
        !std::isfinite(offset) || !std::isfinite(speed)) {
      return false;
    }
    if (column == -1) {
      for (int col = 0; col < std::min(Columns, activeColumns); ++col) {
        curves[axis][col].Set(point, value, offset, speed);
      }
    } else {
      curves[axis][column].Set(point, value, offset, speed);
    }
    return true;
  }

  void Reset(int axis, int column = -1) {
    if (axis < 0 || axis >= Axes || column < -1 || column >= Columns) {
      return;
    }
    const int first = column == -1 ? 0 : column;
    const int last = column == -1 ? Columns : column + 1;
    for (int col = first; col < last; ++col) {
      curves[axis][col] = Curve();
    }
  }

  void Update(const Paths& target, float elapsed) {
    preserve = target.preserve;
    for (int axis = 0; axis < Axes; ++axis) {
      types[axis] = Approach(
          types[axis], target.types[axis], target.typeSpeeds[axis], elapsed);
      for (int col = 0; col < Columns; ++col) {
        curves[axis][col].Update(target.curves[axis][col], elapsed);
      }
    }
  }

  float Evaluate(int axis, int column, float distance) const {
    return column < 0 || column >= Columns || axis < 0 || axis >= Axes
               ? 0
               : curves[axis][column].Evaluate(distance, types[axis]);
  }

  bool Active(int column) const {
    if (column < 0 || column >= Columns) {
      return false;
    }
    for (const std::array<Curve, Columns>& axis : curves) {
      if (axis[column].active) {
        return true;
      }
    }
    return false;
  }

  bool operator!=(const Paths& other) const {
    return curves != other.curves || types != other.types ||
           preserve != other.preserve;
  }
};
}  // namespace NotePath
#endif
