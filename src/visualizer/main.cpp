#include "relativity/simulation.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct CliOptions {
  double distance_ly {4.37};
  double beta {0.8};
  std::size_t samples {600};
};

double ParseDouble(const std::string_view text, const std::string_view name) {
  const std::string owned(text);
  std::size_t consumed = 0;
  const double value = std::stod(owned, &consumed);
  if (consumed != owned.size()) {
    throw std::invalid_argument("invalid numeric value for " + std::string(name));
  }
  return value;
}

std::size_t ParseSize(const std::string_view text, const std::string_view name) {
  const std::string owned(text);
  std::size_t consumed = 0;
  const auto value = std::stoull(owned, &consumed);
  if (consumed != owned.size()) {
    throw std::invalid_argument("invalid integer value for " + std::string(name));
  }
  return static_cast<std::size_t>(value);
}

CliOptions ParseArgs(int argc, char** argv) {
  CliOptions options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);

    const auto require_value = [&](const std::string_view flag) -> std::string_view {
      if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + std::string(flag));
      }
      ++index;
      return argv[index];
    };

    if (arg == "--distance") {
      options.distance_ly = ParseDouble(require_value(arg), arg);
    } else if (arg == "--beta") {
      options.beta = ParseDouble(require_value(arg), arg);
    } else if (arg == "--samples") {
      options.samples = ParseSize(require_value(arg), arg);
    } else if (arg == "--help") {
      throw std::invalid_argument("help");
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(arg));
    }
  }

  return options;
}

std::string FormatFixed(double value, int precision = 3) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(precision);
  out << value;
  return out.str();
}

