#include "relativity/simulation.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kMaximumLights = 4;

struct ShaderLight {
  int type {};
  bool enabled {};
  Vector3 position {};
  Vector3 target {};
  Color color {};
  int enabled_location {-1};
  int type_location {-1};
  int position_location {-1};
  int target_location {-1};
  int color_location {-1};
};

constexpr const char* kLightingVertexShader = R"glsl(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)glsl";

constexpr const char* kLightingFragmentShader = R"glsl(
#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
#define MAX_LIGHTS 4
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};
uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;
void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 lightDot = vec3(0.0);
    vec3 specular = vec3(0.0);
    vec4 tint = colDiffuse * fragColor;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1)
        {
            vec3 light = vec3(0.0);
            if (lights[i].type == LIGHT_DIRECTIONAL) light = -normalize(lights[i].target - lights[i].position);
            if (lights[i].type == LIGHT_POINT) light = normalize(lights[i].position - fragPosition);
            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL;
            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 24.0);
            specular += specCo;
        }
    }
    finalColor = texelColor*((tint + vec4(specular, 1.0))*vec4(lightDot, 1.0));
    finalColor += texelColor*(ambient/10.0)*tint;
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
)glsl";

struct CliOptions {
  double distance_ly {4.37};
  double beta {0.8};
  std::size_t samples {600};
  std::string destination_id {};
  std::string export_prefix {};
  bool distance_explicit {false};
  bool beta_explicit {false};
};

struct ResolvedMissionOptions {
  relativity::MissionProfile profile;
  std::optional<relativity::MissionPreset> preset;
};

struct PathFrame {
  Vector3 center {};
  Vector3 tangent {};
  Vector3 right {};
  Vector3 up {};
  float path_fraction {};
  float proper_time_rate_radius {};
};

enum class TimeDomain {
  Coordinate,
  Proper,
};

struct PulseMarker {
  double time_years {};
  float path_fraction {};
  float proper_time_rate_radius {};
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

std::string UsageText(const char* program_name) {
  std::ostringstream out;
  out << "Usage: " << program_name << " [--destination <preset>] [--distance <light-years>] "
      << "[--beta <fraction-of-c>] [--samples <count>] [--export-prefix <path-prefix>]\n";
  out << "  --export-prefix writes <prefix>_summary.png and <prefix>_visual.png\n";
  out << "Available destination presets:\n";
  for (const auto& preset : relativity::MissionPresets()) {
    out << "  " << preset.id << " (" << preset.display_name << ", " << preset.distance_ly
        << " ly, suggested beta " << preset.suggested_beta << ")\n";
  }
  return out.str();
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
      options.distance_explicit = true;
    } else if (arg == "--beta") {
      options.beta = ParseDouble(require_value(arg), arg);
      options.beta_explicit = true;
    } else if (arg == "--samples") {
      options.samples = ParseSize(require_value(arg), arg);
    } else if (arg == "--destination") {
      options.destination_id = std::string(require_value(arg));
    } else if (arg == "--export-prefix") {
      options.export_prefix = std::string(require_value(arg));
    } else if (arg == "--help") {
      throw std::invalid_argument("help");
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(arg));
    }
  }

  return options;
}

