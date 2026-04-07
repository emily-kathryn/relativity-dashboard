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

struct IntegratorState {
  double proper_time_years {};
  double coordinate_time_years {};
  double position_ly {};
  double rapidity {};
};

struct StateDerivative {
  double proper_time_years {};
  double coordinate_time_years {};
  double position_ly {};
  double rapidity {};
};

IntegratorState AddScaled(const IntegratorState& state, const StateDerivative& derivative, double scale) {
  return {
      .proper_time_years = state.proper_time_years + (derivative.proper_time_years * scale),
      .coordinate_time_years = state.coordinate_time_years + (derivative.coordinate_time_years * scale),
      .position_ly = state.position_ly + (derivative.position_ly * scale),
      .rapidity = state.rapidity + (derivative.rapidity * scale),
  };
}

StateDerivative EvaluateDerivative(const IntegratorState& state, double signed_proper_acceleration_ly_per_year2) {
  return {
      .proper_time_years = 1.0,
      .coordinate_time_years = std::cosh(state.rapidity),
      .position_ly = std::sinh(state.rapidity),
      .rapidity = signed_proper_acceleration_ly_per_year2,
  };
}

IntegratorState AdvanceStateRk4(const IntegratorState& initial_state, double step_years,
                                double signed_proper_acceleration_ly_per_year2) {
  const StateDerivative k1 = EvaluateDerivative(initial_state, signed_proper_acceleration_ly_per_year2);
  const StateDerivative k2 =
      EvaluateDerivative(AddScaled(initial_state, k1, step_years * 0.5), signed_proper_acceleration_ly_per_year2);
  const StateDerivative k3 =
      EvaluateDerivative(AddScaled(initial_state, k2, step_years * 0.5), signed_proper_acceleration_ly_per_year2);
  const StateDerivative k4 =
      EvaluateDerivative(AddScaled(initial_state, k3, step_years), signed_proper_acceleration_ly_per_year2);

  return {
      .proper_time_years = initial_state.proper_time_years +
                           (step_years / 6.0) *
                               (k1.proper_time_years + (2.0 * k2.proper_time_years) + (2.0 * k3.proper_time_years) +
                                k4.proper_time_years),
      .coordinate_time_years = initial_state.coordinate_time_years +
                               (step_years / 6.0) *
                                   (k1.coordinate_time_years + (2.0 * k2.coordinate_time_years) +
                                    (2.0 * k3.coordinate_time_years) + k4.coordinate_time_years),
      .position_ly = initial_state.position_ly +
                     (step_years / 6.0) *
                         (k1.position_ly + (2.0 * k2.position_ly) + (2.0 * k3.position_ly) + k4.position_ly),
      .rapidity =
          initial_state.rapidity +
          (step_years / 6.0) * (k1.rapidity + (2.0 * k2.rapidity) + (2.0 * k3.rapidity) + k4.rapidity),
  };
}

IntegratorState IntegrateToProperTime(IntegratorState state, double target_proper_time_years, double max_step_years,
                                      double half_proper_time_years, double proper_acceleration_ly_per_year2) {
  constexpr double kTimeEpsilon = 1e-12;

  while (state.proper_time_years + kTimeEpsilon < target_proper_time_years) {
    const double signed_proper_acceleration =
        state.proper_time_years < half_proper_time_years ? proper_acceleration_ly_per_year2 : -proper_acceleration_ly_per_year2;
    double step_years = std::min(max_step_years, target_proper_time_years - state.proper_time_years);

    if (state.proper_time_years < half_proper_time_years &&
        state.proper_time_years + step_years > half_proper_time_years + kTimeEpsilon) {
      step_years = half_proper_time_years - state.proper_time_years;
    }

    state = AdvanceStateRk4(state, step_years, signed_proper_acceleration);
  }

  const double remaining_step_years = target_proper_time_years - state.proper_time_years;
  if (remaining_step_years > kTimeEpsilon) {
    const double signed_proper_acceleration =
        state.proper_time_years < half_proper_time_years ? proper_acceleration_ly_per_year2 : -proper_acceleration_ly_per_year2;
    state = AdvanceStateRk4(state, remaining_step_years, signed_proper_acceleration);
  }

  state.proper_time_years = target_proper_time_years;
  return state;
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
  const double half_proper_time_years = peak_rapidity / proper_acceleration;
  const double proper_time_years = half_proper_time_years * 2.0;
  const std::size_t integration_step_count = std::max<std::size_t>(profile.sample_count * 16, 65536);
  const double max_step_years = proper_time_years / static_cast<double>(integration_step_count);

  MissionResult result {
      .profile = profile,
      .summary = {},
      .samples = {},
  };

  result.samples.reserve(profile.sample_count);
  IntegratorState state {};

  for (std::size_t index = 0; index < profile.sample_count; ++index) {
    const double progress = static_cast<double>(index) /
                            static_cast<double>(profile.sample_count - 1);
    const double target_proper_time_years = proper_time_years * progress;
    state = IntegrateToProperTime(state, target_proper_time_years, max_step_years, half_proper_time_years,
                                  proper_acceleration);

    const double gamma = std::cosh(state.rapidity);
    const double beta = std::tanh(state.rapidity);
    const double signed_proper_acceleration =
        state.proper_time_years <= half_proper_time_years ? proper_acceleration : -proper_acceleration;
    const double signal_return_to_earth = state.coordinate_time_years + state.position_ly;

    result.samples.push_back(WorldlineSample {
        .progress = progress,
        .coordinate_time_years = state.coordinate_time_years,
        .proper_time_years = state.proper_time_years,
        .position_ly = state.position_ly,
        .gamma = gamma,
        .proper_time_rate = ProperTimeRate(gamma),
        .beta = beta,
        .rapidity = state.rapidity,
        .signed_proper_acceleration_ly_per_year2 = signed_proper_acceleration,
        .signal_return_to_earth_years = signal_return_to_earth,
    });
  }

  const auto& final_sample = result.samples.back();
  result.summary = {
      .gamma = peak_gamma,
      .peak_beta = profile.beta,
      .proper_acceleration_ly_per_year2 = proper_acceleration,
      .coordinate_time_years = final_sample.coordinate_time_years,
      .proper_time_years = proper_time_years,
      .elapsed_difference_years = final_sample.coordinate_time_years - proper_time_years,
      .one_way_light_time_years = profile.distance_ly,
      .arrival_signal_to_earth_years = final_sample.signal_return_to_earth_years,
  };

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