Font LoadUiFont(bool& custom_font_loaded) {
  constexpr const char* kFontPath = "/System/Library/Fonts/Supplemental/Arial.ttf";
  if (FileExists(kFontPath)) {
    custom_font_loaded = true;
    Font font = LoadFontEx(kFontPath, 96, nullptr, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
  }

  custom_font_loaded = false;
  return GetFontDefault();
}

void DrawUiText(const Font& font, const char* text, float x, float y, float size, Color color) {
  DrawTextEx(font, text, {x, y}, size, 0.0F, color);
}

void DrawUiText(const Font& font, const std::string& text, float x, float y, float size, Color color) {
  DrawTextEx(font, text.c_str(), {x, y}, size, 0.0F, color);
}

float MeasureUiText(const Font& font, const char* text, float size) {
  return MeasureTextEx(font, text, size, 0.0F).x;
}

float Clamp01(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

Rectangle ExpandedRect(const Rectangle& rectangle, float margin) {
  return {rectangle.x - margin, rectangle.y - margin, rectangle.width + (margin * 2.0F),
          rectangle.height + (margin * 2.0F)};
}

bool ButtonClicked(const Rectangle& bounds, const Vector2 mouse) {
  return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, bounds);
}

void DrawButton(const Font& font, const Rectangle& bounds, const char* label, bool active, bool hover, Color fill,
                Color outline, Color text_color) {
  const Color background = active ? fill : (hover ? Fade(fill, 0.84F) : Fade(fill, 0.58F));
  DrawRectangleRounded(bounds, 0.18F, 10, background);
  DrawRectangleRoundedLinesEx(bounds, 0.18F, 10, 1.0F, outline);

  const int font_size = 18;
  const float text_width = MeasureUiText(font, label, static_cast<float>(font_size));
  DrawUiText(font, label, bounds.x + ((bounds.width - text_width) * 0.5F),
             bounds.y + ((bounds.height - static_cast<float>(font_size)) * 0.5F), static_cast<float>(font_size),
             text_color);
}

float SliderStageFromMouse(const Rectangle& slider_track, const Vector2 mouse_position) {
  return Clamp01((mouse_position.x - slider_track.x) / slider_track.width);
}

Vector2 LerpPoint(Vector2 start, Vector2 end, float amount) {
  return {
      start.x + ((end.x - start.x) * amount),
      start.y + ((end.y - start.y) * amount),
  };
}

Vector2 Normalize(Vector2 value) {
  const float length = std::sqrt((value.x * value.x) + (value.y * value.y));
  if (length <= 0.0001F) {
    return {0.0F, 0.0F};
  }
  return {value.x / length, value.y / length};
}

Vector2 Rotate(Vector2 value, float angle) {
  return {
      (value.x * std::cos(angle)) - (value.y * std::sin(angle)),
      (value.x * std::sin(angle)) + (value.y * std::cos(angle)),
  };
}

void DrawPerspectiveGrid(const Rectangle& bounds, Vector2 vanishing_point, Color color) {
  for (int index = 0; index <= 14; ++index) {
    const float x = bounds.x + ((bounds.width / 14.0F) * static_cast<float>(index));
    DrawLineEx({x, bounds.y + bounds.height}, vanishing_point, 1.0F, color);
  }

  const Vector2 bottom_left {bounds.x, bounds.y + bounds.height};
  const Vector2 bottom_right {bounds.x + bounds.width, bounds.y + bounds.height};
  for (int index = 1; index <= 10; ++index) {
    const float amount = static_cast<float>(index) / 11.0F;
    const float curved = amount * amount;
    const Vector2 left = LerpPoint(bottom_left, vanishing_point, curved);
    const Vector2 right = LerpPoint(bottom_right, vanishing_point, curved);
    DrawLineEx(left, right, 1.0F, color);
  }
}

void DrawArrow(Vector2 start, Vector2 end, Color color, float thickness) {
  DrawLineEx(start, end, thickness, color);

  const Vector2 delta {end.x - start.x, end.y - start.y};
  const float length = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
  if (length <= 0.001F) {
    return;
  }

  const Vector2 direction {delta.x / length, delta.y / length};
  const Vector2 normal {-direction.y, direction.x};
  const Vector2 wing_base {end.x - (direction.x * 12.0F), end.y - (direction.y * 12.0F)};
  const Vector2 wing_a {wing_base.x + (normal.x * 5.5F), wing_base.y + (normal.y * 5.5F)};
  const Vector2 wing_b {wing_base.x - (normal.x * 5.5F), wing_base.y - (normal.y * 5.5F)};
  DrawTriangle(end, wing_a, wing_b, color);
}

Vector2 EllipsePoint(Vector2 center, float rx, float ry, float rotation, float theta) {
  const Vector2 local {rx * std::cos(theta), ry * std::sin(theta)};
  const Vector2 rotated = Rotate(local, rotation);
  return {center.x + rotated.x, center.y + rotated.y};
}

void DrawRotatedEllipseLines(Vector2 center, float rx, float ry, float rotation, int segments, float thickness,
                             Color color) {
  Vector2 previous = EllipsePoint(center, rx, ry, rotation, 0.0F);
  for (int index = 1; index <= segments; ++index) {
    const float theta = (static_cast<float>(index) / static_cast<float>(segments)) * 2.0F * PI;
    const Vector2 current = EllipsePoint(center, rx, ry, rotation, theta);
    DrawLineEx(previous, current, thickness, color);
    previous = current;
  }
}

const char* PhaseLabel(double progress) {
  if (progress < 0.04) {
    return "departure";
  }
  if (progress < 0.5) {
    return "accelerating";
  }
  if (progress < 0.96) {
    return "decelerating";
  }
  return "arrival";
}

}  // namespace