ResolvedMissionOptions ResolveMissionOptions(const CliOptions& options) {
  ResolvedMissionOptions resolved {
      .profile =
          {
              .distance_ly = options.distance_ly,
              .beta = options.beta,
              .sample_count = options.samples,
          },
      .preset = std::nullopt,
  };

  if (!options.destination_id.empty()) {
    resolved.preset = relativity::FindMissionPreset(options.destination_id);
    if (!resolved.preset.has_value()) {
      throw std::invalid_argument("unknown destination preset: " + options.destination_id);
    }
    if (!options.distance_explicit) {
      resolved.profile.distance_ly = resolved.preset->distance_ly;
    }
    if (!options.beta_explicit) {
      resolved.profile.beta = resolved.preset->suggested_beta;
    }
  }

  return resolved;
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

double LerpDouble(double start, double end, double amount) {
  return start + ((end - start) * amount);
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

Vector3 LerpPoint(Vector3 start, Vector3 end, float amount) {
  return {
      start.x + ((end.x - start.x) * amount),
      start.y + ((end.y - start.y) * amount),
      start.z + ((end.z - start.z) * amount),
  };
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

float Dot(Vector3 left, Vector3 right) {
  return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

Vector3 Cross(Vector3 left, Vector3 right) {
  return {
      (left.y * right.z) - (left.z * right.y),
      (left.z * right.x) - (left.x * right.z),
      (left.x * right.y) - (left.y * right.x),
  };
}

Vector3 QuadraticBezierPoint(Vector3 start, Vector3 control, Vector3 end, float amount) {
  const Vector3 a = LerpPoint(start, control, amount);
  const Vector3 b = LerpPoint(control, end, amount);
  return LerpPoint(a, b, amount);
}

Vector3 CameraPositionFromOrbit(Vector3 target, float distance, float yaw, float pitch) {
  const float cos_pitch = std::cos(pitch);
  return {
      target.x + (distance * cos_pitch * std::sin(yaw)),
      target.y + (distance * std::sin(pitch)),
      target.z + (distance * cos_pitch * std::cos(yaw)),
  };
}

float ProperTimeRateTubeRadius(double proper_time_rate) {
  constexpr float kMinimumRadius = 0.16F;
  constexpr float kMaximumRadius = 1.72F;
  const float normalized_rate = Clamp01(static_cast<float>(proper_time_rate));
  return kMinimumRadius + ((kMaximumRadius - kMinimumRadius) * std::pow(normalized_rate, 2.55F));
}

PathFrame EvaluatePathFrame(Vector3 start, Vector3 control, Vector3 end, float fraction, float proper_time_rate_radius) {
  const float clamped_fraction = Clamp01(fraction);
  const float delta = 0.0035F;
  const float previous_fraction = std::max(0.0F, clamped_fraction - delta);
  const float next_fraction = std::min(1.0F, clamped_fraction + delta);
  const Vector3 center = QuadraticBezierPoint(start, control, end, clamped_fraction);
  const Vector3 tangent = Normalize(Subtract(QuadraticBezierPoint(start, control, end, next_fraction),
                                             QuadraticBezierPoint(start, control, end, previous_fraction)));
  const Vector3 reference_axis = std::abs(Dot(tangent, {0.0F, 1.0F, 0.0F})) > 0.92F ? Vector3 {1.0F, 0.0F, 0.0F}
                                                                                     : Vector3 {0.0F, 1.0F, 0.0F};
  const Vector3 right = Normalize(Cross(reference_axis, tangent));
  const Vector3 up = Normalize(Cross(tangent, right));
  return {
      .center = center,
      .tangent = tangent,
      .right = right,
      .up = up,
      .path_fraction = clamped_fraction,
      .proper_time_rate_radius = proper_time_rate_radius,
  };
}

Model BuildProperTimeRateTubeModel(const relativity::MissionResult& mission, std::size_t current_index, Vector3 start,
                                   Vector3 control, Vector3 end, int sides) {
  std::vector<std::size_t> ring_indices;
  const std::size_t stride = std::max<std::size_t>(1, current_index / 72);
  for (std::size_t index = 0; index <= current_index; index += stride) {
    ring_indices.push_back(index);
  }
  if (ring_indices.empty() || ring_indices.back() != current_index) {
    ring_indices.push_back(current_index);
  }
  if (ring_indices.size() < 2) {
    ring_indices.push_back(std::min(current_index + 1, mission.samples.size() - 1));
  }

  std::vector<PathFrame> rings;
  rings.reserve(ring_indices.size());

  for (const auto sample_index : ring_indices) {
    const auto& sample = mission.samples[sample_index];
    const float fraction = static_cast<float>(sample.position_ly / mission.profile.distance_ly);
    rings.push_back(EvaluatePathFrame(start, control, end, fraction, ProperTimeRateTubeRadius(sample.proper_time_rate)));
  }

  std::vector<float> vertices;
  std::vector<float> normals;
  std::vector<float> texcoords;
  vertices.reserve((rings.size() - 1) * static_cast<std::size_t>(sides) * 18);
  normals.reserve((rings.size() - 1) * static_cast<std::size_t>(sides) * 18);
  texcoords.reserve((rings.size() - 1) * static_cast<std::size_t>(sides) * 12);

  const auto append_vertex = [&](Vector3 position, Vector3 normal, float u, float v) {
    vertices.push_back(position.x);
    vertices.push_back(position.y);
    vertices.push_back(position.z);
    normals.push_back(normal.x);
    normals.push_back(normal.y);
    normals.push_back(normal.z);
    texcoords.push_back(u);
    texcoords.push_back(v);
  };

  for (std::size_t ring = 0; ring + 1 < rings.size(); ++ring) {
    const PathFrame& current_ring = rings[ring];
    const PathFrame& next_ring = rings[ring + 1];

    for (int side = 0; side < sides; ++side) {
      const float angle0 = (static_cast<float>(side) / static_cast<float>(sides)) * 2.0F * PI;
      const float angle1 = (static_cast<float>(side + 1) / static_cast<float>(sides)) * 2.0F * PI;

      const Vector3 radial0 = Add(Scale(current_ring.right, std::cos(angle0)), Scale(current_ring.up, std::sin(angle0)));
      const Vector3 radial1 = Add(Scale(current_ring.right, std::cos(angle1)), Scale(current_ring.up, std::sin(angle1)));
      const Vector3 next_radial0 = Add(Scale(next_ring.right, std::cos(angle0)), Scale(next_ring.up, std::sin(angle0)));
      const Vector3 next_radial1 = Add(Scale(next_ring.right, std::cos(angle1)), Scale(next_ring.up, std::sin(angle1)));

      const Vector3 p0 = Add(current_ring.center, Scale(radial0, current_ring.proper_time_rate_radius));
      const Vector3 p1 = Add(current_ring.center, Scale(radial1, current_ring.proper_time_rate_radius));
      const Vector3 p2 = Add(next_ring.center, Scale(next_radial0, next_ring.proper_time_rate_radius));
      const Vector3 p3 = Add(next_ring.center, Scale(next_radial1, next_ring.proper_time_rate_radius));

      const float v0 = static_cast<float>(side) / static_cast<float>(sides);
      const float v1 = static_cast<float>(side + 1) / static_cast<float>(sides);

      append_vertex(p0, radial0, current_ring.path_fraction, v0);
      append_vertex(p2, next_radial0, next_ring.path_fraction, v0);
      append_vertex(p1, radial1, current_ring.path_fraction, v1);

      append_vertex(p1, radial1, current_ring.path_fraction, v1);
      append_vertex(p2, next_radial0, next_ring.path_fraction, v0);
      append_vertex(p3, next_radial1, next_ring.path_fraction, v1);
    }
  }

  Mesh mesh {};
  mesh.vertexCount = static_cast<int>(vertices.size() / 3);
  mesh.triangleCount = mesh.vertexCount / 3;
  mesh.vertices = static_cast<float*>(MemAlloc(vertices.size() * sizeof(float)));
  mesh.normals = static_cast<float*>(MemAlloc(normals.size() * sizeof(float)));
  mesh.texcoords = static_cast<float*>(MemAlloc(texcoords.size() * sizeof(float)));

  std::copy(vertices.begin(), vertices.end(), mesh.vertices);
  std::copy(normals.begin(), normals.end(), mesh.normals);
  std::copy(texcoords.begin(), texcoords.end(), mesh.texcoords);

  UploadMesh(&mesh, false);
  return LoadModelFromMesh(mesh);
}

void UpdateShaderLight(Shader shader, const ShaderLight& light) {
  SetShaderValue(shader, light.enabled_location, &light.enabled, SHADER_UNIFORM_INT);
  SetShaderValue(shader, light.type_location, &light.type, SHADER_UNIFORM_INT);

  const float position[3] {light.position.x, light.position.y, light.position.z};
  const float target[3] {light.target.x, light.target.y, light.target.z};
  const float color[4] {
      static_cast<float>(light.color.r) / 255.0F,
      static_cast<float>(light.color.g) / 255.0F,
      static_cast<float>(light.color.b) / 255.0F,
      static_cast<float>(light.color.a) / 255.0F,
  };

  SetShaderValue(shader, light.position_location, position, SHADER_UNIFORM_VEC3);
  SetShaderValue(shader, light.target_location, target, SHADER_UNIFORM_VEC3);
  SetShaderValue(shader, light.color_location, color, SHADER_UNIFORM_VEC4);
}

ShaderLight CreateShaderLight(int index, int type, Vector3 position, Vector3 target, Color color, Shader shader) {
  ShaderLight light {};
  light.enabled = true;
  light.type = type;
  light.position = position;
  light.target = target;
  light.color = color;
  light.enabled_location = GetShaderLocation(shader, TextFormat("lights[%i].enabled", index));
  light.type_location = GetShaderLocation(shader, TextFormat("lights[%i].type", index));
  light.position_location = GetShaderLocation(shader, TextFormat("lights[%i].position", index));
  light.target_location = GetShaderLocation(shader, TextFormat("lights[%i].target", index));
  light.color_location = GetShaderLocation(shader, TextFormat("lights[%i].color", index));
  UpdateShaderLight(shader, light);
  return light;
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

double SampleTime(const relativity::WorldlineSample& sample, TimeDomain domain) {
  return domain == TimeDomain::Coordinate ? sample.coordinate_time_years : sample.proper_time_years;
}

double NiceTimeInterval(double total_years, int target_markers) {
  if (!(total_years > 0.0) || !std::isfinite(total_years)) {
    return 1.0;
  }

  const double raw_interval = total_years / static_cast<double>(std::max(1, target_markers));
  const double magnitude = std::pow(10.0, std::floor(std::log10(raw_interval)));
  for (const double multiplier : {1.0, 2.0, 5.0, 10.0}) {
    const double interval = magnitude * multiplier;
    if (interval >= raw_interval) {
      return interval;
    }
  }
  return magnitude * 10.0;
}

PulseMarker InterpolatePulseMarker(const relativity::WorldlineSample& previous, const relativity::WorldlineSample& next,
                                   double target_time_years, TimeDomain domain, double distance_ly) {
  const double previous_time = SampleTime(previous, domain);
  const double next_time = SampleTime(next, domain);
  const double denominator = next_time - previous_time;
  const double alpha = denominator > 0.0 ? std::clamp((target_time_years - previous_time) / denominator, 0.0, 1.0) : 0.0;
  const double position_ly = LerpDouble(previous.position_ly, next.position_ly, alpha);
  const double proper_time_rate = LerpDouble(previous.proper_time_rate, next.proper_time_rate, alpha);
  return {
      .time_years = target_time_years,
      .path_fraction = Clamp01(static_cast<float>(position_ly / distance_ly)),
      .proper_time_rate_radius = ProperTimeRateTubeRadius(proper_time_rate),
  };
}

std::vector<PulseMarker> BuildPulseMarkers(const relativity::MissionResult& mission, std::size_t current_index,
                                           double interval_years, TimeDomain domain) {
  std::vector<PulseMarker> markers;
  if ((current_index < 1) || !(interval_years > 0.0)) {
    return markers;
  }

  const double max_time = SampleTime(mission.samples[current_index], domain);
  double target_time = interval_years;
  markers.reserve(static_cast<std::size_t>(max_time / interval_years) + 1);

  for (std::size_t index = 1; index <= current_index && target_time <= (max_time + 1e-9); ++index) {
    const auto& previous = mission.samples[index - 1];
    const auto& next = mission.samples[index];
    const double next_time = SampleTime(next, domain);

    if (next_time + 1e-9 < target_time) {
      continue;
    }

    while (target_time <= (next_time + 1e-9) && target_time <= (max_time + 1e-9)) {
      markers.push_back(InterpolatePulseMarker(previous, next, target_time, domain, mission.profile.distance_ly));
      target_time += interval_years;
    }
  }

  return markers;
}

void DrawRingSegments(const PathFrame& frame, float radius, float thickness, Color color, int segments) {
  for (int segment = 0; segment < segments; ++segment) {
    const float angle0 = (static_cast<float>(segment) / static_cast<float>(segments)) * 2.0F * PI;
    const float angle1 = (static_cast<float>(segment + 1) / static_cast<float>(segments)) * 2.0F * PI;
    const Vector3 radial0 = Add(Scale(frame.right, std::cos(angle0)), Scale(frame.up, std::sin(angle0)));
    const Vector3 radial1 = Add(Scale(frame.right, std::cos(angle1)), Scale(frame.up, std::sin(angle1)));
    const Vector3 p0 = Add(frame.center, Scale(radial0, radius));
    const Vector3 p1 = Add(frame.center, Scale(radial1, radius));
    DrawCylinderEx(p0, p1, thickness, thickness, 6, color);
  }
}

void DrawTrajectoryCurve(Vector3 start, Vector3 control, Vector3 end, float maximum_fraction, int segments, Color color) {
  if (maximum_fraction <= 0.0F) {
    return;
  }

  const int usable_segments = std::max(2, static_cast<int>(std::ceil(static_cast<float>(segments) * maximum_fraction)));
  Vector3 previous = QuadraticBezierPoint(start, control, end, 0.0F);
  for (int segment = 1; segment <= usable_segments; ++segment) {
    const float fraction = maximum_fraction * (static_cast<float>(segment) / static_cast<float>(usable_segments));
    const Vector3 current = QuadraticBezierPoint(start, control, end, fraction);
    DrawLine3D(previous, current, color);
    previous = current;
  }
}

bool NeedsCosmologyNote(double distance_ly) {
  return distance_ly >= 1.0e6;
}

std::string DestinationLabel(const std::optional<relativity::MissionPreset>& preset) {
  if (preset.has_value()) {
    return std::string(preset->display_name);
  }
  return "Configured destination";
}

std::string ExportImagePath(std::string_view prefix, std::string_view suffix) {
  return std::string(prefix) + "_" + std::string(suffix) + ".png";
}

bool ExportRenderTexturePng(RenderTexture2D target, const std::string& path) {
  Image image = LoadImageFromTexture(target.texture);
  ImageFlipVertical(&image);
  const bool success = ExportImage(image, path.c_str());
  UnloadImage(image);
  return success;
}

void DrawSummaryLine(const Font& font, std::string_view label, const std::string& value, float x, float y, float width,
                     Color label_color, Color value_color) {
  DrawUiText(font, std::string(label), x, y, 22.0F, label_color);
  const float value_width = MeasureTextEx(font, value.c_str(), 22.0F, 0.0F).x;
  DrawUiText(font, value, x + width - value_width, y, 22.0F, value_color);
}

void DrawMissionSummaryCard(const Font& ui_font, const relativity::MissionResult& mission, const std::string& mission_name,
                            const std::string& destination_name, double coordinate_pulse_interval,
                            double proper_pulse_interval, bool cosmology_note, Color app_background,
                            Color viewport_background, Color panel_edge, Color text, Color muted, Color earth_frame,
                            Color traveler_frame, Color warning) {
  const Rectangle card {36.0F, 36.0F, 952.0F, 568.0F};
  DrawRectangleGradientV(0, 0, 1024, 640, app_background, viewport_background);
  DrawPanel(card, Color {19, 23, 29, 236}, Fade(panel_edge, 0.80F));

  DrawUiText(ui_font, "Relativity Mission Summary", card.x + 28.0F, card.y + 24.0F, 34.0F, text);
  DrawUiText(ui_font, mission_name, card.x + 30.0F, card.y + 72.0F, 22.0F, traveler_frame);
  DrawUiText(ui_font, destination_name, card.x + 30.0F, card.y + 104.0F, 20.0F, muted);

  const float metrics_x = card.x + 30.0F;
  const float metrics_y = card.y + 154.0F;
  const float metrics_width = 388.0F;

  DrawSummaryLine(ui_font, "distance", FormatFixed(mission.profile.distance_ly, 2) + " ly", metrics_x, metrics_y,
                  metrics_width, muted, text);
  DrawSummaryLine(ui_font, "peak beta", FormatFixed(mission.summary.peak_beta, 3), metrics_x, metrics_y + 38.0F,
                  metrics_width, muted, text);
  DrawSummaryLine(ui_font, "peak Lorentz factor", FormatFixed(mission.summary.gamma, 4), metrics_x, metrics_y + 76.0F,
                  metrics_width, muted, text);
  DrawSummaryLine(ui_font, "proper acceleration", FormatFixed(mission.summary.proper_acceleration_ly_per_year2, 3) + " ly / y^2",
                  metrics_x, metrics_y + 114.0F, metrics_width, muted, text);
  DrawSummaryLine(ui_font, "coordinate time", FormatFixed(mission.summary.coordinate_time_years, 3) + " y",
                  metrics_x, metrics_y + 174.0F, metrics_width, earth_frame, earth_frame);
  DrawSummaryLine(ui_font, "proper time", FormatFixed(mission.summary.proper_time_years, 3) + " y",
                  metrics_x, metrics_y + 212.0F, metrics_width, traveler_frame, traveler_frame);
  DrawSummaryLine(ui_font, "elapsed difference", FormatFixed(mission.summary.elapsed_difference_years, 3) + " y",
                  metrics_x, metrics_y + 250.0F, metrics_width, traveler_frame, traveler_frame);
  DrawSummaryLine(ui_font, "arrival signal to Earth", FormatFixed(mission.summary.arrival_signal_to_earth_years, 3) + " y",
                  metrics_x, metrics_y + 288.0F, metrics_width, earth_frame, earth_frame);

  const Rectangle frame_box {card.x + 500.0F, card.y + 150.0F, 420.0F, 266.0F};
  DrawPanel(frame_box, Color {14, 17, 21, 220}, Fade(panel_edge, 0.65F));
  DrawUiText(ui_font, "Frame Comparison", frame_box.x + 22.0F, frame_box.y + 18.0F, 24.0F, text);
  DrawUiText(ui_font, "Earth frame", frame_box.x + 52.0F, frame_box.y + 62.0F, 20.0F, earth_frame);
  DrawUiText(ui_font, "Traveler frame", frame_box.x + 232.0F, frame_box.y + 62.0F, 20.0F, traveler_frame);

  const Rectangle earth_rail {frame_box.x + 70.0F, frame_box.y + 102.0F, 48.0F, 118.0F};
  const Rectangle traveler_rail {frame_box.x + 274.0F, frame_box.y + 102.0F, 48.0F, 118.0F};
  DrawRectangleRounded(earth_rail, 0.2F, 8, Fade(earth_frame, 0.08F));
  DrawRectangleRoundedLinesEx(earth_rail, 0.2F, 8, 1.0F, Fade(earth_frame, 0.45F));
  DrawRectangleRounded({earth_rail.x, earth_rail.y, earth_rail.width, earth_rail.height}, 0.2F, 8, Fade(earth_frame, 0.30F));

  const float traveler_fill = static_cast<float>(mission.summary.proper_time_years / mission.summary.coordinate_time_years);
  DrawRectangleRounded(traveler_rail, 0.2F, 8, Fade(traveler_frame, 0.08F));
  DrawRectangleRoundedLinesEx(traveler_rail, 0.2F, 8, 1.0F, Fade(traveler_frame, 0.48F));
  DrawRectangleRounded({traveler_rail.x, traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_fill),
                        traveler_rail.width, traveler_rail.height * traveler_fill},
                       0.2F, 8, Fade(traveler_frame, 0.34F));

  DrawUiText(ui_font, FormatFixed(mission.summary.coordinate_time_years, 2) + " y", earth_rail.x - 14.0F, earth_rail.y + 136.0F,
             18.0F, earth_frame);
  DrawUiText(ui_font, FormatFixed(mission.summary.proper_time_years, 2) + " y", traveler_rail.x - 14.0F,
             traveler_rail.y + 136.0F, 18.0F, traveler_frame);

  DrawUiText(ui_font, "proper pulse interval", frame_box.x + 22.0F, frame_box.y + 234.0F, 18.0F, muted);
  DrawUiText(ui_font, FormatFixed(proper_pulse_interval, proper_pulse_interval < 1.0 ? 2 : 1) + " y",
             frame_box.x + 286.0F, frame_box.y + 234.0F, 18.0F, traveler_frame);
  DrawUiText(ui_font, "coordinate pulse interval", frame_box.x + 22.0F, frame_box.y + 262.0F, 18.0F, muted);
  DrawUiText(ui_font, FormatFixed(coordinate_pulse_interval, coordinate_pulse_interval < 1.0 ? 2 : 1) + " y",
             frame_box.x + 286.0F, frame_box.y + 262.0F, 18.0F, earth_frame);

  DrawUiText(ui_font,
             cosmology_note ? "Special relativity only. Cosmological expansion is not included at this distance."
                            : "Special relativity only. Earth and destination are stationary in one inertial frame.",
             card.x + 30.0F, card.y + 500.0F, 18.0F, cosmology_note ? warning : muted);
}

bool ExportMissionArtifacts(std::string_view export_prefix, const Font& ui_font, RenderTexture2D scene_target,
                            const relativity::MissionResult& mission, const std::string& mission_name,
                            const std::string& destination_name, double coordinate_pulse_interval,
                            double proper_pulse_interval, bool cosmology_note, Color app_background,
                            Color viewport_background, Color panel_edge, Color text, Color muted, Color earth_frame,
                            Color traveler_frame, Color warning) {
  if (export_prefix.empty()) {
    return false;
  }

  const std::string visual_path = ExportImagePath(export_prefix, "visual");
  const std::string summary_path = ExportImagePath(export_prefix, "summary");

  RenderTexture2D summary_target = LoadRenderTexture(1024, 640);
  SetTextureFilter(summary_target.texture, TEXTURE_FILTER_BILINEAR);

  BeginTextureMode(summary_target);
  ClearBackground(app_background);
  DrawMissionSummaryCard(ui_font, mission, mission_name, destination_name, coordinate_pulse_interval, proper_pulse_interval,
                         cosmology_note, app_background, viewport_background, panel_edge, text, muted, earth_frame,
                         traveler_frame, warning);
  EndTextureMode();

  const bool visual_saved = ExportRenderTexturePng(scene_target, visual_path);
  const bool summary_saved = ExportRenderTexturePng(summary_target, summary_path);
  UnloadRenderTexture(summary_target);

  TraceLog(summary_saved && visual_saved ? LOG_INFO : LOG_WARNING,
           "mission export summary=%s visual=%s", summary_saved ? summary_path.c_str() : "failed",
           visual_saved ? visual_path.c_str() : "failed");

  return summary_saved && visual_saved;
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
      TraceLog(LOG_INFO, "%s", UsageText(argv[0]).c_str());
      return 0;
    }
    TraceLog(LOG_ERROR, "%s", error.what());
    TraceLog(LOG_INFO, "%s", UsageText(argv[0]).c_str());
    return 1;
  }

  ResolvedMissionOptions resolved;
  relativity::MissionResult mission;
  try {
    resolved = ResolveMissionOptions(options);
    mission = relativity::SimulateMission(resolved.profile);
  } catch (const std::exception& error) {
    TraceLog(LOG_ERROR, "%s", error.what());
    return 1;
  }

  const std::string destination_name = DestinationLabel(resolved.preset);
  const std::string mission_name =
      resolved.preset.has_value() ? (std::string(resolved.preset->display_name) + " mission") : "Custom mission";
  const double coordinate_pulse_interval = NiceTimeInterval(mission.summary.coordinate_time_years, 9);
  const double proper_pulse_interval = NiceTimeInterval(mission.summary.proper_time_years, 9);
  const bool cosmology_note = NeedsCosmologyNote(mission.profile.distance_ly);
  const bool auto_export = !options.export_prefix.empty();

  InitWindow(screen_width, screen_height, "Relativity Mission Control");
  SetTargetFPS(60);

  const Rectangle viewport {40.0F, 96.0F, 1200.0F, 600.0F};
  const Rectangle controls {40.0F, 720.0F, 1200.0F, 64.0F};
  const Rectangle play_button {60.0F, 736.0F, 92.0F, 32.0F};
  const Rectangle reset_button {164.0F, 736.0F, 92.0F, 32.0F};
  const Rectangle slider_track {304.0F, 751.0F, 700.0F, 8.0F};
  const Rectangle mission_panel {76.0F, 138.0F, 236.0F, 186.0F};
  const Rectangle frames_panel {1022.0F, 138.0F, 190.0F, 236.0F};

  const Color app_background {10, 12, 16, 255};
  const Color viewport_background {14, 17, 21, 255};
  const Color panel_edge {54, 61, 73, 255};
  const Color grid {34, 39, 48, 255};
  const Color text {228, 233, 241, 255};
  const Color muted {132, 141, 154, 255};
  const Color structure {176, 184, 194, 255};
  const Color earth_frame {149, 164, 182, 255};
  const Color traveler_frame {214, 221, 230, 255};
  const Color earth_body {114, 133, 164, 255};
  const Color destination_body {176, 167, 144, 255};
  const Color warning {220, 197, 136, 255};

  bool custom_font_loaded = false;
  const Font ui_font = LoadUiFont(custom_font_loaded);
  RenderTexture2D scene_target = LoadRenderTexture(static_cast<int>(viewport.width), static_cast<int>(viewport.height));
  SetTextureFilter(scene_target.texture, TEXTURE_FILTER_BILINEAR);
  Shader lighting_shader = LoadShaderFromMemory(kLightingVertexShader, kLightingFragmentShader);
  const int ambient_location = GetShaderLocation(lighting_shader, "ambient");
  const int view_position_location = GetShaderLocation(lighting_shader, "viewPos");
  const float ambient_value[4] {0.22F, 0.24F, 0.28F, 1.0F};
  SetShaderValue(lighting_shader, ambient_location, ambient_value, SHADER_UNIFORM_VEC4);

  Camera3D camera {};
  Vector3 camera_target {0.0F, 0.15F, 0.0F};
  float camera_distance = 24.0F;
  float camera_yaw = -0.42F;
  float camera_pitch = 0.24F;
  camera.position = CameraPositionFromOrbit(camera_target, camera_distance, camera_yaw, camera_pitch);
  camera.target = camera_target;
  camera.up = {0.0F, 1.0F, 0.0F};
  camera.fovy = 38.0F;
  camera.projection = CAMERA_PERSPECTIVE;

  const Vector3 earth_world {-10.5F, 2.2F, -4.8F};
  const Vector3 turnaround_world {0.0F, 0.8F, 0.0F};
  const Vector3 destination_world {10.5F, -1.9F, 4.8F};
  const Vector3 world_up {0.0F, 1.0F, 0.0F};
  const Vector3 turnaround_curve_world = QuadraticBezierPoint(earth_world, turnaround_world, destination_world, 0.5F);
  ShaderLight lights[3] {
      CreateShaderLight(0, 0, {-8.5F, 11.0F, 10.0F}, {0.0F, 0.0F, 0.0F}, Color {240, 244, 255, 255}, lighting_shader),
      CreateShaderLight(1, 1, {8.5F, 4.5F, 5.5F}, {0.0F, 0.0F, 0.0F}, Color {196, 212, 255, 255}, lighting_shader),
      CreateShaderLight(2, 1, {-2.5F, 2.0F, -8.0F}, {0.0F, 0.0F, 0.0F}, Color {198, 186, 170, 255}, lighting_shader),
  };

  Model proper_time_rate_tube_model {};
  bool proper_time_rate_tube_ready = false;
  std::size_t proper_time_rate_tube_index = std::numeric_limits<std::size_t>::max();

  bool playing = false;
  bool dragging_slider = false;
  bool export_requested = auto_export;
  float stage = auto_export ? 1.0F : 0.0F;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();
    const Vector2 mouse = GetMousePosition();
    const Vector2 mouse_delta = GetMouseDelta();
    const bool mouse_in_viewport = CheckCollisionPointRec(mouse, viewport);

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
      if (mouse_in_viewport) {
        camera_distance = std::clamp(camera_distance - (wheel * 1.15F), 7.5F, 34.0F);
      } else {
        stage = Clamp01(stage + (wheel * 0.009F));
        playing = false;
      }
    }

    if (mouse_in_viewport && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      camera_yaw -= mouse_delta.x * 0.0065F;
      camera_pitch = std::clamp(camera_pitch - (mouse_delta.y * 0.0045F), -0.25F, 1.10F);
    }

    if (mouse_in_viewport && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
      const Vector3 forward = Normalize(Subtract(camera.target, camera.position));
      const Vector3 right {forward.z, 0.0F, -forward.x};
      const Vector3 pan_offset = Add(Scale(right, -mouse_delta.x * 0.018F), Scale(world_up, mouse_delta.y * 0.018F));
      camera_target = Add(camera_target, pan_offset);
    }

    if (IsKeyPressed(KEY_C)) {
      camera_target = {0.0F, 0.15F, 0.0F};
      camera_distance = 24.0F;
      camera_yaw = -0.42F;
      camera_pitch = 0.24F;
    }
    if (IsKeyPressed(KEY_E)) {
      export_requested = true;
    }

    if (playing) {
      stage = Clamp01(stage + (dt * 0.02F));
      if (stage >= 1.0F) {
        playing = false;
      }
    }

    camera.position = CameraPositionFromOrbit(camera_target, camera_distance, camera_yaw, camera_pitch);
    camera.target = camera_target;

    const std::size_t current_index = std::min(
        mission.samples.size() - 1,
        static_cast<std::size_t>(std::round(stage * static_cast<float>(mission.samples.size() - 1))));
    const auto& current = mission.samples[current_index];

    if ((current_index != proper_time_rate_tube_index) && (current_index > 1)) {
      if (proper_time_rate_tube_ready) {
        UnloadModel(proper_time_rate_tube_model);
        proper_time_rate_tube_ready = false;
      }
      proper_time_rate_tube_model =
          BuildProperTimeRateTubeModel(mission, current_index, earth_world, turnaround_world, destination_world, 48);
      proper_time_rate_tube_model.materials[0].shader = lighting_shader;
      proper_time_rate_tube_ready = true;
      proper_time_rate_tube_index = current_index;
    }

    const float travel_fraction = static_cast<float>(current.position_ly / mission.profile.distance_ly);
    const PathFrame ship_frame = EvaluatePathFrame(earth_world, turnaround_world, destination_world, travel_fraction,
                                                   ProperTimeRateTubeRadius(current.proper_time_rate));
    const bool decelerating_phase = current.signed_proper_acceleration_ly_per_year2 < 0.0;
    const Vector3 ship_world = ship_frame.center;
    const Vector3 acceleration_offset_start = Add(ship_world, {0.0F, 0.78F, 0.0F});
    const float acceleration_vector_length = 0.86F + (1.55F * static_cast<float>(current.beta));
    const float acceleration_direction_sign = decelerating_phase ? -1.0F : 1.0F;
    const Vector3 acceleration_vector_end =
        Add(acceleration_offset_start, Scale(ship_frame.tangent, acceleration_vector_length * acceleration_direction_sign));
    const Vector2 earth_label = GetWorldToScreenEx(earth_world, camera, static_cast<int>(viewport.width),
                                                   static_cast<int>(viewport.height));
    const Vector2 destination_label = GetWorldToScreenEx(destination_world, camera, static_cast<int>(viewport.width),
                                                         static_cast<int>(viewport.height));
    const Vector2 turnaround_label = GetWorldToScreenEx(turnaround_curve_world, camera, static_cast<int>(viewport.width),
                                                        static_cast<int>(viewport.height));
    const std::vector<PulseMarker> coordinate_pulses =
        BuildPulseMarkers(mission, current_index, coordinate_pulse_interval, TimeDomain::Coordinate);
    const std::vector<PulseMarker> proper_pulses =
        BuildPulseMarkers(mission, current_index, proper_pulse_interval, TimeDomain::Proper);

    BeginDrawing();
    ClearBackground(app_background);

    DrawUiText(ui_font, "Relativity Mission Control", 40.0F, 26.0F, 24.0F, text);
    DrawUiText(ui_font, mission_name + "    " + FormatFixed(mission.profile.distance_ly, 2) + " ly"
                           + "    beta_max " + FormatFixed(mission.summary.peak_beta, 2),
               42.0F, 56.0F, 15.0F, muted);

    BeginTextureMode(scene_target);
    ClearBackground(viewport_background);
    BeginMode3D(camera);

    const float view_position[3] {camera.position.x, camera.position.y, camera.position.z};
    SetShaderValue(lighting_shader, view_position_location, view_position, SHADER_UNIFORM_VEC3);
    for (const auto& light : lights) {
      UpdateShaderLight(lighting_shader, light);
    }

    DrawGrid(24, 1.5F);
    DrawTrajectoryCurve(earth_world, turnaround_world, destination_world, 1.0F, 90, Fade(grid, 0.28F));
    DrawTrajectoryCurve(earth_world, turnaround_world, destination_world, travel_fraction, 90, Fade(structure, 0.32F));
    DrawSphereEx(earth_world, 0.46F, 18, 18, earth_body);
    DrawSphereEx(destination_world, 0.38F, 18, 18, destination_body);
    DrawSphereWires(earth_world, 0.46F, 12, 12, Fade(structure, 0.65F));
    DrawSphereWires(destination_world, 0.38F, 12, 12, Fade(structure, 0.52F));

    if (proper_time_rate_tube_ready) {
      DrawModel(proper_time_rate_tube_model, {0.0F, 0.0F, 0.0F}, 1.0F, Fade(traveler_frame, 0.26F));
      DrawModelWires(proper_time_rate_tube_model, {0.0F, 0.0F, 0.0F}, 1.0F, Fade(traveler_frame, 0.54F));
    }

    for (const auto& marker : coordinate_pulses) {
      const PathFrame frame = EvaluatePathFrame(earth_world, turnaround_world, destination_world, marker.path_fraction,
                                                marker.proper_time_rate_radius);
      const float radius = std::max(frame.proper_time_rate_radius + 0.30F, frame.proper_time_rate_radius * 1.18F);
      DrawRingSegments(frame, radius, 0.016F, Fade(earth_frame, 0.36F), 28);
    }

    for (const auto& marker : proper_pulses) {
      const PathFrame frame = EvaluatePathFrame(earth_world, turnaround_world, destination_world, marker.path_fraction,
                                                marker.proper_time_rate_radius);
      DrawRingSegments(frame, frame.proper_time_rate_radius + 0.03F, 0.030F, Fade(traveler_frame, 0.70F), 28);
    }

    const Vector3 probe_tail = Add(ship_world, Scale(ship_frame.tangent, -0.65F));
    const Vector3 probe_nose = Add(ship_world, Scale(ship_frame.tangent, 0.65F));
    DrawCylinderEx(probe_tail, probe_nose, 0.16F, 0.08F, 12, Fade(text, 0.96F));
    DrawSphereEx(ship_world, 0.15F, 10, 10, Fade(app_background, 0.92F));
    DrawCylinderEx(acceleration_offset_start, acceleration_vector_end, 0.03F, 0.03F, 10, traveler_frame);
    DrawCylinderEx(Add(acceleration_vector_end, Scale(ship_frame.tangent, -0.18F)), acceleration_vector_end, 0.10F, 0.0F,
                   10, traveler_frame);

    EndMode3D();
    EndTextureMode();

    DrawPanel(viewport, viewport_background, panel_edge);
    DrawTexturePro(scene_target.texture, {0.0F, 0.0F, static_cast<float>(scene_target.texture.width),
                                          -static_cast<float>(scene_target.texture.height)},
                   viewport, {0.0F, 0.0F}, 0.0F, WHITE);
    DrawRectangleRoundedLinesEx(viewport, 0.015F, 10, 1.0F, panel_edge);

    DrawPanel(mission_panel, Color {19, 23, 29, 184}, Fade(panel_edge, 0.60F));
    DrawPanel(frames_panel, Color {19, 23, 29, 184}, Fade(panel_edge, 0.60F));

    DrawUiText(ui_font, "Earth", viewport.x + earth_label.x - 18.0F, viewport.y + earth_label.y - 42.0F, 18.0F, structure);
    DrawUiText(ui_font, destination_name, viewport.x + destination_label.x - 56.0F, viewport.y + destination_label.y + 18.0F,
               16.0F, structure);

    if (current.progress >= 0.42) {
      DrawUiText(ui_font, "Turnaround Event", viewport.x + turnaround_label.x + 24.0F,
                 viewport.y + turnaround_label.y - 10.0F, 15.0F, text);
    }

    DrawUiText(ui_font, "Mission State", mission_panel.x + 16.0F, mission_panel.y + 14.0F, 16.0F, muted);
    DrawUiText(ui_font, "phase", mission_panel.x + 16.0F, mission_panel.y + 44.0F, 14.0F, muted);
    DrawUiText(ui_font, std::string(PhaseLabel(current.progress)), mission_panel.x + 112.0F, mission_panel.y + 44.0F, 14.0F,
               text);
    DrawUiText(ui_font, "beta", mission_panel.x + 16.0F, mission_panel.y + 72.0F, 14.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.beta, 3), mission_panel.x + 112.0F, mission_panel.y + 72.0F, 14.0F, text);
    DrawUiText(ui_font, "Lorentz factor", mission_panel.x + 16.0F, mission_panel.y + 100.0F, 14.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.gamma, 4), mission_panel.x + 112.0F, mission_panel.y + 100.0F, 14.0F, text);
    DrawUiText(ui_font, "proper-time rate", mission_panel.x + 16.0F, mission_panel.y + 128.0F, 14.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.proper_time_rate, 4), mission_panel.x + 112.0F, mission_panel.y + 128.0F, 14.0F,
               text);
    DrawUiText(ui_font, DynamicsLabel(current.signed_proper_acceleration_ly_per_year2), mission_panel.x + 16.0F,
               mission_panel.y + 156.0F, 13.0F, muted);
    DrawUiText(ui_font,
               FormatFixed(std::abs(current.signed_proper_acceleration_ly_per_year2), 3) + " ly / y^2",
               mission_panel.x + 16.0F, mission_panel.y + 174.0F, 13.0F, text);

    const Rectangle earth_rail {frames_panel.x + 24.0F, frames_panel.y + 62.0F, 28.0F, 116.0F};
    const Rectangle traveler_rail {frames_panel.x + 138.0F, frames_panel.y + 62.0F, 28.0F, 116.0F};
    const float earth_progress = static_cast<float>(current.coordinate_time_years / mission.summary.coordinate_time_years);
    const float traveler_progress = static_cast<float>(current.proper_time_years / mission.summary.coordinate_time_years);
    const float earth_marker_y = earth_rail.y + earth_rail.height - (earth_rail.height * earth_progress);
    const float traveler_marker_y = traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_progress);

    DrawUiText(ui_font, "Frame Times", frames_panel.x + 16.0F, frames_panel.y + 14.0F, 16.0F, muted);
    DrawUiText(ui_font, "Earth", earth_rail.x - 2.0F, earth_rail.y - 24.0F, 13.0F, earth_frame);
    DrawUiText(ui_font, "Traveler", traveler_rail.x - 8.0F, traveler_rail.y - 24.0F, 13.0F, traveler_frame);

    DrawRectangleRounded(earth_rail, 0.2F, 8, Fade(earth_frame, 0.07F));
    DrawRectangleRoundedLinesEx(earth_rail, 0.2F, 8, 1.0F, Fade(earth_frame, 0.42F));
    DrawRectangleRounded({earth_rail.x, earth_rail.y + earth_rail.height - (earth_rail.height * earth_progress),
                          earth_rail.width, earth_rail.height * earth_progress},
                         0.2F, 8, Fade(earth_frame, 0.30F));

    DrawRectangleRounded(traveler_rail, 0.2F, 8, Fade(traveler_frame, 0.07F));
    DrawRectangleRoundedLinesEx(traveler_rail, 0.2F, 8, 1.0F, Fade(traveler_frame, 0.48F));
    DrawRectangleRounded({traveler_rail.x, traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_progress),
                          traveler_rail.width, traveler_rail.height * traveler_progress},
                         0.2F, 8, Fade(traveler_frame, 0.32F));

    DrawLineEx({earth_rail.x - 10.0F, earth_marker_y}, {earth_rail.x + earth_rail.width + 10.0F, earth_marker_y}, 2.0F,
               earth_frame);
    DrawLineEx({traveler_rail.x - 10.0F, traveler_marker_y},
               {traveler_rail.x + traveler_rail.width + 10.0F, traveler_marker_y}, 2.0F, traveler_frame);

    DrawUiText(ui_font, FormatFixed(current.coordinate_time_years, 2) + " y", earth_rail.x - 12.0F,
               earth_rail.y + earth_rail.height + 10.0F, 14.0F, text);
    DrawUiText(ui_font, FormatFixed(current.proper_time_years, 2) + " y", traveler_rail.x - 10.0F,
               traveler_rail.y + traveler_rail.height + 10.0F, 14.0F, text);

    DrawUiText(ui_font, "delta", frames_panel.x + 16.0F, frames_panel.y + 200.0F, 13.0F, muted);
    DrawUiText(ui_font, FormatFixed(current.coordinate_time_years - current.proper_time_years, 3) + " y",
               frames_panel.x + 92.0F, frames_panel.y + 200.0F, 13.0F, traveler_frame);

    DrawRectangleRounded(controls, 0.04F, 10, viewport_background);
    DrawRectangleRoundedLinesEx(controls, 0.04F, 10, 1.0F, panel_edge);

    DrawButton(ui_font, play_button, playing ? "Pause" : "Play", playing, CheckCollisionPointRec(mouse, play_button),
               traveler_frame, traveler_frame, app_background);
    DrawButton(ui_font, reset_button, "Reset", false, CheckCollisionPointRec(mouse, reset_button),
               Color {88, 96, 108, 255}, panel_edge, text);

    DrawRectangleRounded(slider_track, 1.0F, 16, Color {44, 49, 57, 255});
    DrawRectangleRounded({slider_track.x, slider_track.y, slider_track.width * stage, slider_track.height}, 1.0F, 16,
                         traveler_frame);
    const Vector2 knob_center {slider_track.x + (slider_track.width * stage), slider_track.y + (slider_track.height * 0.5F)};
    DrawCircleV(knob_center, 8.0F, text);
    DrawCircleV(knob_center, 3.0F, app_background);

    DrawUiText(ui_font, "Playback", 304.0F, 730.0F, 17.0F, muted);
    DrawUiText(ui_font, "Space play/pause    Left/Right scrub    wheel over scene zooms    C resets camera", 948.0F,
               730.0F, 14.0F, muted);
    DrawUiText(ui_font, "Right-drag orbit    Middle-drag pan    End jump to arrival    E export PNGs", 900.0F, 748.0F,
               14.0F, muted);
    DrawUiText(ui_font, cosmology_note ? "SR model only; cosmology not included." : "SR model only.", 470.0F, 748.0F, 14.0F,
               cosmology_note ? warning : muted);

    EndDrawing();

    if (export_requested) {
      const std::string export_prefix = auto_export ? options.export_prefix : "mission_export";
      const bool export_ok =
          ExportMissionArtifacts(export_prefix, ui_font, scene_target, mission, mission_name, destination_name,
                                 coordinate_pulse_interval, proper_pulse_interval, cosmology_note, app_background,
                                 viewport_background, panel_edge, text, muted, earth_frame, traveler_frame, warning);
      if (!export_ok) {
        TraceLog(LOG_WARNING, "png export failed");
      }
      export_requested = false;
      if (auto_export) {
        break;
      }
    }
  }

  if (custom_font_loaded) {
    UnloadFont(ui_font);
  }
  if (proper_time_rate_tube_ready) {
    UnloadModel(proper_time_rate_tube_model);
  }
  UnloadShader(lighting_shader);
  UnloadRenderTexture(scene_target);

  CloseWindow();
  return 0;
}
