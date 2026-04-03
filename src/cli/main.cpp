#include "relativity/simulation.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct CliOptions {
  double distance_ly {4.37};
  double beta {0.8};
  std::size_t samples {400};
  std::string destination_id {};
  std::filesystem::path csv_path {};
  bool distance_explicit {false};
  bool beta_explicit {false};
};

void PrintUsage(const char* program_name) {
  const auto presets = relativity::MissionPresets();
  std::cout
      << "Usage: " << program_name << " [options]\n"
      << "  --destination <preset>     Named destination preset.\n"
      << "  --distance <light-years>   Mission distance in light-years.\n"
      << "  --beta <fraction-of-c>     Cruise speed as a fraction of c.\n"
      << "  --samples <count>          Number of worldline samples.\n"
      << "  --csv <path>               Export the sampled worldline to CSV.\n"
      << "  --help                     Show this help message.\n";

  std::cout << "\nAvailable destination presets:\n";
  for (const auto& preset : presets) {
    std::cout << "  " << preset.id << "  (" << preset.display_name << ", "
              << preset.distance_ly << " ly, suggested beta " << preset.suggested_beta << ")\n";
  }
}

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
      options.distance_explicit = true;
    } else if (arg == "--beta") {
      options.beta = ParseDouble(require_value(arg), arg);
      options.beta_explicit = true;
    } else if (arg == "--destination") {
      options.destination_id = std::string(require_value(arg));
    } else if (arg == "--samples") {
      options.samples = ParseSize(require_value(arg), arg);
    } else if (arg == "--csv") {
      options.csv_path = require_value(arg);
    } else if (arg == "--help") {
      PrintUsage(argv[0]);
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(arg));
    }
  }

  return options;
}

struct ResolvedMissionOptions {
  relativity::MissionProfile profile;
  std::optional<relativity::MissionPreset> preset;
};

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

void WriteCsv(const std::filesystem::path& path, const relativity::MissionResult& result) {
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open CSV output: " + path.string());
  }

  out << "progress,coordinate_time_years,proper_time_years,position_ly,gamma,proper_time_rate,"
         "signal_return_to_earth_years\n";
  for (const auto& sample : result.samples) {
    out << sample.progress << ','
        << sample.coordinate_time_years << ','
        << sample.proper_time_years << ','
        << sample.position_ly << ','
        << sample.gamma << ','
        << sample.proper_time_rate << ','
        << sample.signal_return_to_earth_years << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const CliOptions options = ParseArgs(argc, argv);
    const ResolvedMissionOptions resolved = ResolveMissionOptions(options);
    const relativity::MissionResult result = relativity::SimulateMission(resolved.profile);

    std::cout << relativity::FormatMissionReport(result);
    if (resolved.preset.has_value()) {
      std::cout << "destination_preset        : " << resolved.preset->display_name << " (" << resolved.preset->id << ")\n";
    }

    if (!options.csv_path.empty()) {
      WriteCsv(options.csv_path, result);
      std::cout << "csv_export                : " << options.csv_path.string() << '\n';
    }

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    PrintUsage(argv[0]);
    return 1;
  }
}
