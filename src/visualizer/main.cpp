#include "relativity/simulation.hpp"

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kMaximumLights = 4;

struct ShaderLight {
  int type {};
  int enabled {};
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

constexpr const char* kCockpitVertexShader = R"glsl(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)glsl";

constexpr const char* kCockpitFragmentShader = R"glsl(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 resolution;
uniform float beta;
uniform float gamma;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float fovyRadians;
uniform float aspectRatio;

const float PI = 3.14159265359;

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 SafePerpendicular(vec3 axis)
{
    vec3 basis = abs(axis.y) < 0.98 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(axis, basis));
}

vec3 InverseAberration(vec3 observedDir)
{
    vec3 velocityDir = normalize(cameraForward);
    float cosObserved = clamp(dot(observedDir, velocityDir), -1.0, 1.0);
    vec3 perpendicular = observedDir - velocityDir * cosObserved;
    float perpendicularLength = length(perpendicular);
    vec3 perpendicularDir = perpendicularLength > 1e-5 ? perpendicular / perpendicularLength : SafePerpendicular(velocityDir);
    float denominator = max(1.0 - beta * cosObserved, 1e-5);
    float cosWorld = clamp((cosObserved - beta) / denominator, -1.0, 1.0);
    float sinWorld = sqrt(max(0.0, 1.0 - cosWorld * cosWorld));
    return normalize((velocityDir * cosWorld) + (perpendicularDir * sinWorld));
}

vec3 DopplerTint(float factor, float seed)
{
    float shift = clamp(0.5 + 0.28 * log2(max(factor, 0.125)), 0.0, 1.0);
    vec3 warm = vec3(1.0, 0.54, 0.30);
    vec3 cool = vec3(0.57, 0.72, 1.0);
    vec3 base = mix(vec3(1.0, 0.90, 0.82), vec3(0.78, 0.88, 1.0), seed);
    return mix(warm, cool, shift) * base;
}

float StarLayer(vec3 worldDir, vec2 scale, float threshold, float radiusScale, out float seed)
{
    float azimuth = atan(worldDir.z, worldDir.x) / (2.0 * PI) + 0.5;
    float polar = acos(clamp(worldDir.y, -1.0, 1.0)) / PI;
    vec2 grid = vec2(azimuth, polar) * scale;
    vec2 cell = floor(grid);
    vec2 local = fract(grid) - 0.5;
    seed = Hash12(cell);
    vec2 offset = vec2(Hash12(cell + 7.2), Hash12(cell + 19.4)) - 0.5;
    float distanceToStar = length(local - offset * 0.62);
    float sparkle = smoothstep(0.055 * radiusScale, 0.0, distanceToStar);
    float star = seed > threshold ? sparkle * (0.35 + seed * 0.9) : 0.0;
    if (seed > threshold + 0.01) {
        star += smoothstep(0.14 * radiusScale, 0.0, distanceToStar) * 0.12;
    }
    return star;
}

float Noise21(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);

    float a = Hash12(cell);
    float b = Hash12(cell + vec2(1.0, 0.0));
    float c = Hash12(cell + vec2(0.0, 1.0));
    float d = Hash12(cell + vec2(1.0, 1.0));

    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

float FractalNoise(vec2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave)
    {
        sum += Noise21(p) * amplitude;
        p = p * 2.03 + vec2(13.7, 8.1);
        amplitude *= 0.5;
    }
    return sum;
}

