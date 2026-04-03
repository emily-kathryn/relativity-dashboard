#include "relativity/simulation.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace relativity {

namespace {

constexpr std::array<MissionPreset, 4> kMissionPresets {{
    {"alpha-centauri", "Alpha Centauri", 4.37, 0.80},
    {"barnards-star", "Barnard's Star", 5.96, 0.80},
    {"sirius", "Sirius", 8.61, 0.85},
    {"trappist-1", "TRAPPIST-1", 39.50, 0.95},
}};

void ValidateProfile(const MissionProfile& profile) {
  if (!(profile.distance_ly > 0.0) || !std::isfinite(profile.distance_ly)) {
    throw std::invalid_argument("distance must be finite and greater than zero");
  }

  if (!(profile.beta > 0.0) || !(profile.beta < 1.0) || !std::isfinite(profile.beta)) {
    throw std::invalid_argument("beta must be finite and satisfy 0 < beta < 1");
  }

  if (profile.sample_count < 2) {
    throw std::invalid_argument("sample_count must be at least 2");
  }
}

}  // namespace

double LorentzGamma(double beta) {
  if (!(beta >= 0.0) || !(beta < 1.0) || !std::isfinite(beta)) {
    throw std::invalid_argument("beta must be finite and satisfy 0 <= beta < 1");
  }

  return 1.0 / std::sqrt(1.0 - (beta * beta));
}

double ProperTimeRate(double gamma) {
  if (!(gamma >= 1.0) || !std::isfinite(gamma)) {
    throw std::invalid_argument("gamma must be finite and satisfy gamma >= 1");
  }

  return 1.0 / gamma;
}

std::vector<MissionPreset> MissionPresets() {
  return {kMissionPresets.begin(), kMissionPresets.end()};
}

std::optional<MissionPreset> FindMissionPreset(std::string_view id) {
  for (const auto& preset : kMissionPresets) {
    if (preset.id == id) {
      return preset;
    }
  }
  return std::nullopt;
}

MissionResult SimulateMission(const MissionProfile& profile) {
  ValidateProfile(profile);

  const double peak_gamma = LorentzGamma(profile.beta);
  const double peak_rapidity = std::atanh(profile.beta);
  const double half_distance_ly = profile.distance_ly * 0.5;
  const double proper_acceleration = (peak_gamma - 1.0) / half_distance_ly;
  const double half_coordinate_time_years = std::sinh(peak_rapidity) / proper_acceleration;
  const double half_proper_time_years = peak_rapidity / proper_acceleration;
  const double coordinate_time_years = half_coordinate_time_years * 2.0;
  const double proper_time_years = half_proper_time_years * 2.0;
  const double elapsed_difference_years = coordinate_time_years - proper_time_years;
  const double arrival_signal_to_earth_years = coordinate_time_years + profile.distance_ly;

  MissionResult result {
      .profile = profile,
      .summary =
          {
              .gamma = peak_gamma,
              .peak_beta = profile.beta,
              .proper_acceleration_ly_per_year2 = proper_acceleration,
              .coordinate_time_years = coordinate_time_years,
              .proper_time_years = proper_time_years,
              .elapsed_difference_years = elapsed_difference_years,
              .one_way_light_time_years = profile.distance_ly,
              .arrival_signal_to_earth_years = arrival_signal_to_earth_years,
          },
      .samples = {},
  };

  result.samples.reserve(profile.sample_count);

  for (std::size_t index = 0; index < profile.sample_count; ++index) {
    const double progress = static_cast<double>(index) /
                            static_cast<double>(profile.sample_count - 1);
    const double proper_time = proper_time_years * progress;

    double rapidity = 0.0;
    double gamma = 1.0;
    double beta = 0.0;
    double coordinate_time = 0.0;
    double position_ly = 0.0;
    double signed_proper_acceleration = proper_acceleration;

    if (proper_time <= half_proper_time_years) {
      rapidity = proper_acceleration * proper_time;
      gamma = std::cosh(rapidity);
      beta = std::tanh(rapidity);
      coordinate_time = std::sinh(rapidity) / proper_acceleration;
      position_ly = (gamma - 1.0) / proper_acceleration;
      signed_proper_acceleration = proper_acceleration;
    } else {
      const double post_midpoint_proper = proper_time - half_proper_time_years;
      rapidity = peak_rapidity - (proper_acceleration * post_midpoint_proper);
      gamma = std::cosh(rapidity);
      beta = std::tanh(rapidity);
      coordinate_time = half_coordinate_time_years +
                        ((std::sinh(peak_rapidity) - std::sinh(rapidity)) / proper_acceleration);
      position_ly = half_distance_ly + ((peak_gamma - gamma) / proper_acceleration);
      signed_proper_acceleration = -proper_acceleration;
    }

    const double signal_return_to_earth = coordinate_time + position_ly;

    result.samples.push_back(WorldlineSample {
        .progress = progress,
        .coordinate_time_years = coordinate_time,
        .proper_time_years = proper_time,
        .position_ly = position_ly,
        .gamma = gamma,
        .proper_time_rate = ProperTimeRate(gamma),
        .beta = beta,
        .rapidity = rapidity,
        .signed_proper_acceleration_ly_per_year2 = signed_proper_acceleration,
        .signal_return_to_earth_years = signal_return_to_earth,
    });
  }

  return result;
}

std::string FormatMissionReport(const MissionResult& result) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);
  out << "Relativity Mission Report\n";
  out << "distance_ly               : " << result.profile.distance_ly << '\n';
  out << "peak_beta                 : " << result.summary.peak_beta << '\n';
  out << "peak_gamma                : " << result.summary.gamma << '\n';
  out << "proper_acceleration       : " << result.summary.proper_acceleration_ly_per_year2 << '\n';
  out << "earth_frame_time_years    : " << result.summary.coordinate_time_years << '\n';
  out << "traveler_proper_time_years: " << result.summary.proper_time_years << '\n';
  out << "time_difference_years     : " << result.summary.elapsed_difference_years << '\n';
  out << "light_time_years          : " << result.summary.one_way_light_time_years << '\n';
  out << "return_signal_years       : " << result.summary.arrival_signal_to_earth_years << '\n';
  return out.str();
}

}  // namespace relativity