int main(int argc, char** argv) {
  const int screen_width = 1280;
  const int screen_height = 820;

  CliOptions options;
  try {
    options = ParseArgs(argc, argv);
  } catch (const std::exception& error) {
    if (std::string_view(error.what()) == "help") {
      TraceLog(LOG_INFO, "Usage: %s [--distance <light-years>] [--beta <fraction-of-c>] [--samples <count>]", argv[0]);
      return 0;
    }
    TraceLog(LOG_ERROR, "%s", error.what());
    return 1;
  }

  const relativity::MissionResult mission = relativity::SimulateMission(relativity::MissionProfile {
      .distance_ly = options.distance_ly,
      .beta = options.beta,
      .sample_count = options.samples,
  });

  InitWindow(screen_width, screen_height, "Relativity Mission Control");
  SetTargetFPS(60);

  const Rectangle viewport {40.0F, 96.0F, 1200.0F, 600.0F};
  const Rectangle controls {40.0F, 720.0F, 1200.0F, 64.0F};
  const Rectangle play_button {60.0F, 736.0F, 92.0F, 32.0F};
  const Rectangle reset_button {164.0F, 736.0F, 92.0F, 32.0F};
  const Rectangle slider_track {304.0F, 751.0F, 700.0F, 8.0F};

  const Color app_background {10, 12, 16, 255};
  const Color viewport_background {15, 17, 22, 255};
  const Color panel_edge {54, 61, 73, 255};
  const Color grid {34, 39, 48, 255};
  const Color text {228, 233, 241, 255};
  const Color muted {132, 141, 154, 255};
  const Color structure {174, 181, 190, 255};
  const Color accent {205, 214, 226, 255};
  const Color accent_soft {205, 214, 226, 90};

  bool custom_font_loaded = false;
  const Font ui_font = LoadUiFont(custom_font_loaded);

  bool playing = false;
  bool dragging_slider = false;
  float stage = 0.0F;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();
    const Vector2 mouse = GetMousePosition();

    if (ButtonClicked(play_button, mouse) || IsKeyPressed(KEY_SPACE)) {
      if (stage >= 1.0F) {
        stage = 0.0F;
      }
      playing = !playing;
    }
    if (ButtonClicked(reset_button, mouse) || IsKeyPressed(KEY_HOME)) {
      stage = 0.0F;
      playing = false;
    }
    if (IsKeyPressed(KEY_END)) {
      stage = 1.0F;
      playing = false;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, ExpandedRect(slider_track, 12.0F))) {
      dragging_slider = true;
      playing = false;
    }

    if (dragging_slider) {
      stage = SliderStageFromMouse(slider_track, mouse);
      if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        dragging_slider = false;
      }
    }

    if (!dragging_slider) {
      if (IsKeyDown(KEY_RIGHT)) {
        stage = Clamp01(stage + (dt * 0.14F));
        playing = false;
      }
      if (IsKeyDown(KEY_LEFT)) {
        stage = Clamp01(stage - (dt * 0.14F));
        playing = false;
      }
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0F) {
      stage = Clamp01(stage + (wheel * 0.009F));
      playing = false;
    }

    if (playing) {
      stage = Clamp01(stage + (dt * 0.02F));
      if (stage >= 1.0F) {
        playing = false;
      }
    }

    const std::size_t current_index = std::min(
        mission.samples.size() - 1,
        static_cast<std::size_t>(std::round(stage * static_cast<float>(mission.samples.size() - 1))));
    const auto& current = mission.samples[current_index];

    const Vector2 earth {viewport.x + 190.0F, viewport.y + 170.0F};
    const Vector2 destination {viewport.x + viewport.width - 220.0F, viewport.y + viewport.height - 170.0F};
    const Vector2 axis = Normalize({destination.x - earth.x, destination.y - earth.y});
    const Vector2 normal {-axis.y, axis.x};
    const float axis_angle = std::atan2(axis.y, axis.x);

    const float travel_fraction = static_cast<float>(current.position_ly / mission.profile.distance_ly);
    const Vector2 ship = LerpPoint(earth, destination, travel_fraction);
    const bool braking = current.signed_proper_acceleration_ly_per_year2 < 0.0;

    BeginDrawing();
    ClearBackground(app_background);

    DrawUiText(ui_font, "Relativistic Proper-Time Simulation", 40.0F, 28.0F, 28.0F, text);
    DrawUiText(ui_font,
               "Narrower tube sections correspond to higher Lorentz factor and reduced proper-time accumulation.",
               42.0F, 60.0F, 18.0F, muted);

    DrawRectangleRounded(viewport, 0.015F, 10, viewport_background);
    DrawRectangleRoundedLinesEx(viewport, 0.015F, 10, 1.0F, panel_edge);
    DrawPerspectiveGrid(viewport, {viewport.x + viewport.width - 180.0F, viewport.y + 120.0F}, grid);

    DrawUiText(ui_font, "Earth", earth.x - 20.0F, earth.y - 72.0F, 20.0F, structure);
    DrawUiText(ui_font, "Destination", destination.x - 54.0F, destination.y + 34.0F, 20.0F, structure);

    DrawCircleV(earth, 10.0F, structure);
    DrawCircleV(destination, 8.0F, Fade(structure, 0.88F));
    DrawLineEx(earth, destination, 1.0F, Fade(structure, 0.18F));

    const std::size_t stride = std::max<std::size_t>(1, mission.samples.size() / 56);
    Vector2 previous_upper {};
    Vector2 previous_lower {};
    bool has_previous_edges = false;
    Vector2 previous_upper_completed {};
    Vector2 previous_lower_completed {};
    bool has_previous_completed_edges = false;
    Vector2 midpoint_center {};
    float midpoint_radius_major = 0.0F;
    float midpoint_radius_minor = 0.0F;
    bool midpoint_set = false;

    for (std::size_t index = 0; index < mission.samples.size(); index += stride) {
      const auto& sample = mission.samples[index];
      const float sample_t = static_cast<float>(sample.position_ly / mission.profile.distance_ly);
      const Vector2 center = LerpPoint(earth, destination, sample_t);

      const float perspective = 1.14F - (sample_t * 0.34F);
      const float gamma_fraction = static_cast<float>((sample.gamma - 1.0) / (mission.summary.gamma - 1.0));
      const float widening = std::pow(1.0F - gamma_fraction, 2.15F);
      const float radius_major = (16.0F + (66.0F * widening)) * perspective;
      const float radius_minor = (6.0F + (24.0F * widening)) * perspective;

      const Color ring_color = Fade(structure, 0.10F + (0.22F * (1.0F - std::abs((sample_t - 0.5F) * 1.3F))));
      DrawRotatedEllipseLines(center, radius_minor, radius_major, axis_angle + (PI * 0.5F), 40, 1.8F, ring_color);

      const Vector2 upper {center.x + (normal.x * radius_major), center.y + (normal.y * radius_major)};
      const Vector2 lower {center.x - (normal.x * radius_major), center.y - (normal.y * radius_major)};

      if (has_previous_edges) {
        DrawLineEx(previous_upper, upper, 1.4F, Fade(structure, 0.22F));
        DrawLineEx(previous_lower, lower, 1.4F, Fade(structure, 0.22F));
      }

      previous_upper = upper;
      previous_lower = lower;
      has_previous_edges = true;

      if (index <= current_index) {
        if (has_previous_completed_edges) {
          DrawLineEx(previous_upper_completed, upper, 2.2F, Fade(accent, 0.44F));
          DrawLineEx(previous_lower_completed, lower, 2.2F, Fade(accent, 0.44F));
        }

        previous_upper_completed = upper;
        previous_lower_completed = lower;
        has_previous_completed_edges = true;
        DrawRotatedEllipseLines(center, radius_minor, radius_major, axis_angle + (PI * 0.5F), 40, 2.2F,
                                Fade(accent, 0.22F));
      }

      if (!midpoint_set && sample_t >= 0.5F) {
        midpoint_center = center;
        midpoint_radius_major = radius_major;
        midpoint_radius_minor = radius_minor;
        midpoint_set = true;
      }
    }

    if (!midpoint_set) {
      midpoint_center = LerpPoint(earth, destination, 0.5F);
      midpoint_radius_major = 16.0F;
      midpoint_radius_minor = 6.0F;
    }

    DrawRotatedEllipseLines(earth, 11.0F, 84.0F, axis_angle + (PI * 0.5F), 44, 2.8F, Fade(structure, 0.82F));
    DrawRotatedEllipseLines(destination, 10.0F, 84.0F, axis_angle + (PI * 0.5F), 44, 2.8F, Fade(structure, 0.74F));
    DrawRotatedEllipseLines(midpoint_center, midpoint_radius_minor, midpoint_radius_major, axis_angle + (PI * 0.5F), 44,
                            2.8F, accent);

    const Vector2 turnaround_label_anchor {midpoint_center.x + (normal.x * (midpoint_radius_major + 34.0F)),
                                           midpoint_center.y + (normal.y * (midpoint_radius_major + 34.0F))};
    DrawLineEx(turnaround_label_anchor, {turnaround_label_anchor.x + 60.0F, turnaround_label_anchor.y - 20.0F}, 1.6F,
               Fade(accent, 0.75F));
    DrawUiText(ui_font, "Turnaround Event", turnaround_label_anchor.x + 66.0F, turnaround_label_anchor.y - 34.0F, 18.0F,
               text);
    DrawUiText(ui_font, "maximum Lorentz factor", turnaround_label_anchor.x + 66.0F, turnaround_label_anchor.y - 12.0F,
               16.0F, muted);

    DrawRotatedEllipseLines(ship, 5.5F, 18.0F, axis_angle + (PI * 0.5F), 32, 2.2F, text);
    DrawLineEx({ship.x - (axis.x * 26.0F), ship.y - (axis.y * 26.0F)},
               {ship.x + (axis.x * 26.0F), ship.y + (axis.y * 26.0F)}, 5.0F, accent);
    DrawCircleV(ship, 4.0F, app_background);

    const float accel_length = 30.0F + (58.0F * static_cast<float>(current.beta));
    const float accel_sign = braking ? -1.0F : 1.0F;
    DrawArrow({ship.x, ship.y - 46.0F}, {ship.x + (axis.x * accel_length * accel_sign), ship.y - 46.0F + (axis.y * accel_length * accel_sign)},
              accent, 2.0F);

    const Rectangle earth_rail {viewport.x + viewport.width - 190.0F, viewport.y + 128.0F, 32.0F, 280.0F};
    const Rectangle traveler_rail {viewport.x + viewport.width - 126.0F, viewport.y + 128.0F, 32.0F, 280.0F};
    const float earth_progress = static_cast<float>(current.coordinate_time_years / mission.summary.coordinate_time_years);
    const float traveler_progress = static_cast<float>(current.proper_time_years / mission.summary.coordinate_time_years);

    DrawUiText(ui_font, "Coordinate Time", earth_rail.x - 18.0F, earth_rail.y - 30.0F, 18.0F, muted);
    DrawUiText(ui_font, "Proper Time", traveler_rail.x - 8.0F, traveler_rail.y - 30.0F, 18.0F, muted);

    DrawRectangleRounded(earth_rail, 0.2F, 8, Fade(structure, 0.06F));
    DrawRectangleRoundedLinesEx(earth_rail, 0.2F, 8, 1.0F, Fade(structure, 0.40F));
    DrawRectangleRounded({earth_rail.x, earth_rail.y + earth_rail.height - (earth_rail.height * earth_progress),
                          earth_rail.width, earth_rail.height * earth_progress},
                         0.2F, 8, Fade(structure, 0.22F));

    DrawRectangleRounded(traveler_rail, 0.2F, 8, Fade(accent, 0.06F));
    DrawRectangleRoundedLinesEx(traveler_rail, 0.2F, 8, 1.0F, Fade(accent, 0.46F));
    DrawRectangleRounded({traveler_rail.x, traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_progress),
                          traveler_rail.width, traveler_rail.height * traveler_progress},
                         0.2F, 8, Fade(accent, 0.26F));

    const float earth_marker_y = earth_rail.y + earth_rail.height - (earth_rail.height * earth_progress);
    const float traveler_marker_y = traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_progress);
    DrawLineEx({earth_rail.x - 10.0F, earth_marker_y}, {earth_rail.x + earth_rail.width + 10.0F, earth_marker_y}, 2.0F,
               structure);
    DrawLineEx({traveler_rail.x - 10.0F, traveler_marker_y}, {traveler_rail.x + traveler_rail.width + 10.0F, traveler_marker_y},
               2.0F, accent);

    DrawUiText(ui_font, FormatFixed(current.coordinate_time_years, 3), earth_rail.x - 4.0F,
               earth_rail.y + earth_rail.height + 12.0F, 18.0F, text);
    DrawUiText(ui_font, FormatFixed(current.proper_time_years, 3), traveler_rail.x - 4.0F,
               traveler_rail.y + traveler_rail.height + 12.0F, 18.0F, text);

    DrawUiText(ui_font, "kinematic state", viewport.x + 54.0F, viewport.y + viewport.height - 162.0F, 18.0F, muted);
    DrawUiText(ui_font, "phase", viewport.x + 54.0F, viewport.y + viewport.height - 132.0F, 18.0F, muted);
    DrawUiText(ui_font, std::string(PhaseLabel(current.progress)), viewport.x + 132.0F,
               viewport.y + viewport.height - 132.0F, 18.0F, text);
    DrawUiText(ui_font, "beta", viewport.x + 54.0F, viewport.y + viewport.height - 102.0F, 18.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.beta, 3), viewport.x + 132.0F, viewport.y + viewport.height - 102.0F, 18.0F,
               text);
    DrawUiText(ui_font, "gamma", viewport.x + 54.0F, viewport.y + viewport.height - 72.0F, 18.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.gamma, 4), viewport.x + 132.0F, viewport.y + viewport.height - 72.0F, 18.0F,
               text);
    DrawUiText(ui_font, "delta t", viewport.x + 54.0F, viewport.y + viewport.height - 42.0F, 18.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.coordinate_time_years - current.proper_time_years, 3) + " y",
               viewport.x + 132.0F, viewport.y + viewport.height - 42.0F, 18.0F, accent);

    DrawUiText(ui_font, "Larger tube diameter corresponds to lower velocity and faster proper-time accumulation.", 380.0F,
               viewport.y + viewport.height - 76.0F, 18.0F, muted);
    DrawUiText(ui_font,
               "The tube contracts continuously toward the turnaround event, where the Lorentz factor is maximal.", 380.0F,
               viewport.y + viewport.height - 44.0F, 18.0F, muted);

    DrawRectangleRounded(controls, 0.04F, 10, viewport_background);
    DrawRectangleRoundedLinesEx(controls, 0.04F, 10, 1.0F, panel_edge);

    DrawButton(ui_font, play_button, playing ? "Pause" : "Play", playing, CheckCollisionPointRec(mouse, play_button),
               accent, accent, app_background);
    DrawButton(ui_font, reset_button, "Reset", false, CheckCollisionPointRec(mouse, reset_button),
               Color {88, 96, 108, 255}, panel_edge, text);

    DrawRectangleRounded(slider_track, 1.0F, 16, Color {44, 49, 57, 255});
    DrawRectangleRounded({slider_track.x, slider_track.y, slider_track.width * stage, slider_track.height}, 1.0F, 16,
                         accent);
    const Vector2 knob_center {slider_track.x + (slider_track.width * stage), slider_track.y + (slider_track.height * 0.5F)};
    DrawCircleV(knob_center, 8.0F, text);
    DrawCircleV(knob_center, 3.0F, app_background);

    DrawUiText(ui_font, "Playback", 304.0F, 730.0F, 17.0F, muted);
    DrawUiText(ui_font, "Space play/pause    Left/Right scrub    mouse wheel fine step    End jump to arrival", 1032.0F,
               742.0F, 14.0F, muted);

    EndDrawing();
  }

  if (custom_font_loaded) {
    UnloadFont(ui_font);
  }

  CloseWindow();
  return 0;
}
