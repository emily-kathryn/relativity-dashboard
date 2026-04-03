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

Vector3 LerpPoint(Vector3 start, Vector3 end, float amount) {
  return {
      start.x + ((end.x - start.x) * amount),
      start.y + ((end.y - start.y) * amount),
      start.z + ((end.z - start.z) * amount),
  };
}

Vector2 Normalize(Vector2 value) {
  const float length = std::sqrt((value.x * value.x) + (value.y * value.y));
  if (length <= 0.0001F) {
    return {0.0F, 0.0F};
  }
  return {value.x / length, value.y / length};
}

Vector3 Normalize(Vector3 value) {
  const float length = std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
  if (length <= 0.0001F) {
    return {0.0F, 0.0F, 0.0F};
  }
  return {value.x / length, value.y / length, value.z / length};
}

Vector3 Add(Vector3 left, Vector3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vector3 Subtract(Vector3 left, Vector3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector3 Scale(Vector3 value, float scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

Vector3 QuadraticBezierPoint(Vector3 start, Vector3 control, Vector3 end, float amount) {
  const Vector3 a = LerpPoint(start, control, amount);
  const Vector3 b = LerpPoint(control, end, amount);
  return LerpPoint(a, b, amount);
}

float TubeRadius(double gamma, double maximum_gamma) {
  const double gamma_span = std::max(0.0001, maximum_gamma - 1.0);
  const float gamma_fraction = Clamp01(static_cast<float>((gamma - 1.0) / gamma_span));
  const float widening = std::pow(1.0F - gamma_fraction, 2.35F);
  return 0.16F + (1.28F * widening);
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

void DrawPanel(const Rectangle& bounds, Color fill, Color outline) {
  DrawRectangleRounded(bounds, 0.12F, 10, fill);
  DrawRectangleRoundedLinesEx(bounds, 0.12F, 10, 1.0F, outline);
}

const char* DynamicsLabel(double signed_proper_acceleration) {
  return signed_proper_acceleration < 0.0 ? "proper deceleration" : "proper acceleration";
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
  const Rectangle state_panel {74.0F, 126.0F, 252.0F, 170.0F};
  const Rectangle time_panel {1032.0F, 126.0F, 150.0F, 310.0F};

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
  RenderTexture2D scene_target = LoadRenderTexture(static_cast<int>(viewport.width), static_cast<int>(viewport.height));
  SetTextureFilter(scene_target.texture, TEXTURE_FILTER_BILINEAR);

  Camera3D camera {};
  camera.position = {0.0F, 6.6F, 18.0F};
  camera.target = {0.0F, 0.8F, 0.0F};
  camera.up = {0.0F, 1.0F, 0.0F};
  camera.fovy = 30.0F;
  camera.projection = CAMERA_PERSPECTIVE;

  const Vector3 earth_world {-10.5F, 2.2F, -4.8F};
  const Vector3 turnaround_world {0.0F, 0.6F, 0.0F};
  const Vector3 destination_world {10.5F, -1.9F, 4.8F};
  const Vector3 world_up {0.0F, 1.0F, 0.0F};

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

    const float travel_fraction = static_cast<float>(current.position_ly / mission.profile.distance_ly);
    const bool decelerating_phase = current.signed_proper_acceleration_ly_per_year2 < 0.0;
    const Vector3 ship_world = QuadraticBezierPoint(earth_world, turnaround_world, destination_world, travel_fraction);
    const float probe_offset_fraction = 0.003F;
    const float previous_fraction = std::max(0.0F, travel_fraction - probe_offset_fraction);
    const float next_fraction = std::min(1.0F, travel_fraction + probe_offset_fraction);
    const Vector3 probe_previous = QuadraticBezierPoint(earth_world, turnaround_world, destination_world, previous_fraction);
    const Vector3 probe_next = QuadraticBezierPoint(earth_world, turnaround_world, destination_world, next_fraction);
    const Vector3 tube_direction = Normalize(Subtract(probe_next, probe_previous));
    const Vector3 acceleration_offset_start = Add(ship_world, {0.0F, 0.8F, 0.0F});
    const float acceleration_vector_length = 0.9F + (1.6F * static_cast<float>(current.beta));
    const float acceleration_direction_sign = decelerating_phase ? -1.0F : 1.0F;
    const Vector3 acceleration_vector_end =
        Add(acceleration_offset_start, Scale(tube_direction, acceleration_vector_length * acceleration_direction_sign));
    const Vector2 earth_label = GetWorldToScreenEx(earth_world, camera, static_cast<int>(viewport.width),
                                                   static_cast<int>(viewport.height));
    const Vector2 destination_label = GetWorldToScreenEx(destination_world, camera, static_cast<int>(viewport.width),
                                                         static_cast<int>(viewport.height));
    const Vector2 turnaround_label = GetWorldToScreenEx(turnaround_world, camera, static_cast<int>(viewport.width),
                                                        static_cast<int>(viewport.height));

    BeginDrawing();
    ClearBackground(app_background);

    DrawUiText(ui_font, "Relativistic Proper-Time Simulation", 40.0F, 28.0F, 28.0F, text);
    DrawUiText(ui_font, "A true 3D world-tube: radius contracts as Lorentz factor increases.", 42.0F, 60.0F, 18.0F,
               muted);

    BeginTextureMode(scene_target);
    ClearBackground(viewport_background);
    BeginMode3D(camera);

    DrawGrid(24, 1.5F);
    DrawLine3D(earth_world, destination_world, Fade(structure, 0.25F));
    DrawSphereEx(earth_world, 0.42F, 18, 18, Fade(structure, 0.95F));
    DrawSphereEx(destination_world, 0.34F, 16, 16, Fade(structure, 0.85F));

    const std::size_t stride = std::max<std::size_t>(1, mission.samples.size() / 180);
    bool midpoint_world_set = false;
    Vector3 midpoint_world = turnaround_world;
    float midpoint_radius = TubeRadius(mission.summary.gamma, mission.summary.gamma);

    for (std::size_t index = 0; index < current_index; index += stride) {
      const auto& start_sample = mission.samples[index];
      const auto& end_sample = mission.samples[std::min(index + stride, current_index)];
      const float start_fraction = static_cast<float>(start_sample.position_ly / mission.profile.distance_ly);
      const float end_fraction = static_cast<float>(end_sample.position_ly / mission.profile.distance_ly);
      const Vector3 start_point = QuadraticBezierPoint(earth_world, turnaround_world, destination_world, start_fraction);
      const Vector3 end_point = QuadraticBezierPoint(earth_world, turnaround_world, destination_world, end_fraction);

      const float start_radius = TubeRadius(start_sample.gamma, mission.summary.gamma);
      const float end_radius = TubeRadius(end_sample.gamma, mission.summary.gamma);
      const float completion = current_index > 0 ? static_cast<float>(index) / static_cast<float>(current_index) : 1.0F;
      const Color fill_color = Fade(accent, 0.10F + (0.12F * completion));
      const Color wire_color = Fade(accent, 0.25F + (0.30F * completion));

      DrawCylinderEx(start_point, end_point, start_radius, end_radius, 18, fill_color);
      DrawCylinderWiresEx(start_point, end_point, start_radius, end_radius, 18, wire_color);

      if (!midpoint_world_set && end_fraction >= 0.5F) {
        midpoint_world = end_point;
        midpoint_radius = end_radius;
        midpoint_world_set = true;
      }
    }

    const Vector3 probe_tail = Add(ship_world, Scale(tube_direction, -0.65F));
    const Vector3 probe_nose = Add(ship_world, Scale(tube_direction, 0.65F));
    DrawCylinderEx(probe_tail, probe_nose, 0.16F, 0.08F, 12, Fade(text, 0.95F));
    DrawSphereEx(ship_world, 0.15F, 10, 10, Fade(app_background, 0.90F));
    DrawCylinderEx(acceleration_offset_start, acceleration_vector_end, 0.03F, 0.03F, 10, accent);
    DrawCylinderEx(Add(acceleration_vector_end, Scale(tube_direction, -0.18F)), acceleration_vector_end, 0.10F, 0.0F, 10,
                   accent);

    EndMode3D();
    EndTextureMode();

    DrawPanel(viewport, viewport_background, panel_edge);
    DrawTexturePro(scene_target.texture, {0.0F, 0.0F, static_cast<float>(scene_target.texture.width),
                                          -static_cast<float>(scene_target.texture.height)},
                   viewport, {0.0F, 0.0F}, 0.0F, WHITE);
    DrawRectangleRoundedLinesEx(viewport, 0.015F, 10, 1.0F, panel_edge);

    DrawPanel(state_panel, Color {19, 23, 29, 215}, Fade(panel_edge, 0.65F));
    DrawPanel(time_panel, Color {19, 23, 29, 215}, Fade(panel_edge, 0.65F));

    DrawUiText(ui_font, "Earth", viewport.x + earth_label.x - 18.0F, viewport.y + earth_label.y - 40.0F, 18.0F, structure);
    DrawUiText(ui_font, "Destination", viewport.x + destination_label.x - 48.0F, viewport.y + destination_label.y + 18.0F,
               18.0F, structure);

    if (current.progress >= 0.45) {
      DrawUiText(ui_font, "Turnaround Event", viewport.x + turnaround_label.x + (midpoint_radius * 18.0F),
                 viewport.y + turnaround_label.y - 20.0F, 16.0F, text);
      DrawUiText(ui_font, "maximum Lorentz factor", viewport.x + turnaround_label.x + (midpoint_radius * 18.0F),
                 viewport.y + turnaround_label.y + 2.0F,
                 14.0F, muted);
    }

    const Rectangle earth_rail {time_panel.x + 22.0F, time_panel.y + 72.0F, 30.0F, 188.0F};
    const Rectangle traveler_rail {time_panel.x + 82.0F, time_panel.y + 72.0F, 30.0F, 188.0F};
    const float earth_progress = static_cast<float>(current.coordinate_time_years / mission.summary.coordinate_time_years);
    const float traveler_progress = static_cast<float>(current.proper_time_years / mission.summary.coordinate_time_years);

    DrawUiText(ui_font, "Temporal Accumulation", time_panel.x + 18.0F, time_panel.y + 16.0F, 17.0F, muted);
    DrawUiText(ui_font, "Coordinate", earth_rail.x - 12.0F, earth_rail.y - 28.0F, 14.0F, muted);
    DrawUiText(ui_font, "Proper", traveler_rail.x + 1.0F, traveler_rail.y - 28.0F, 14.0F, muted);

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

    DrawUiText(ui_font, "Kinematic State", state_panel.x + 18.0F, state_panel.y + 16.0F, 17.0F, muted);
    DrawUiText(ui_font, "phase", state_panel.x + 18.0F, state_panel.y + 48.0F, 16.0F, muted);
    DrawUiText(ui_font, std::string(PhaseLabel(current.progress)), state_panel.x + 102.0F, state_panel.y + 48.0F, 16.0F,
               text);
    DrawUiText(ui_font, "beta", state_panel.x + 18.0F, state_panel.y + 78.0F, 16.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.beta, 3), state_panel.x + 102.0F, state_panel.y + 78.0F, 16.0F, text);
    DrawUiText(ui_font, "gamma", state_panel.x + 18.0F, state_panel.y + 108.0F, 16.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.gamma, 4), state_panel.x + 102.0F, state_panel.y + 108.0F, 16.0F, text);
    DrawUiText(ui_font, "dynamics", state_panel.x + 18.0F, state_panel.y + 138.0F, 16.0F, muted);
    DrawUiText(ui_font, std::string(DynamicsLabel(current.signed_proper_acceleration_ly_per_year2)), state_panel.x + 102.0F,
               state_panel.y + 138.0F, 16.0F, text);

    DrawUiText(ui_font, "delta t", time_panel.x + 18.0F, time_panel.y + 274.0F, 15.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.coordinate_time_years - current.proper_time_years, 3) + " y",
               time_panel.x + 76.0F, time_panel.y + 274.0F, 15.0F, accent);

    DrawUiText(ui_font, "The world-tube is generated only by the completed trajectory.", 380.0F,
               viewport.y + viewport.height - 72.0F, 17.0F, muted);
    DrawUiText(ui_font, "Tube contraction tracks the rise in Lorentz factor toward the turnaround event.", 380.0F,
               viewport.y + viewport.height - 42.0F, 17.0F, muted);

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
  UnloadRenderTexture(scene_target);

  CloseWindow();
  return 0;
}