void main()
{
    vec2 uv = fragTexCoord * 2.0 - 1.0;
    uv.x *= aspectRatio;
    float focalScale = tan(fovyRadians * 0.5);
    vec3 observedDir = normalize(cameraForward + (uv.x * focalScale * cameraRight) + (uv.y * focalScale * cameraUp));
    vec3 worldDir = InverseAberration(observedDir);

    float cosTheta = clamp(dot(worldDir, normalize(cameraForward)), -1.0, 1.0);
    float doppler = 1.0 / max(gamma * (1.0 - beta * cosTheta), 1e-4);
    float beaming = min(pow(doppler, 4.0), 15.0);

    float seedA = 0.0;
    float seedB = 0.0;
    float seedC = 0.0;
    float starA = StarLayer(worldDir, vec2(150.0, 84.0), 0.982, 1.10, seedA);
    float starB = StarLayer(worldDir, vec2(260.0, 144.0), 0.991, 0.80, seedB);
    float starC = StarLayer(worldDir, vec2(430.0, 240.0), 0.9975, 0.56, seedC);
    float starIntensity = (starA * 0.95 + starB * 0.75 + starC * 0.42) * (0.55 + 0.45 * beaming);

    float azimuth = atan(worldDir.z, worldDir.x) / (2.0 * PI) + 0.5;
    float polar = acos(clamp(worldDir.y, -1.0, 1.0)) / PI;
    vec2 skyUv = vec2(azimuth, polar);
    vec3 galacticAxis = normalize(vec3(0.32, 0.87, -0.37));
    float galacticBand = pow(max(0.0, 1.0 - abs(dot(worldDir, galacticAxis))), 6.0);
    float galacticNoise = FractalNoise(skyUv * vec2(8.0, 18.0));
    float dustNoise = FractalNoise((skyUv + vec2(0.13, 0.27)) * vec2(22.0, 34.0));
    float horizonGlow = pow(max(0.0, 1.0 - abs(worldDir.y)), 5.0) * 0.060;
    float forwardGlow = pow(max(0.0, cosTheta), 7.0) * 0.12 * beaming;
    vec3 background = vec3(0.018, 0.022, 0.031)
                    + horizonGlow * vec3(0.10, 0.11, 0.14)
                    + forwardGlow * vec3(0.24, 0.26, 0.34);
    vec3 galacticColor = vec3(0.12, 0.14, 0.18) * galacticBand * (0.30 + 0.70 * galacticNoise);
    vec3 dustColor = vec3(0.05, 0.06, 0.08) * galacticBand * smoothstep(0.52, 0.88, dustNoise) * 0.55;

    vec3 starColor = starA * DopplerTint(doppler, seedA)
                   + starB * DopplerTint(doppler * 0.96, seedB)
                   + starC * DopplerTint(doppler * 1.03, seedC);
    vec3 color = background + galacticColor + dustColor + starColor * starIntensity;

    finalColor = vec4(clamp(color, 0.0, 1.0), 1.0) * fragColor;
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

enum class ViewMode {
  ThirdPerson,
  Cockpit,
};

struct PulseMarker {
  double time_years {};
  float path_fraction {};
  float proper_time_rate_radius {};
};

struct AppColors {
  Color app_background {};
  Color viewport_background {};
  Color panel_edge {};
  Color grid {};
  Color text {};
  Color muted {};
  Color structure {};
  Color earth_frame {};
  Color traveler_frame {};
  Color earth_body {};
  Color destination_body {};
  Color warning {};
};

struct UiLayout {
  Rectangle viewport {};
  Rectangle controls {};
  Rectangle play_button {};
  Rectangle reset_button {};
  Rectangle slider_track {};
  Rectangle mission_panel {};
  Rectangle frames_panel {};
  Rectangle simultaneity_panel {};
  Rectangle solar_context_panel {};
};

struct SceneLabels {
  Vector2 earth {};
  Vector2 destination {};
  Vector2 turnaround {};
};

struct SolarSystemBody {
  std::string name;
  int horizons_id {};
  Vector3 heliocentric_position_au {};
  float display_radius {};
  Color color {};
};

struct SolarSystemContext {
  std::string source_label;
  std::string epoch_label;
  std::vector<SolarSystemBody> bodies;
  Vector3 earth_heliocentric_position_au {};
  bool ready {false};
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

std::string Trim(const std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string(text.substr(begin, (end - begin) + 1));
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool in_quotes = false;

  for (char character : line) {
    if (character == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (character == ',' && !in_quotes) {
      fields.push_back(Trim(current));
      current.clear();
      continue;
    }
    current.push_back(character);
  }

  fields.push_back(Trim(current));
  return fields;
}

std::optional<std::filesystem::path> FindDataFile(std::string_view relative_path) {
  const std::filesystem::path current = std::filesystem::current_path();
  const std::filesystem::path executable = std::filesystem::path(GetApplicationDirectory());
  const std::array<std::filesystem::path, 6> candidates {
      current / std::string(relative_path),
      current / ".." / std::string(relative_path),
      current / ".." / ".." / std::string(relative_path),
      executable / std::string(relative_path),
      executable / ".." / std::string(relative_path),
      executable / ".." / ".." / std::string(relative_path),
  };

  for (const auto& candidate : candidates) {
    std::error_code error;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, error);
    const std::filesystem::path resolved = error ? candidate : normalized;
    if (std::filesystem::exists(resolved)) {
      return resolved;
    }
  }

  return std::nullopt;
}

float SolarBodyRadius(std::string_view name) {
  if (name == "Sun") {
    return 0.34F;
  }
  if (name == "Jupiter") {
    return 0.20F;
  }
  if (name == "Saturn") {
    return 0.18F;
  }
  if (name == "Uranus" || name == "Neptune") {
    return 0.15F;
  }
  if (name == "Earth" || name == "Venus") {
    return 0.13F;
  }
  if (name == "Mercury" || name == "Mars") {
    return 0.10F;
  }
  return 0.11F;
}

Color SolarBodyColor(std::string_view name) {
  if (name == "Sun") {
    return {236, 216, 150, 255};
  }
  if (name == "Mercury") {
    return {164, 154, 144, 255};
  }
  if (name == "Venus") {
    return {198, 171, 120, 255};
  }
  if (name == "Earth") {
    return {120, 150, 192, 255};
  }
  if (name == "Mars") {
    return {194, 118, 92, 255};
  }
  if (name == "Jupiter") {
    return {196, 176, 136, 255};
  }
  if (name == "Saturn") {
    return {208, 192, 142, 255};
  }
  if (name == "Uranus") {
    return {134, 186, 196, 255};
  }
  if (name == "Neptune") {
    return {106, 132, 214, 255};
  }
  return {180, 180, 180, 255};
}

SolarSystemContext LoadSolarSystemContext(std::string_view relative_path) {
  SolarSystemContext context {
      .source_label = "NASA/JPL Horizons",
      .epoch_label = {},
      .bodies = {},
      .earth_heliocentric_position_au = {},
      .ready = false,
  };

  const auto dataset_path = FindDataFile(relative_path);
  if (!dataset_path.has_value()) {
    TraceLog(LOG_WARNING, "solar-system dataset not found: %s", std::string(relative_path).c_str());
    return context;
  }

  std::ifstream input(*dataset_path);
  if (!input.is_open()) {
    TraceLog(LOG_WARNING, "failed to open solar-system dataset: %s", dataset_path->string().c_str());
    return context;
  }

  std::string line;
  bool header_seen = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    if (!header_seen) {
      header_seen = true;
      continue;
    }

    const std::vector<std::string> columns = SplitCsvLine(line);
    if (columns.size() != 6) {
      continue;
    }

    SolarSystemBody body {
        .name = columns[0],
        .horizons_id = static_cast<int>(ParseSize(columns[1], "horizons_id")),
        .heliocentric_position_au =
            {
                static_cast<float>(ParseDouble(columns[3], "x_au")),
                static_cast<float>(ParseDouble(columns[4], "y_au")),
                static_cast<float>(ParseDouble(columns[5], "z_au")),
            },
        .display_radius = SolarBodyRadius(columns[0]),
        .color = SolarBodyColor(columns[0]),
    };

    if (context.epoch_label.empty()) {
      context.epoch_label = columns[2];
    }
    if (body.name == "Earth") {
      context.earth_heliocentric_position_au = body.heliocentric_position_au;
    }

    context.bodies.push_back(body);
  }

  context.ready = !context.bodies.empty();
  if (!context.ready) {
    TraceLog(LOG_WARNING, "solar-system dataset parsed but had no body rows: %s", dataset_path->string().c_str());
  }

  return context;
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

std::string FormatSignedFixed(double value, int precision = 3) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(precision);
  out << std::showpos << value;
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

float SmoothStep01(float value) {
  const float x = Clamp01(value);
  return x * x * (3.0F - (2.0F * x));
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

void DrawPanel(const Rectangle& bounds, Color fill, Color outline) {
  DrawRectangleRounded(bounds, 0.12F, 10, fill);
  DrawRectangleRoundedLinesEx(bounds, 0.12F, 10, 1.0F, outline);
}

bool ContainsRect(const Rectangle& outer, const Rectangle& inner) {
  return inner.x >= outer.x && inner.y >= outer.y && (inner.x + inner.width) <= (outer.x + outer.width)
         && (inner.y + inner.height) <= (outer.y + outer.height);
}

bool IntersectsAny(const Rectangle& candidate, std::span<const Rectangle> blockers) {
  for (const Rectangle& blocker : blockers) {
    if (CheckCollisionRecs(candidate, blocker)) {
      return true;
    }
  }
  return false;
}

Vector3 CompressSolarSystemOffset(Vector3 delta_au) {
  const float magnitude = std::sqrt((delta_au.x * delta_au.x) + (delta_au.y * delta_au.y) + (delta_au.z * delta_au.z));
  if (magnitude <= 1.0e-4F) {
    return {0.0F, 0.0F, 0.0F};
  }

  constexpr float kCompressedUnitsPerLogAu = 1.62F;
  const float compressed_radius = static_cast<float>(std::log1p(magnitude)) * kCompressedUnitsPerLogAu;
  return {
      (delta_au.x / magnitude) * compressed_radius,
      (delta_au.y / magnitude) * compressed_radius,
      (delta_au.z / magnitude) * compressed_radius,
  };
}

void DrawButton(const Font& font, const Rectangle& bounds, const char* label, bool active, bool hover, Color fill,
                Color outline, Color text_color) {
  const Color background = active ? fill : (hover ? Fade(fill, 0.84F) : Fade(fill, 0.58F));
  DrawRectangleRounded(bounds, 0.18F, 10, background);
  DrawRectangleRoundedLinesEx(bounds, 0.18F, 10, 1.0F, outline);

  constexpr float kFontSize = 18.0F;
  const float text_width = MeasureUiText(font, label, kFontSize);
  DrawUiText(font, label, bounds.x + ((bounds.width - text_width) * 0.5F),
             bounds.y + ((bounds.height - kFontSize) * 0.5F), kFontSize, text_color);
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

Vector3 RotateAroundAxis(Vector3 value, Vector3 axis, float angle_radians) {
  const Vector3 normalized_axis = Normalize(axis);
  const float cosine = std::cos(angle_radians);
  const float sine = std::sin(angle_radians);
  return Add(Add(Scale(value, cosine), Scale(Cross(normalized_axis, value), sine)),
             Scale(normalized_axis, Dot(normalized_axis, value) * (1.0F - cosine)));
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
  light.enabled = 1;
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

const char* DynamicsLabel(double signed_proper_acceleration) {
  return signed_proper_acceleration < 0.0 ? "proper deceleration" : "proper acceleration";
}

const char* PhaseLabel(double progress) {
  if (progress < 0.04) {
    return "departure";
  }
  if (progress >= 0.485 && progress <= 0.535) {
    return "turnaround";
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

Vector3 ShipBodyForward(const PathFrame& frame, double progress) {
  constexpr float kFlipStartProgress = 0.465F;
  constexpr float kFlipEndProgress = 0.535F;
  const float flip_amount =
      SmoothStep01((static_cast<float>(progress) - kFlipStartProgress) / (kFlipEndProgress - kFlipStartProgress));
  return Normalize(RotateAroundAxis(frame.tangent, frame.up, PI * flip_amount));
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

double DelayedObservationTimeYears(const relativity::WorldlineSample& sample) {
  return sample.coordinate_time_years - sample.position_ly;
}

void DrawMissionSummaryCard(const Font& ui_font, const relativity::MissionResult& mission, const std::string& mission_name,
                            const std::string& destination_name, double coordinate_pulse_interval,
                            double proper_pulse_interval, bool cosmology_note, const AppColors& colors) {
  const Rectangle card {36.0F, 36.0F, 952.0F, 568.0F};
  DrawRectangleGradientV(0, 0, 1024, 640, colors.app_background, colors.viewport_background);
  DrawPanel(card, Color {19, 23, 29, 236}, Fade(colors.panel_edge, 0.80F));

  DrawUiText(ui_font, "Relativity Mission Summary", card.x + 28.0F, card.y + 24.0F, 34.0F, colors.text);
  DrawUiText(ui_font, mission_name, card.x + 30.0F, card.y + 72.0F, 22.0F, colors.traveler_frame);
  DrawUiText(ui_font, destination_name, card.x + 30.0F, card.y + 104.0F, 20.0F, colors.muted);

  const float metrics_x = card.x + 30.0F;
  const float metrics_y = card.y + 154.0F;
  const float metrics_width = 388.0F;

  DrawSummaryLine(ui_font, "distance", FormatFixed(mission.profile.distance_ly, 2) + " ly", metrics_x, metrics_y,
                  metrics_width, colors.muted, colors.text);
  DrawSummaryLine(ui_font, "peak beta", FormatFixed(mission.summary.peak_beta, 3), metrics_x, metrics_y + 38.0F,
                  metrics_width, colors.muted, colors.text);
  DrawSummaryLine(ui_font, "peak Lorentz factor", FormatFixed(mission.summary.gamma, 4), metrics_x, metrics_y + 76.0F,
                  metrics_width, colors.muted, colors.text);
  DrawSummaryLine(ui_font, "proper acceleration",
                  FormatFixed(mission.summary.proper_acceleration_ly_per_year2, 3) + " ly / y^2", metrics_x,
                  metrics_y + 114.0F, metrics_width, colors.muted, colors.text);
  DrawSummaryLine(ui_font, "coordinate time", FormatFixed(mission.summary.coordinate_time_years, 3) + " y",
                  metrics_x, metrics_y + 174.0F, metrics_width, colors.earth_frame, colors.earth_frame);
  DrawSummaryLine(ui_font, "proper time", FormatFixed(mission.summary.proper_time_years, 3) + " y", metrics_x,
                  metrics_y + 212.0F, metrics_width, colors.traveler_frame, colors.traveler_frame);
  DrawSummaryLine(ui_font, "elapsed difference", FormatFixed(mission.summary.elapsed_difference_years, 3) + " y",
                  metrics_x, metrics_y + 250.0F, metrics_width, colors.traveler_frame, colors.traveler_frame);
  DrawSummaryLine(ui_font, "arrival signal to Earth",
                  FormatFixed(mission.summary.arrival_signal_to_earth_years, 3) + " y", metrics_x, metrics_y + 288.0F,
                  metrics_width, colors.earth_frame, colors.earth_frame);

  const Rectangle frame_box {card.x + 500.0F, card.y + 150.0F, 420.0F, 266.0F};
  DrawPanel(frame_box, Color {14, 17, 21, 220}, Fade(colors.panel_edge, 0.65F));
  DrawUiText(ui_font, "Frame Comparison", frame_box.x + 22.0F, frame_box.y + 18.0F, 24.0F, colors.text);
  DrawUiText(ui_font, "Earth frame", frame_box.x + 52.0F, frame_box.y + 62.0F, 20.0F, colors.earth_frame);
  DrawUiText(ui_font, "Traveler frame", frame_box.x + 232.0F, frame_box.y + 62.0F, 20.0F, colors.traveler_frame);

  const Rectangle earth_rail {frame_box.x + 70.0F, frame_box.y + 102.0F, 48.0F, 118.0F};
  const Rectangle traveler_rail {frame_box.x + 274.0F, frame_box.y + 102.0F, 48.0F, 118.0F};
  DrawRectangleRounded(earth_rail, 0.2F, 8, Fade(colors.earth_frame, 0.08F));
  DrawRectangleRoundedLinesEx(earth_rail, 0.2F, 8, 1.0F, Fade(colors.earth_frame, 0.45F));
  DrawRectangleRounded({earth_rail.x, earth_rail.y, earth_rail.width, earth_rail.height}, 0.2F, 8,
                       Fade(colors.earth_frame, 0.30F));

  const float traveler_fill = static_cast<float>(mission.summary.proper_time_years / mission.summary.coordinate_time_years);
  DrawRectangleRounded(traveler_rail, 0.2F, 8, Fade(colors.traveler_frame, 0.08F));
  DrawRectangleRoundedLinesEx(traveler_rail, 0.2F, 8, 1.0F, Fade(colors.traveler_frame, 0.48F));
  DrawRectangleRounded(
      {traveler_rail.x, traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_fill),
       traveler_rail.width, traveler_rail.height * traveler_fill},
      0.2F, 8, Fade(colors.traveler_frame, 0.34F));

  DrawUiText(ui_font, FormatFixed(mission.summary.coordinate_time_years, 2) + " y", earth_rail.x - 14.0F,
             earth_rail.y + 136.0F, 18.0F, colors.earth_frame);
  DrawUiText(ui_font, FormatFixed(mission.summary.proper_time_years, 2) + " y", traveler_rail.x - 14.0F,
             traveler_rail.y + 136.0F, 18.0F, colors.traveler_frame);

  DrawUiText(ui_font, "proper pulse interval", frame_box.x + 22.0F, frame_box.y + 234.0F, 18.0F, colors.muted);
  DrawUiText(ui_font, FormatFixed(proper_pulse_interval, proper_pulse_interval < 1.0 ? 2 : 1) + " y",
             frame_box.x + 286.0F, frame_box.y + 234.0F, 18.0F, colors.traveler_frame);
  DrawUiText(ui_font, "coordinate pulse interval", frame_box.x + 22.0F, frame_box.y + 262.0F, 18.0F, colors.muted);
  DrawUiText(ui_font, FormatFixed(coordinate_pulse_interval, coordinate_pulse_interval < 1.0 ? 2 : 1) + " y",
             frame_box.x + 286.0F, frame_box.y + 262.0F, 18.0F, colors.earth_frame);

  DrawUiText(ui_font,
             cosmology_note ? "Special relativity only. Cosmological expansion is not included at this distance."
                            : "Special relativity only. Earth and destination are stationary in one inertial frame.",
             card.x + 30.0F, card.y + 500.0F, 18.0F, cosmology_note ? colors.warning : colors.muted);
}

bool ExportMissionArtifacts(std::string_view export_prefix, const Font& ui_font, RenderTexture2D scene_target,
                            const relativity::MissionResult& mission, const std::string& mission_name,
                            const std::string& destination_name, double coordinate_pulse_interval,
                            double proper_pulse_interval, bool cosmology_note, const AppColors& colors) {
  if (export_prefix.empty()) {
    return false;
  }

  const std::string visual_path = ExportImagePath(export_prefix, "visual");
  const std::string summary_path = ExportImagePath(export_prefix, "summary");

  RenderTexture2D summary_target = LoadRenderTexture(1024, 640);
  SetTextureFilter(summary_target.texture, TEXTURE_FILTER_BILINEAR);

  BeginTextureMode(summary_target);
  ClearBackground(colors.app_background);
  DrawMissionSummaryCard(ui_font, mission, mission_name, destination_name, coordinate_pulse_interval, proper_pulse_interval,
                         cosmology_note, colors);
  EndTextureMode();

  const bool visual_saved = ExportRenderTexturePng(scene_target, visual_path);
  const bool summary_saved = ExportRenderTexturePng(summary_target, summary_path);
  UnloadRenderTexture(summary_target);

  TraceLog(summary_saved && visual_saved ? LOG_INFO : LOG_WARNING, "mission export summary=%s visual=%s",
           summary_saved ? summary_path.c_str() : "failed", visual_saved ? visual_path.c_str() : "failed");

  return summary_saved && visual_saved;
}

class RelativisticCamera {
 public:
  enum class ThirdPersonPreset {
    Isometric,
    Broadside,
    Forward,
    Overhead,
  };

  RelativisticCamera() {
    ResetView();
  }

  void ToggleMode() {
    mode_ = mode_ == ViewMode::ThirdPerson ? ViewMode::Cockpit : ViewMode::ThirdPerson;
  }

  void SnapToThirdPersonPreset(ThirdPersonPreset preset) {
    switch (preset) {
      case ThirdPersonPreset::Isometric:
        third_person_distance_ = 24.0F;
        third_person_yaw_ = -0.42F;
        third_person_pitch_ = 0.24F;
        break;
      case ThirdPersonPreset::Broadside:
        third_person_distance_ = 20.5F;
        third_person_yaw_ = -1.56F;
        third_person_pitch_ = 0.20F;
        break;
      case ThirdPersonPreset::Forward:
        third_person_distance_ = 18.0F;
        third_person_yaw_ = 0.88F;
        third_person_pitch_ = 0.12F;
        break;
      case ThirdPersonPreset::Overhead:
        third_person_distance_ = 26.0F;
        third_person_yaw_ = -0.42F;
        third_person_pitch_ = 1.04F;
        break;
    }
    CommitThirdPersonCamera();
  }

  void ResetView() {
    third_person_target_ = {0.0F, 0.15F, 0.0F};
    third_person_camera_.target = third_person_target_;
    third_person_camera_.up = {0.0F, 1.0F, 0.0F};
    third_person_camera_.fovy = 38.0F;
    third_person_camera_.projection = CAMERA_PERSPECTIVE;
    cockpit_look_yaw_ = 0.0F;
    cockpit_look_pitch_ = 0.0F;
    cockpit_camera_.up = {0.0F, 1.0F, 0.0F};
    cockpit_camera_.fovy = 72.0F;
    cockpit_camera_.projection = CAMERA_PERSPECTIVE;
    SnapToThirdPersonPreset(ThirdPersonPreset::Isometric);
  }

  void HandleViewportInput(bool mouse_in_viewport, Vector2 mouse_delta, float wheel, float dt) {
    if (mode_ == ViewMode::ThirdPerson) {
      HandleThirdPersonInput(mouse_in_viewport, mouse_delta, wheel, dt);
      return;
    }

    HandleCockpitInput(mouse_in_viewport, mouse_delta, dt);
  }

  void UpdateFromShip(const PathFrame& ship_frame) {
    const Vector3 cockpit_eye =
        Add(ship_frame.center, Add(Scale(ship_frame.up, 0.22F), Scale(ship_frame.tangent, 0.08F)));
    const Vector3 yaw_adjusted_forward = RotateAroundAxis(ship_frame.tangent, ship_frame.up, cockpit_look_yaw_);
    const Vector3 pitch_axis = Normalize(Cross(ship_frame.up, yaw_adjusted_forward));
    const Vector3 cockpit_forward = Normalize(RotateAroundAxis(yaw_adjusted_forward, pitch_axis, cockpit_look_pitch_));
    const Vector3 cockpit_up = Normalize(Cross(cockpit_forward, pitch_axis));
    cockpit_camera_.position = cockpit_eye;
    cockpit_camera_.target = Add(cockpit_eye, Scale(cockpit_forward, 12.0F));
    cockpit_camera_.up = cockpit_up;
    CommitThirdPersonCamera();
  }

  const Camera3D& ActiveCamera() const {
    return mode_ == ViewMode::Cockpit ? cockpit_camera_ : third_person_camera_;
  }

  bool IsCockpitMode() const {
    return mode_ == ViewMode::Cockpit;
  }

  std::string_view ModeLabel() const {
    return mode_ == ViewMode::Cockpit ? "Traveler View" : "Third-Person View";
  }

 private:
  void HandleThirdPersonInput(bool mouse_in_viewport, Vector2 mouse_delta, float wheel, float dt) {
    if (wheel != 0.0F) {
      third_person_distance_ = std::clamp(third_person_distance_ - (wheel * 1.15F), 7.5F, 34.0F);
    }

    if (mouse_in_viewport && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      third_person_yaw_ -= mouse_delta.x * 0.0065F;
      third_person_pitch_ = std::clamp(third_person_pitch_ - (mouse_delta.y * 0.0045F), -0.25F, 1.10F);
    }

    if (mouse_in_viewport && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
      const Vector3 forward = Normalize(Subtract(third_person_camera_.target, third_person_camera_.position));
      const Vector3 right = Normalize(Cross(third_person_camera_.up, forward));
      const Vector3 pan_offset =
          Add(Scale(right, -mouse_delta.x * 0.018F), Scale(third_person_camera_.up, mouse_delta.y * 0.018F));
      third_person_target_ = Add(third_person_target_, pan_offset);
    }

    const float keyboard_pan_speed = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? 8.0F : 4.5F;
    const Vector3 forward = Normalize(Subtract(third_person_camera_.target, third_person_camera_.position));
    const Vector3 planar_forward = Normalize({forward.x, 0.0F, forward.z});
    const Vector3 right = Normalize(Cross(third_person_camera_.up, forward));
    const float move_scale = keyboard_pan_speed * dt;

    if (IsKeyDown(KEY_W)) {
      third_person_target_ = Add(third_person_target_, Scale(planar_forward, move_scale));
    }
    if (IsKeyDown(KEY_S)) {
      third_person_target_ = Add(third_person_target_, Scale(planar_forward, -move_scale));
    }
    if (IsKeyDown(KEY_A)) {
      third_person_target_ = Add(third_person_target_, Scale(right, move_scale));
    }
    if (IsKeyDown(KEY_D)) {
      third_person_target_ = Add(third_person_target_, Scale(right, -move_scale));
    }
    if (IsKeyDown(KEY_Q)) {
      third_person_target_ = Add(third_person_target_, Scale(third_person_camera_.up, move_scale));
    }
    if (IsKeyDown(KEY_Z)) {
      third_person_target_ = Add(third_person_target_, Scale(third_person_camera_.up, -move_scale));
    }

    CommitThirdPersonCamera();
  }

  void HandleCockpitInput(bool mouse_in_viewport, Vector2 mouse_delta, float dt) {
    if (mouse_in_viewport && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      cockpit_look_yaw_ -= mouse_delta.x * 0.0048F;
      cockpit_look_pitch_ = std::clamp(cockpit_look_pitch_ - (mouse_delta.y * 0.0038F), -1.20F, 1.20F);
    }

    const float keyboard_look_speed = 1.45F * dt;
    if (IsKeyDown(KEY_J)) {
      cockpit_look_yaw_ += keyboard_look_speed;
    }
    if (IsKeyDown(KEY_L)) {
      cockpit_look_yaw_ -= keyboard_look_speed;
    }
    if (IsKeyDown(KEY_I)) {
      cockpit_look_pitch_ = std::clamp(cockpit_look_pitch_ + keyboard_look_speed, -1.20F, 1.20F);
    }
    if (IsKeyDown(KEY_K)) {
      cockpit_look_pitch_ = std::clamp(cockpit_look_pitch_ - keyboard_look_speed, -1.20F, 1.20F);
    }
  }

  void CommitThirdPersonCamera() {
    third_person_camera_.position =
        CameraPositionFromOrbit(third_person_target_, third_person_distance_, third_person_yaw_, third_person_pitch_);
    third_person_camera_.target = third_person_target_;
  }

  ViewMode mode_ {ViewMode::ThirdPerson};
  Camera3D third_person_camera_ {};
  Camera3D cockpit_camera_ {};
  Vector3 third_person_target_ {};
  float third_person_distance_ {};
  float third_person_yaw_ {};
  float third_person_pitch_ {};
  float cockpit_look_yaw_ {};
  float cockpit_look_pitch_ {};
};

class MissionScene {
 public:
  MissionScene(const relativity::MissionResult& mission, const Rectangle& viewport, const SolarSystemContext& solar_system,
               const AppColors& colors)
      : mission_(mission),
        viewport_(viewport),
        solar_system_(solar_system),
        colors_(colors),
        scene_target_(LoadRenderTexture(static_cast<int>(viewport.width), static_cast<int>(viewport.height))),
        lighting_shader_(LoadShaderFromMemory(kLightingVertexShader, kLightingFragmentShader)),
        cockpit_shader_(LoadShaderFromMemory(kCockpitVertexShader, kCockpitFragmentShader)),
        earth_world_ {-10.5F, 2.2F, -4.8F},
        turnaround_world_ {0.0F, 0.8F, 0.0F},
        destination_world_ {10.5F, -1.9F, 4.8F},
        turnaround_curve_world_ {QuadraticBezierPoint(earth_world_, turnaround_world_, destination_world_, 0.5F)} {
    SetTextureFilter(scene_target_.texture, TEXTURE_FILTER_BILINEAR);

    ambient_location_ = GetShaderLocation(lighting_shader_, "ambient");
    view_position_location_ = GetShaderLocation(lighting_shader_, "viewPos");
    const float ambient_value[4] {0.22F, 0.24F, 0.28F, 1.0F};
    SetShaderValue(lighting_shader_, ambient_location_, ambient_value, SHADER_UNIFORM_VEC4);

    cockpit_resolution_location_ = GetShaderLocation(cockpit_shader_, "resolution");
    cockpit_beta_location_ = GetShaderLocation(cockpit_shader_, "beta");
    cockpit_gamma_location_ = GetShaderLocation(cockpit_shader_, "gamma");
    cockpit_forward_location_ = GetShaderLocation(cockpit_shader_, "cameraForward");
    cockpit_right_location_ = GetShaderLocation(cockpit_shader_, "cameraRight");
    cockpit_up_location_ = GetShaderLocation(cockpit_shader_, "cameraUp");
    cockpit_fovy_location_ = GetShaderLocation(cockpit_shader_, "fovyRadians");
    cockpit_aspect_location_ = GetShaderLocation(cockpit_shader_, "aspectRatio");

    lights_.push_back(CreateShaderLight(0, 0, {-8.5F, 11.0F, 10.0F}, {0.0F, 0.0F, 0.0F},
                                        Color {240, 244, 255, 255}, lighting_shader_));
    lights_.push_back(CreateShaderLight(1, 1, {8.5F, 4.5F, 5.5F}, {0.0F, 0.0F, 0.0F},
                                        Color {196, 212, 255, 255}, lighting_shader_));
    lights_.push_back(CreateShaderLight(2, 1, {-2.5F, 2.0F, -8.0F}, {0.0F, 0.0F, 0.0F},
                                        Color {198, 186, 170, 255}, lighting_shader_));
  }

  ~MissionScene() {
    if (proper_time_rate_tube_ready_) {
      UnloadModel(proper_time_rate_tube_model_);
    }
    UnloadShader(cockpit_shader_);
    UnloadShader(lighting_shader_);
    UnloadRenderTexture(scene_target_);
  }

  PathFrame ShipFrame(const relativity::WorldlineSample& current) const {
    const float travel_fraction = static_cast<float>(current.position_ly / mission_.profile.distance_ly);
    return EvaluatePathFrame(earth_world_, turnaround_world_, destination_world_, travel_fraction,
                             ProperTimeRateTubeRadius(current.proper_time_rate));
  }

  SceneLabels ProjectLabels(const Camera3D& camera) const {
    return {
        .earth = GetWorldToScreenEx(earth_world_, camera, static_cast<int>(viewport_.width),
                                    static_cast<int>(viewport_.height)),
        .destination = GetWorldToScreenEx(destination_world_, camera, static_cast<int>(viewport_.width),
                                          static_cast<int>(viewport_.height)),
        .turnaround = GetWorldToScreenEx(turnaround_curve_world_, camera, static_cast<int>(viewport_.width),
                                         static_cast<int>(viewport_.height)),
    };
  }

  void Render(const RelativisticCamera& camera, const relativity::WorldlineSample& current, std::size_t current_index,
              double coordinate_pulse_interval, double proper_pulse_interval) {
    BeginTextureMode(scene_target_);
    ClearBackground(colors_.viewport_background);

    if (camera.IsCockpitMode()) {
      RenderCockpit(camera.ActiveCamera(), current);
    } else {
      RenderThirdPerson(camera.ActiveCamera(), current, current_index, coordinate_pulse_interval, proper_pulse_interval);
    }

    EndTextureMode();
  }

  const RenderTexture2D& RenderTarget() const {
    return scene_target_;
  }

  const Vector3& EarthWorld() const {
    return earth_world_;
  }

  const Vector3& DestinationWorld() const {
    return destination_world_;
  }

  const Vector3& TurnaroundCurveWorld() const {
    return turnaround_curve_world_;
  }

 private:
  void DrawSolarSystemContext() const {
    if (!solar_system_.ready) {
      return;
    }

    for (const auto& body : solar_system_.bodies) {
      if (body.name == "Earth") {
        continue;
      }

      const Vector3 delta_au = Subtract(body.heliocentric_position_au, solar_system_.earth_heliocentric_position_au);
      const Vector3 body_world = Add(earth_world_, CompressSolarSystemOffset(delta_au));
      DrawLine3D(earth_world_, body_world, Fade(body.color, 0.18F));
      DrawSphereEx(body_world, body.display_radius, 14, 14, Fade(body.color, 0.96F));
      DrawSphereWires(body_world, body.display_radius, 10, 10, Fade(colors_.structure, 0.30F));

      if (body.name == "Saturn") {
        DrawCircle3D(body_world, body.display_radius * 1.85F, {0.1F, 1.0F, 0.18F}, 72.0F, Fade(body.color, 0.44F));
      }
    }

    DrawCircle3D(earth_world_, 6.0F, {0.0F, 1.0F, 0.0F}, 90.0F, Fade(colors_.earth_frame, 0.10F));
  }

  void DrawInertialReferenceGrid() const {
    constexpr int kHalfLineCount = 12;
    constexpr float kSpacing = 1.5F;
    constexpr float kExtent = static_cast<float>(kHalfLineCount) * kSpacing;

    for (int index = -kHalfLineCount; index <= kHalfLineCount; ++index) {
      const float offset = static_cast<float>(index) * kSpacing;
      const bool major_line = index % 4 == 0;
      const Color line_color = Fade(colors_.grid, major_line ? 0.24F : 0.10F);
      DrawLine3D({-kExtent, 0.0F, offset}, {kExtent, 0.0F, offset}, line_color);
      DrawLine3D({offset, 0.0F, -kExtent}, {offset, 0.0F, kExtent}, line_color);
    }

    DrawLine3D({-kExtent, 0.0F, 0.0F}, {kExtent, 0.0F, 0.0F}, Fade(colors_.earth_frame, 0.20F));
    DrawLine3D({0.0F, 0.0F, -kExtent}, {0.0F, 0.0F, kExtent}, Fade(colors_.earth_frame, 0.16F));
  }

  void DrawMissionReferenceFrame() const {
    const Vector3 baseline = Subtract(destination_world_, earth_world_);
    const Vector3 baseline_direction = Normalize(baseline);
    const Vector3 baseline_right = Normalize(Cross({0.0F, 1.0F, 0.0F}, baseline_direction));

    DrawLine3D(earth_world_, destination_world_, Fade(colors_.earth_frame, 0.26F));
    for (int tick = 0; tick <= 10; ++tick) {
      const float fraction = static_cast<float>(tick) / 10.0F;
      const PathFrame frame = EvaluatePathFrame(earth_world_, turnaround_world_, destination_world_, fraction, 1.0F);
      const float major_tick = tick % 5 == 0 ? 0.46F : 0.28F;
      const Color tick_color = tick == 5 ? Fade(colors_.traveler_frame, 0.54F) : Fade(colors_.earth_frame, 0.30F);
      DrawLine3D(Add(frame.center, Scale(frame.right, -major_tick)), Add(frame.center, Scale(frame.right, major_tick)),
                 tick_color);
      DrawLine3D(Add(frame.center, Scale(frame.up, -major_tick * 0.65F)),
                 Add(frame.center, Scale(frame.up, major_tick * 0.65F)), Fade(tick_color, 0.70F));
    }

    const PathFrame turnaround_frame =
        EvaluatePathFrame(earth_world_, turnaround_world_, destination_world_, 0.5F, 1.0F);
    DrawRingSegments(turnaround_frame, 2.25F, 0.014F, Fade(colors_.traveler_frame, 0.42F), 56);
    DrawLine3D(Add(turnaround_curve_world_, Scale(turnaround_frame.right, -2.5F)),
               Add(turnaround_curve_world_, Scale(turnaround_frame.right, 2.5F)), Fade(colors_.traveler_frame, 0.22F));
    DrawLine3D(Add(turnaround_curve_world_, Scale(turnaround_frame.up, -1.55F)),
               Add(turnaround_curve_world_, Scale(turnaround_frame.up, 1.55F)), Fade(colors_.traveler_frame, 0.22F));

    for (int offset = -6; offset <= 6; ++offset) {
      if (offset == 0) {
        continue;
      }
      const float line_offset = static_cast<float>(offset) * 1.5F;
      const Color line_color = Fade(colors_.grid, offset % 3 == 0 ? 0.18F : 0.10F);
      DrawLine3D(Add(earth_world_, Scale(baseline_right, line_offset)),
                 Add(destination_world_, Scale(baseline_right, line_offset)), line_color);
    }
  }

  void DrawShip(const PathFrame& ship_frame, const relativity::WorldlineSample& current) const {
    const Vector3 ship_world = ship_frame.center;
    const Vector3 ship_forward = ShipBodyForward(ship_frame, current.progress);
    const Vector3 ship_right = Normalize(Cross(ship_frame.up, ship_forward));
    const Vector3 ship_up = Normalize(Cross(ship_forward, ship_right));
    const bool decelerating_phase = current.signed_proper_acceleration_ly_per_year2 < 0.0;
    const float acceleration_direction_sign = decelerating_phase ? -1.0F : 1.0F;

    const Vector3 probe_tail = Add(ship_world, Scale(ship_forward, -0.70F));
    const Vector3 probe_nose = Add(ship_world, Scale(ship_forward, 0.78F));
    DrawCylinderEx(probe_tail, probe_nose, 0.17F, 0.055F, 14, Fade(colors_.text, 0.96F));
    DrawCylinderEx(Add(probe_tail, Scale(ship_forward, -0.18F)), probe_tail, 0.20F, 0.15F, 14,
                   Fade(colors_.structure, 0.88F));
    DrawSphereEx(ship_world, 0.13F, 10, 10, Fade(colors_.app_background, 0.92F));

    const Vector3 wing_center = Add(ship_world, Scale(ship_forward, -0.10F));
    DrawCylinderEx(Add(wing_center, Scale(ship_right, -0.42F)), Add(wing_center, Scale(ship_right, 0.42F)), 0.025F,
                   0.025F, 6, Fade(colors_.structure, 0.86F));
    DrawCylinderEx(Add(probe_tail, Scale(ship_up, -0.34F)), Add(probe_tail, Scale(ship_up, 0.34F)), 0.022F, 0.022F, 6,
                   Fade(colors_.structure, 0.70F));

    const Vector3 exhaust_end = Add(probe_tail, Scale(ship_forward, -0.54F - (0.46F * static_cast<float>(current.beta))));
    DrawCylinderEx(probe_tail, exhaust_end, 0.12F, 0.0F, 10, Fade(colors_.traveler_frame, 0.54F));

    const Vector3 acceleration_offset_start = Add(ship_world, {0.0F, 0.78F, 0.0F});
    const float acceleration_vector_length = 0.86F + (1.55F * static_cast<float>(current.beta));
    const Vector3 acceleration_direction = Scale(ship_frame.tangent, acceleration_direction_sign);
    const Vector3 acceleration_vector_end =
        Add(acceleration_offset_start, Scale(acceleration_direction, acceleration_vector_length));
    DrawCylinderEx(acceleration_offset_start, acceleration_vector_end, 0.03F, 0.03F, 10, colors_.traveler_frame);
    DrawCylinderEx(Add(acceleration_vector_end, Scale(acceleration_direction, -0.18F)), acceleration_vector_end, 0.10F,
                   0.0F, 10, colors_.traveler_frame);
  }

  void UpdateTubeModel(std::size_t current_index) {
    if (current_index <= 1) {
      return;
    }
    if (current_index == proper_time_rate_tube_index_) {
      return;
    }

    if (proper_time_rate_tube_ready_) {
      UnloadModel(proper_time_rate_tube_model_);
      proper_time_rate_tube_ready_ = false;
    }

    proper_time_rate_tube_model_ =
        BuildProperTimeRateTubeModel(mission_, current_index, earth_world_, turnaround_world_, destination_world_, 48);
    proper_time_rate_tube_model_.materials[0].shader = lighting_shader_;
    proper_time_rate_tube_ready_ = true;
    proper_time_rate_tube_index_ = current_index;
  }

  void RenderThirdPerson(const Camera3D& camera, const relativity::WorldlineSample& current, std::size_t current_index,
                         double coordinate_pulse_interval, double proper_pulse_interval) {
    UpdateTubeModel(current_index);

    const float view_position[3] {camera.position.x, camera.position.y, camera.position.z};
    SetShaderValue(lighting_shader_, view_position_location_, view_position, SHADER_UNIFORM_VEC3);
    for (const auto& light : lights_) {
      UpdateShaderLight(lighting_shader_, light);
    }

    const float travel_fraction = static_cast<float>(current.position_ly / mission_.profile.distance_ly);
    const PathFrame ship_frame = ShipFrame(current);
    const std::vector<PulseMarker> coordinate_pulses =
        BuildPulseMarkers(mission_, current_index, coordinate_pulse_interval, TimeDomain::Coordinate);
    const std::vector<PulseMarker> proper_pulses =
        BuildPulseMarkers(mission_, current_index, proper_pulse_interval, TimeDomain::Proper);

    BeginMode3D(camera);

    DrawInertialReferenceGrid();
    DrawMissionReferenceFrame();
    DrawSolarSystemContext();
    DrawTrajectoryCurve(earth_world_, turnaround_world_, destination_world_, 1.0F, 90, Fade(colors_.grid, 0.28F));
    DrawTrajectoryCurve(earth_world_, turnaround_world_, destination_world_, travel_fraction, 90,
                        Fade(colors_.structure, 0.32F));
    DrawSphereEx(earth_world_, 0.46F, 18, 18, colors_.earth_body);
    DrawSphereEx(destination_world_, 0.38F, 18, 18, colors_.destination_body);
    DrawSphereWires(earth_world_, 0.46F, 12, 12, Fade(colors_.structure, 0.65F));
    DrawSphereWires(destination_world_, 0.38F, 12, 12, Fade(colors_.structure, 0.52F));

    if (proper_time_rate_tube_ready_) {
      DrawModel(proper_time_rate_tube_model_, {0.0F, 0.0F, 0.0F}, 1.0F, Fade(colors_.traveler_frame, 0.26F));
      DrawModelWires(proper_time_rate_tube_model_, {0.0F, 0.0F, 0.0F}, 1.0F, Fade(colors_.traveler_frame, 0.54F));
    }

    for (const auto& marker : coordinate_pulses) {
      const PathFrame frame = EvaluatePathFrame(earth_world_, turnaround_world_, destination_world_, marker.path_fraction,
                                                marker.proper_time_rate_radius);
      const float radius = std::max(frame.proper_time_rate_radius + 0.30F, frame.proper_time_rate_radius * 1.18F);
      DrawRingSegments(frame, radius, 0.016F, Fade(colors_.earth_frame, 0.36F), 28);
    }

    for (const auto& marker : proper_pulses) {
      const PathFrame frame = EvaluatePathFrame(earth_world_, turnaround_world_, destination_world_, marker.path_fraction,
                                                marker.proper_time_rate_radius);
      DrawRingSegments(frame, frame.proper_time_rate_radius + 0.03F, 0.030F, Fade(colors_.traveler_frame, 0.70F), 28);
    }

    DrawShip(ship_frame, current);

    EndMode3D();
  }

  void RenderCockpit(const Camera3D& camera, const relativity::WorldlineSample& current) {
    const Vector2 resolution {viewport_.width, viewport_.height};
    const float beta = static_cast<float>(current.beta);
    const float gamma = static_cast<float>(current.gamma);
    const Vector3 forward = Normalize(Subtract(camera.target, camera.position));
    const Vector3 right = Normalize(Cross(camera.up, forward));
    const Vector3 up = Normalize(Cross(forward, right));
    const float fovy_radians = camera.fovy * DEG2RAD;
    const float aspect_ratio = viewport_.width / viewport_.height;
    const float forward_data[3] {forward.x, forward.y, forward.z};
    const float right_data[3] {right.x, right.y, right.z};
    const float up_data[3] {up.x, up.y, up.z};

    SetShaderValue(cockpit_shader_, cockpit_resolution_location_, &resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(cockpit_shader_, cockpit_beta_location_, &beta, SHADER_UNIFORM_FLOAT);
    SetShaderValue(cockpit_shader_, cockpit_gamma_location_, &gamma, SHADER_UNIFORM_FLOAT);
    SetShaderValue(cockpit_shader_, cockpit_forward_location_, forward_data, SHADER_UNIFORM_VEC3);
    SetShaderValue(cockpit_shader_, cockpit_right_location_, right_data, SHADER_UNIFORM_VEC3);
    SetShaderValue(cockpit_shader_, cockpit_up_location_, up_data, SHADER_UNIFORM_VEC3);
    SetShaderValue(cockpit_shader_, cockpit_fovy_location_, &fovy_radians, SHADER_UNIFORM_FLOAT);
    SetShaderValue(cockpit_shader_, cockpit_aspect_location_, &aspect_ratio, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(cockpit_shader_);
    DrawRectangleRec({0.0F, 0.0F, viewport_.width, viewport_.height}, WHITE);
    EndShaderMode();

    const Vector2 center {viewport_.width * 0.5F, viewport_.height * 0.5F};
    DrawRectangleGradientV(0, 0, static_cast<int>(viewport_.width), static_cast<int>(viewport_.height),
                           Fade(colors_.app_background, 0.00F), Fade(colors_.app_background, 0.18F));
    DrawCircleLinesV(center, 18.0F, Fade(colors_.traveler_frame, 0.65F));
    DrawLineEx({center.x - 30.0F, center.y}, {center.x - 8.0F, center.y}, 1.6F, Fade(colors_.traveler_frame, 0.55F));
    DrawLineEx({center.x + 8.0F, center.y}, {center.x + 30.0F, center.y}, 1.6F, Fade(colors_.traveler_frame, 0.55F));
    DrawLineEx({center.x, center.y - 30.0F}, {center.x, center.y - 8.0F}, 1.6F, Fade(colors_.traveler_frame, 0.55F));
    DrawLineEx({center.x, center.y + 8.0F}, {center.x, center.y + 30.0F}, 1.6F, Fade(colors_.traveler_frame, 0.55F));
    DrawUiText(GetFontDefault(), "Traveler View", 18.0F, 16.0F, 18.0F, Fade(colors_.traveler_frame, 0.82F));
    DrawUiText(GetFontDefault(), "Relativistic sky only", 18.0F, 38.0F, 12.0F, Fade(colors_.muted, 0.92F));
    DrawUiText(GetFontDefault(), "beta " + FormatFixed(current.beta, 3) + "   gamma " + FormatFixed(current.gamma, 3),
               viewport_.width - 226.0F, 18.0F, 13.0F, Fade(colors_.traveler_frame, 0.82F));
  }

 const relativity::MissionResult& mission_;
  Rectangle viewport_ {};
  SolarSystemContext solar_system_ {};
  AppColors colors_ {};
  RenderTexture2D scene_target_ {};
  Shader lighting_shader_ {};
  Shader cockpit_shader_ {};
  int ambient_location_ {-1};
  int view_position_location_ {-1};
  int cockpit_resolution_location_ {-1};
  int cockpit_beta_location_ {-1};
  int cockpit_gamma_location_ {-1};
  int cockpit_forward_location_ {-1};
  int cockpit_right_location_ {-1};
  int cockpit_up_location_ {-1};
  int cockpit_fovy_location_ {-1};
  int cockpit_aspect_location_ {-1};
  std::vector<ShaderLight> lights_ {};
  Model proper_time_rate_tube_model_ {};
  bool proper_time_rate_tube_ready_ {false};
  std::size_t proper_time_rate_tube_index_ {std::numeric_limits<std::size_t>::max()};
  Vector3 earth_world_ {};
  Vector3 turnaround_world_ {};
  Vector3 destination_world_ {};
  Vector3 turnaround_curve_world_ {};
};

class DashboardUI {
 public:
  DashboardUI(const Font& ui_font, const relativity::MissionResult& mission, const std::string& mission_name,
              const std::string& destination_name, double coordinate_pulse_interval, double proper_pulse_interval,
              bool cosmology_note, const SolarSystemContext& solar_system, const AppColors& colors)
      : ui_font_(ui_font),
        mission_(mission),
        mission_name_(mission_name),
        destination_name_(destination_name),
        coordinate_pulse_interval_(coordinate_pulse_interval),
        proper_pulse_interval_(proper_pulse_interval),
        cosmology_note_(cosmology_note),
        solar_system_(solar_system),
        colors_(colors) {}

  const UiLayout& Layout() const {
    return layout_;
  }

  void Draw(const RenderTexture2D& scene_target, const relativity::WorldlineSample& current, float stage, bool playing,
            const Vector2 mouse, std::string_view view_mode_label, bool cockpit_mode, const SceneLabels& labels,
            bool show_turnaround_label, bool show_help) const {
    DrawUiText(ui_font_, "Relativity Mission Control", 40.0F, 26.0F, 24.0F, colors_.text);
    DrawUiText(ui_font_,
               mission_name_ + "    " + FormatFixed(mission_.profile.distance_ly, 2) + " ly"
                   + "    beta_max " + FormatFixed(mission_.summary.peak_beta, 2)
                   + "    " + std::string(view_mode_label),
               42.0F, 56.0F, 15.0F, colors_.muted);

    DrawPanel(layout_.viewport, colors_.viewport_background, colors_.panel_edge);
    DrawTexturePro(scene_target.texture,
                   {0.0F, 0.0F, static_cast<float>(scene_target.texture.width),
                    -static_cast<float>(scene_target.texture.height)},
                   layout_.viewport, {0.0F, 0.0F}, 0.0F, WHITE);
    DrawRectangleRoundedLinesEx(layout_.viewport, 0.015F, 10, 1.0F, colors_.panel_edge);

    if (!cockpit_mode) {
      const std::array<Rectangle, 4> overlay_exclusions {
          ExpandedRect(layout_.mission_panel, 10.0F),
          ExpandedRect(layout_.frames_panel, 10.0F),
          ExpandedRect(layout_.simultaneity_panel, 10.0F),
          ExpandedRect(layout_.solar_context_panel, 10.0F),
      };
      DrawWorldLabel("Earth", labels.earth, 18.0F, colors_.structure, overlay_exclusions);
      DrawWorldLabel(destination_name_, labels.destination, 16.0F, colors_.structure, overlay_exclusions);

      if (show_turnaround_label) {
        DrawWorldLabel("Turnaround: a -> -a", labels.turnaround, 15.0F, colors_.text, overlay_exclusions);
      }
    }

    DrawMissionStatePanel(current, view_mode_label);
    DrawFrameTimesPanel(current);
    DrawSimultaneityPanel(current, cockpit_mode);
    if (!cockpit_mode) {
      DrawSolarContextPanel();
    }
    DrawControls(stage, playing, mouse, cockpit_mode);
    if (show_help) {
      DrawHelpOverlay(cockpit_mode);
    }
  }

 private:
  void DrawWorldLabel(const std::string& text, Vector2 viewport_anchor, float size, Color text_color,
                      std::span<const Rectangle> overlay_exclusions) const {
    const Vector2 anchor {layout_.viewport.x + viewport_anchor.x, layout_.viewport.y + viewport_anchor.y};
    if (!CheckCollisionPointRec(anchor, layout_.viewport)) {
      return;
    }

    const float text_width = MeasureUiText(ui_font_, text.c_str(), size);
    const float text_height = size + 4.0F;
    const std::array<Vector2, 6> offsets {
        Vector2 {-text_width * 0.5F, -text_height - 18.0F},
        Vector2 {14.0F, -text_height - 10.0F},
        Vector2 {14.0F, 8.0F},
        Vector2 {-text_width - 14.0F, 8.0F},
        Vector2 {-text_width - 14.0F, -text_height - 10.0F},
        Vector2 {-text_width * 0.5F, 10.0F},
    };

    for (const Vector2& offset : offsets) {
      const Rectangle text_bounds {anchor.x + offset.x, anchor.y + offset.y, text_width, text_height};
      const Rectangle chip_bounds = ExpandedRect(text_bounds, 7.0F);
      if (!ContainsRect(layout_.viewport, chip_bounds) || IntersectsAny(chip_bounds, overlay_exclusions)) {
        continue;
      }

      DrawRectangleRounded(chip_bounds, 0.20F, 8, Fade(colors_.viewport_background, 0.86F));
      DrawRectangleRoundedLinesEx(chip_bounds, 0.20F, 8, 1.0F, Fade(colors_.panel_edge, 0.72F));
      DrawUiText(ui_font_, text, text_bounds.x, text_bounds.y, size, text_color);
      return;
    }
  }

  void DrawMissionStatePanel(const relativity::WorldlineSample& current, std::string_view view_mode_label) const {
    DrawPanel(layout_.mission_panel, Color {19, 23, 29, 184}, Fade(colors_.panel_edge, 0.60F));
    DrawUiText(ui_font_, "Mission State", layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 14.0F, 16.0F,
               colors_.muted);
    DrawUiText(ui_font_, "view", layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 40.0F, 14.0F, colors_.muted);
    DrawUiText(ui_font_, std::string(view_mode_label), layout_.mission_panel.x + 92.0F, layout_.mission_panel.y + 40.0F,
               14.0F, colors_.text);
    DrawUiText(ui_font_, "phase", layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 66.0F, 14.0F, colors_.muted);
    DrawUiText(ui_font_, std::string(PhaseLabel(current.progress)), layout_.mission_panel.x + 92.0F,
               layout_.mission_panel.y + 66.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "beta", layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 92.0F, 14.0F, colors_.muted);
    DrawUiText(ui_font_, FormatFixed(current.beta, 3), layout_.mission_panel.x + 92.0F, layout_.mission_panel.y + 92.0F,
               14.0F, colors_.text);
    DrawUiText(ui_font_, "Lorentz factor", layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 118.0F, 14.0F,
               colors_.muted);
    DrawUiText(ui_font_, FormatFixed(current.gamma, 4), layout_.mission_panel.x + 138.0F, layout_.mission_panel.y + 118.0F,
               14.0F, colors_.text);
    DrawUiText(ui_font_, "dτ/dt", layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 144.0F, 14.0F,
               colors_.muted);
    DrawUiText(ui_font_, FormatFixed(current.proper_time_rate, 4), layout_.mission_panel.x + 138.0F,
               layout_.mission_panel.y + 144.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, DynamicsLabel(current.signed_proper_acceleration_ly_per_year2), layout_.mission_panel.x + 16.0F,
               layout_.mission_panel.y + 170.0F, 13.0F, colors_.muted);
    DrawUiText(ui_font_, FormatSignedFixed(current.signed_proper_acceleration_ly_per_year2, 3) + " ly / y^2",
               layout_.mission_panel.x + 16.0F, layout_.mission_panel.y + 188.0F, 13.0F, colors_.text);
  }

  void DrawFrameTimesPanel(const relativity::WorldlineSample& current) const {
    DrawPanel(layout_.frames_panel, Color {19, 23, 29, 184}, Fade(colors_.panel_edge, 0.60F));

    const Rectangle earth_rail {layout_.frames_panel.x + 24.0F, layout_.frames_panel.y + 62.0F, 28.0F, 116.0F};
    const Rectangle traveler_rail {layout_.frames_panel.x + 138.0F, layout_.frames_panel.y + 62.0F, 28.0F, 116.0F};
    const float earth_progress = static_cast<float>(current.coordinate_time_years / mission_.summary.coordinate_time_years);
    const float traveler_progress = static_cast<float>(current.proper_time_years / mission_.summary.coordinate_time_years);
    const float earth_marker_y = earth_rail.y + earth_rail.height - (earth_rail.height * earth_progress);
    const float traveler_marker_y = traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_progress);

    DrawUiText(ui_font_, "Frame Clocks", layout_.frames_panel.x + 16.0F, layout_.frames_panel.y + 14.0F, 16.0F,
               colors_.muted);
    DrawUiText(ui_font_, "Earth", earth_rail.x - 2.0F, earth_rail.y - 24.0F, 13.0F, colors_.earth_frame);
    DrawUiText(ui_font_, "Traveler", traveler_rail.x - 8.0F, traveler_rail.y - 24.0F, 13.0F, colors_.traveler_frame);

    DrawRectangleRounded(earth_rail, 0.2F, 8, Fade(colors_.earth_frame, 0.07F));
    DrawRectangleRoundedLinesEx(earth_rail, 0.2F, 8, 1.0F, Fade(colors_.earth_frame, 0.42F));
    DrawRectangleRounded(
        {earth_rail.x, earth_rail.y + earth_rail.height - (earth_rail.height * earth_progress), earth_rail.width,
         earth_rail.height * earth_progress},
        0.2F, 8, Fade(colors_.earth_frame, 0.30F));

    DrawRectangleRounded(traveler_rail, 0.2F, 8, Fade(colors_.traveler_frame, 0.07F));
    DrawRectangleRoundedLinesEx(traveler_rail, 0.2F, 8, 1.0F, Fade(colors_.traveler_frame, 0.48F));
    DrawRectangleRounded(
        {traveler_rail.x, traveler_rail.y + traveler_rail.height - (traveler_rail.height * traveler_progress),
         traveler_rail.width, traveler_rail.height * traveler_progress},
        0.2F, 8, Fade(colors_.traveler_frame, 0.32F));

    DrawLineEx({earth_rail.x - 10.0F, earth_marker_y}, {earth_rail.x + earth_rail.width + 10.0F, earth_marker_y}, 2.0F,
               colors_.earth_frame);
    DrawLineEx({traveler_rail.x - 10.0F, traveler_marker_y},
               {traveler_rail.x + traveler_rail.width + 10.0F, traveler_marker_y}, 2.0F, colors_.traveler_frame);

    DrawUiText(ui_font_, FormatFixed(current.coordinate_time_years, 2) + " y", earth_rail.x - 12.0F,
               earth_rail.y + earth_rail.height + 10.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, FormatFixed(current.proper_time_years, 2) + " y", traveler_rail.x - 10.0F,
               traveler_rail.y + traveler_rail.height + 10.0F, 14.0F, colors_.text);

    DrawUiText(ui_font_, "delta", layout_.frames_panel.x + 16.0F, layout_.frames_panel.y + 200.0F, 13.0F,
               colors_.muted);
    DrawUiText(ui_font_, FormatFixed(current.coordinate_time_years - current.proper_time_years, 3) + " y",
               layout_.frames_panel.x + 92.0F, layout_.frames_panel.y + 200.0F, 13.0F, colors_.traveler_frame);
  }

  void DrawSimultaneityPanel(const relativity::WorldlineSample& current, bool cockpit_mode) const {
    DrawPanel(layout_.simultaneity_panel, Color {19, 23, 29, 184}, Fade(colors_.panel_edge, 0.60F));
    DrawUiText(ui_font_, "Simultaneity Dashboard", layout_.simultaneity_panel.x + 16.0F,
               layout_.simultaneity_panel.y + 14.0F, 16.0F, colors_.muted);

    const float label_x = layout_.simultaneity_panel.x + 16.0F;
    const float value_x = layout_.simultaneity_panel.x + 168.0F;
    DrawUiText(ui_font_, "Proper time τ", label_x, layout_.simultaneity_panel.y + 46.0F, 14.0F, colors_.traveler_frame);
    DrawUiText(ui_font_, FormatFixed(current.proper_time_years, 3) + " y", value_x,
               layout_.simultaneity_panel.y + 46.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "Coordinate time t", label_x, layout_.simultaneity_panel.y + 74.0F, 14.0F, colors_.earth_frame);
    DrawUiText(ui_font_, FormatFixed(current.coordinate_time_years, 3) + " y", value_x,
               layout_.simultaneity_panel.y + 74.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "Observed at Earth", label_x, layout_.simultaneity_panel.y + 102.0F, 14.0F,
               colors_.traveler_frame);
    DrawUiText(ui_font_, FormatFixed(DelayedObservationTimeYears(current), 3) + " y", value_x,
               layout_.simultaneity_panel.y + 102.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "Earth light-travel delay", label_x, layout_.simultaneity_panel.y + 130.0F, 13.0F,
               colors_.muted);
    DrawUiText(ui_font_, FormatFixed(current.position_ly, 3) + " y", value_x, layout_.simultaneity_panel.y + 130.0F,
               13.0F, colors_.earth_frame);
    DrawUiText(ui_font_,
               cockpit_mode ? "Observed sky includes aberration, Doppler shift, and relativistic beaming."
                            : "Earth-frame now and delayed observation are kept distinct.",
               label_x, layout_.simultaneity_panel.y + 160.0F, 12.0F, colors_.muted);
  }

  void DrawSolarContextPanel() const {
    if (!solar_system_.ready) {
      return;
    }

    DrawPanel(layout_.solar_context_panel, Color {19, 23, 29, 184}, Fade(colors_.panel_edge, 0.60F));
    DrawUiText(ui_font_, "Solar System Context", layout_.solar_context_panel.x + 16.0F,
               layout_.solar_context_panel.y + 12.0F, 15.0F, colors_.muted);
    DrawUiText(ui_font_, solar_system_.source_label, layout_.solar_context_panel.x + 16.0F,
               layout_.solar_context_panel.y + 32.0F, 13.0F, colors_.text);
    DrawUiText(ui_font_, solar_system_.epoch_label, layout_.solar_context_panel.x + 16.0F,
               layout_.solar_context_panel.y + 48.0F, 12.0F, colors_.text);
    DrawUiText(ui_font_, "Earth-anchored heliocentric snapshot; radial distances compressed.", layout_.solar_context_panel.x + 16.0F,
               layout_.solar_context_panel.y + 68.0F, 12.0F, colors_.muted);
  }

  void DrawHelpOverlay(bool cockpit_mode) const {
    const Rectangle scrim = layout_.viewport;
    DrawRectangleRec(scrim, Fade(colors_.app_background, 0.72F));

    const Rectangle card {layout_.viewport.x + 220.0F, layout_.viewport.y + 86.0F, 760.0F, 404.0F};
    DrawPanel(card, Color {16, 20, 26, 242}, Fade(colors_.panel_edge, 0.84F));

    DrawUiText(ui_font_, "Controls", card.x + 24.0F, card.y + 20.0F, 24.0F, colors_.text);
    DrawUiText(ui_font_, "Press H to close this overlay.", card.x + 24.0F, card.y + 52.0F, 14.0F, colors_.muted);

    const float left_x = card.x + 24.0F;
    const float right_x = card.x + 394.0F;
    DrawUiText(ui_font_, "Playback", left_x, card.y + 92.0F, 16.0F, colors_.traveler_frame);
    DrawUiText(ui_font_, "Space play/pause", left_x, card.y + 120.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "Home reset mission   End jump to arrival", left_x, card.y + 144.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "Left/Right scrub   Slider drag seeks directly", left_x, card.y + 168.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_, "Wheel outside viewport steps the mission stage", left_x, card.y + 192.0F, 14.0F, colors_.text);

    DrawUiText(ui_font_, "Third-Person Camera", left_x, card.y + 236.0F, 16.0F, colors_.traveler_frame);
    DrawUiText(ui_font_, "Wheel zoom   Right-drag orbit   Middle-drag pan", left_x, card.y + 264.0F, 14.0F,
               colors_.text);
    DrawUiText(ui_font_, "WASD/QZ move target   1 iso   2 side   3 front   4 top", left_x, card.y + 288.0F, 14.0F,
               colors_.text);
    DrawUiText(ui_font_, "C resets camera", left_x, card.y + 312.0F, 14.0F, colors_.text);

    DrawUiText(ui_font_, "Mode And Export", right_x, card.y + 92.0F, 16.0F, colors_.earth_frame);
    DrawUiText(ui_font_, "F toggles between third-person and traveler view", right_x, card.y + 120.0F, 14.0F,
               colors_.text);
    DrawUiText(ui_font_, "E exports summary and visual PNGs", right_x, card.y + 144.0F, 14.0F, colors_.text);

    DrawUiText(ui_font_, "Traveler View", right_x, card.y + 188.0F, 16.0F, colors_.earth_frame);
    DrawUiText(ui_font_, "Right-drag or I/J/K/L looks around from the traveler frame", right_x, card.y + 216.0F,
               14.0F, colors_.text);
    DrawUiText(ui_font_,
               "The sky is a relativistic starfield: aberration compresses stars forward,", right_x, card.y + 240.0F,
               14.0F, colors_.text);
    DrawUiText(ui_font_, "Doppler shift changes color, and beaming boosts forward brightness.", right_x,
               card.y + 264.0F, 14.0F, colors_.text);
    DrawUiText(ui_font_,
               cockpit_mode ? "Traveler view is active now." : "Press F if you want to inspect the traveler-frame sky.",
               right_x, card.y + 288.0F, 14.0F, colors_.traveler_frame);

    DrawUiText(ui_font_, "Launch/build commands stay in the README and terminal --help output.", right_x,
               card.y + 336.0F, 13.0F, colors_.muted);
  }

  void DrawControls(float stage, bool playing, const Vector2 mouse, bool cockpit_mode) const {
    constexpr float kInstructionSize = 12.5F;
    DrawRectangleRounded(layout_.controls, 0.04F, 10, colors_.viewport_background);
    DrawRectangleRoundedLinesEx(layout_.controls, 0.04F, 10, 1.0F, colors_.panel_edge);

    DrawButton(ui_font_, layout_.play_button, playing ? "Pause" : "Play", playing,
               CheckCollisionPointRec(mouse, layout_.play_button), colors_.traveler_frame, colors_.traveler_frame,
               colors_.app_background);
    DrawButton(ui_font_, layout_.reset_button, "Reset", false, CheckCollisionPointRec(mouse, layout_.reset_button),
               Color {88, 96, 108, 255}, colors_.panel_edge, colors_.text);

    DrawRectangleRounded(layout_.slider_track, 1.0F, 16, Color {44, 49, 57, 255});
    DrawRectangleRounded({layout_.slider_track.x, layout_.slider_track.y, layout_.slider_track.width * stage,
                          layout_.slider_track.height},
                         1.0F, 16, colors_.traveler_frame);
    const Vector2 knob_center {layout_.slider_track.x + (layout_.slider_track.width * stage),
                               layout_.slider_track.y + (layout_.slider_track.height * 0.5F)};
    DrawCircleV(knob_center, 8.0F, colors_.text);
    DrawCircleV(knob_center, 3.0F, colors_.app_background);

    DrawUiText(ui_font_, "Playback", layout_.slider_track.x, layout_.controls.y + 14.0F, 17.0F, colors_.muted);
    DrawUiText(ui_font_, cockpit_mode ? "Traveler view active." : "Third-person world-tube active.",
               layout_.slider_track.x, layout_.controls.y + 57.0F, 14.0F, colors_.traveler_frame);
    DrawUiText(ui_font_,
               cosmology_note_ ? "SR model only; cosmology not included." : "SR model only.",
               layout_.slider_track.x + 188.0F, layout_.controls.y + 57.0F, 14.0F,
               cosmology_note_ ? colors_.warning : colors_.muted);
    DrawUiText(ui_font_, "Space play/pause  Left/Right scrub  Wheel steps timeline outside viewport",
               866.0F, layout_.controls.y + 14.0F, kInstructionSize, colors_.muted);
    DrawUiText(ui_font_, cockpit_mode ? "Right-drag or I/J/K/L look around  F third-person view  E export PNGs"
                                      : "Wheel zoom  Right-drag orbit  Middle-drag pan  F traveler view",
               866.0F, layout_.controls.y + 34.0F, kInstructionSize, colors_.muted);
    DrawUiText(ui_font_, cockpit_mode ? "C resets view state"
                                      : "WASD/QZ move target  1 iso  2 side  3 front  4 top  C reset",
               866.0F, layout_.controls.y + 54.0F, kInstructionSize, colors_.muted);
    DrawUiText(ui_font_, "H help", 1166.0F, layout_.controls.y + 14.0F, kInstructionSize, colors_.traveler_frame);
  }

  const Font& ui_font_;
  const relativity::MissionResult& mission_;
  std::string mission_name_;
  std::string destination_name_;
  double coordinate_pulse_interval_ {};
  double proper_pulse_interval_ {};
  bool cosmology_note_ {};
  SolarSystemContext solar_system_ {};
  AppColors colors_ {};
  UiLayout layout_ {
      .viewport = {40.0F, 96.0F, 1200.0F, 600.0F},
      .controls = {40.0F, 708.0F, 1200.0F, 96.0F},
      .play_button = {60.0F, 726.0F, 92.0F, 34.0F},
      .reset_button = {164.0F, 726.0F, 92.0F, 34.0F},
      .slider_track = {304.0F, 746.0F, 500.0F, 8.0F},
      .mission_panel = {76.0F, 138.0F, 236.0F, 218.0F},
      .frames_panel = {1012.0F, 138.0F, 200.0F, 236.0F},
      .simultaneity_panel = {954.0F, 470.0F, 258.0F, 186.0F},
      .solar_context_panel = {356.0F, 112.0F, 388.0F, 96.0F},
  };
};

}  // namespace

int main(int argc, char** argv) {
  constexpr int kScreenWidth = 1280;
  constexpr int kScreenHeight = 820;

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

  InitWindow(kScreenWidth, kScreenHeight, "Relativity Mission Control");
  SetTargetFPS(60);

  const AppColors colors {
      .app_background = {10, 12, 16, 255},
      .viewport_background = {14, 17, 21, 255},
      .panel_edge = {54, 61, 73, 255},
      .grid = {34, 39, 48, 255},
      .text = {228, 233, 241, 255},
      .muted = {132, 141, 154, 255},
      .structure = {176, 184, 194, 255},
      .earth_frame = {149, 164, 182, 255},
      .traveler_frame = {214, 221, 230, 255},
      .earth_body = {114, 133, 164, 255},
      .destination_body = {176, 167, 144, 255},
      .warning = {220, 197, 136, 255},
  };

  bool custom_font_loaded = false;
  const Font ui_font = LoadUiFont(custom_font_loaded);
  const SolarSystemContext solar_system = LoadSolarSystemContext("data/solar_system_horizons_2026-01-01.csv");
  DashboardUI ui(ui_font, mission, mission_name, destination_name, coordinate_pulse_interval, proper_pulse_interval,
                 cosmology_note, solar_system, colors);
  MissionScene scene(mission, ui.Layout().viewport, solar_system, colors);
  RelativisticCamera camera;

  bool playing = false;
  bool dragging_slider = false;
  bool export_requested = auto_export;
  bool show_help = false;
  float stage = auto_export ? 1.0F : 0.0F;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();
    const Vector2 mouse = GetMousePosition();
    const Vector2 mouse_delta = GetMouseDelta();
    const bool mouse_in_viewport = CheckCollisionPointRec(mouse, ui.Layout().viewport);

    if (IsKeyPressed(KEY_H)) {
      show_help = !show_help;
      if (show_help) {
        playing = false;
        dragging_slider = false;
      }
    }

    if (!show_help) {
      if (ButtonClicked(ui.Layout().play_button, mouse) || IsKeyPressed(KEY_SPACE)) {
        if (stage >= 1.0F) {
          stage = 0.0F;
        }
        playing = !playing;
      }
      if (ButtonClicked(ui.Layout().reset_button, mouse) || IsKeyPressed(KEY_HOME)) {
        stage = 0.0F;
        playing = false;
      }
      if (IsKeyPressed(KEY_END)) {
        stage = 1.0F;
        playing = false;
      }
      if (IsKeyPressed(KEY_F)) {
        camera.ToggleMode();
      }
      if (!camera.IsCockpitMode()) {
        if (IsKeyPressed(KEY_ONE)) {
          camera.SnapToThirdPersonPreset(RelativisticCamera::ThirdPersonPreset::Isometric);
        }
        if (IsKeyPressed(KEY_TWO)) {
          camera.SnapToThirdPersonPreset(RelativisticCamera::ThirdPersonPreset::Broadside);
        }
        if (IsKeyPressed(KEY_THREE)) {
          camera.SnapToThirdPersonPreset(RelativisticCamera::ThirdPersonPreset::Forward);
        }
        if (IsKeyPressed(KEY_FOUR)) {
          camera.SnapToThirdPersonPreset(RelativisticCamera::ThirdPersonPreset::Overhead);
        }
      }

      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
          CheckCollisionPointRec(mouse, ExpandedRect(ui.Layout().slider_track, 12.0F))) {
        dragging_slider = true;
        playing = false;
      }

      if (dragging_slider) {
        stage = SliderStageFromMouse(ui.Layout().slider_track, mouse);
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
        if (mouse_in_viewport && !camera.IsCockpitMode()) {
          camera.HandleViewportInput(true, {0.0F, 0.0F}, wheel, dt);
        } else {
          stage = Clamp01(stage + (wheel * 0.009F));
          playing = false;
        }
      }

      camera.HandleViewportInput(mouse_in_viewport, mouse_delta, 0.0F, dt);

      if (IsKeyPressed(KEY_C)) {
        camera.ResetView();
      }
      if (IsKeyPressed(KEY_E)) {
        export_requested = true;
      }
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
    const PathFrame ship_frame = scene.ShipFrame(current);
    camera.UpdateFromShip(ship_frame);

    scene.Render(camera, current, current_index, coordinate_pulse_interval, proper_pulse_interval);
    const SceneLabels labels = scene.ProjectLabels(camera.ActiveCamera());

    BeginDrawing();
    ClearBackground(colors.app_background);
    ui.Draw(scene.RenderTarget(), current, stage, playing, mouse, camera.ModeLabel(), camera.IsCockpitMode(), labels,
            current.progress >= 0.42, show_help);
    EndDrawing();

    if (export_requested) {
      const std::string export_prefix = auto_export ? options.export_prefix : "mission_export";
      const bool export_ok =
          ExportMissionArtifacts(export_prefix, ui_font, scene.RenderTarget(), mission, mission_name, destination_name,
                                 coordinate_pulse_interval, proper_pulse_interval, cosmology_note, colors);
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

  CloseWindow();
  return 0;
}
