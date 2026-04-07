#include "relativity/simulation.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

bool NearlyEqual(double left, double right, double tolerance = 1e-9) {
  return std::abs(left - right) <= tolerance;
}

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct AnalyticalSample {
  double coordinate_time_years {};
  double proper_time_years {};
  double position_ly {};
  double gamma {};
  double proper_time_rate {};
  double beta {};
  double rapidity {};
  double signed_proper_acceleration_ly_per_year2 {};
  double signal_return_to_earth_years {};
};

AnalyticalSample AnalyticalWorldlineSample(const relativity::MissionProfile& profile, double progress) {
  const double peak_gamma = relativity::LorentzGamma(profile.beta);
  const double peak_rapidity = std::atanh(profile.beta);
  const double half_distance_ly = profile.distance_ly * 0.5;
  const double proper_acceleration = (peak_gamma - 1.0) / half_distance_ly;
  const double half_coordinate_time_years = std::sinh(peak_rapidity) / proper_acceleration;
  const double half_proper_time_years = peak_rapidity / proper_acceleration;
  const double proper_time_years = 2.0 * half_proper_time_years;
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

  return {
      .coordinate_time_years = coordinate_time,
      .proper_time_years = proper_time,
      .position_ly = position_ly,
      .gamma = gamma,
      .proper_time_rate = relativity::ProperTimeRate(gamma),
      .beta = beta,
      .rapidity = rapidity,
      .signed_proper_acceleration_ly_per_year2 = signed_proper_acceleration,
      .signal_return_to_earth_years = coordinate_time + position_ly,
  };
}

void TestLorentzGamma() {
  const double gamma = relativity::LorentzGamma(0.8);
  Expect(NearlyEqual(gamma, 1.6666666666666667), "gamma(0.8) should be 5/3");
  Expect(NearlyEqual(relativity::ProperTimeRate(gamma), 0.6), "proper-time rate should be 1/gamma");
}

void TestMissionSummary() {
  const auto result = relativity::SimulateMission(relativity::MissionProfile {
      .distance_ly = 4.37,
      .beta = 0.8,
      .sample_count = 5,
  });

  Expect(NearlyEqual(result.summary.coordinate_time_years, 8.74, 1e-6), "earth-frame time mismatch");
  Expect(NearlyEqual(result.summary.proper_time_years, 7.201403552219457, 1e-6), "proper time mismatch");
  Expect(NearlyEqual(result.summary.elapsed_difference_years, 1.5385964477805398, 1e-6), "time difference mismatch");
  Expect(NearlyEqual(result.summary.proper_acceleration_ly_per_year2, 0.30511060259344025, 1e-9),
         "proper acceleration mismatch");
  Expect(result.samples.size() == 5, "sample count mismatch");
  Expect(NearlyEqual(result.samples.back().position_ly, 4.37, 1e-6), "final position mismatch");
  Expect(NearlyEqual(result.samples.front().beta, 0.0), "initial beta mismatch");
  Expect(NearlyEqual(result.samples.front().proper_time_rate, 1.0), "initial proper-time rate mismatch");
  Expect(NearlyEqual(result.samples[2].beta, 0.8, 1e-6), "midpoint peak beta mismatch");
  Expect(NearlyEqual(result.samples[2].proper_time_rate, 0.6, 1e-6), "midpoint proper-time rate mismatch");
  Expect(NearlyEqual(result.samples.back().beta, 0.0, 1e-6), "final beta mismatch");
}

void TestRk4MatchesAnalyticalPresets() {
  constexpr relativity::MissionProfile kProfiles[] {
      {.distance_ly = 4.37, .beta = 0.80, .sample_count = 257},
      {.distance_ly = 5.96, .beta = 0.80, .sample_count = 257},
      {.distance_ly = 8.61, .beta = 0.85, .sample_count = 257},
      {.distance_ly = 39.50, .beta = 0.95, .sample_count = 257},
  };

  for (const auto& profile : kProfiles) {
    const auto result = relativity::SimulateMission(profile);
    const auto expected_summary = AnalyticalWorldlineSample(profile, 1.0);

    Expect(NearlyEqual(result.summary.coordinate_time_years, expected_summary.coordinate_time_years, 1e-6),
           "summary coordinate time mismatch");
    Expect(NearlyEqual(result.summary.proper_time_years, expected_summary.proper_time_years, 1e-12),
           "summary proper time mismatch");
    Expect(NearlyEqual(result.summary.elapsed_difference_years,
                       expected_summary.coordinate_time_years - expected_summary.proper_time_years, 1e-6),
           "summary elapsed difference mismatch");
    Expect(NearlyEqual(result.summary.arrival_signal_to_earth_years, expected_summary.signal_return_to_earth_years, 1e-6),
           "summary signal return mismatch");

    for (std::size_t index = 0; index < result.samples.size(); ++index) {
      const double progress =
          static_cast<double>(index) / static_cast<double>(result.samples.size() - 1);
      const auto expected = AnalyticalWorldlineSample(profile, progress);
      const auto& actual = result.samples[index];

      Expect(NearlyEqual(actual.coordinate_time_years, expected.coordinate_time_years, 2e-6),
             "sample coordinate time mismatch");
      Expect(NearlyEqual(actual.proper_time_years, expected.proper_time_years, 1e-12),
             "sample proper time mismatch");
      Expect(NearlyEqual(actual.position_ly, expected.position_ly, 2e-6), "sample position mismatch");
      Expect(NearlyEqual(actual.gamma, expected.gamma, 2e-6), "sample gamma mismatch");
      Expect(NearlyEqual(actual.proper_time_rate, expected.proper_time_rate, 2e-6),
             "sample proper-time rate mismatch");
      Expect(NearlyEqual(actual.beta, expected.beta, 2e-6), "sample beta mismatch");
      Expect(NearlyEqual(actual.rapidity, expected.rapidity, 2e-6), "sample rapidity mismatch");
      Expect(NearlyEqual(actual.signed_proper_acceleration_ly_per_year2,
                         expected.signed_proper_acceleration_ly_per_year2, 1e-12),
             "sample proper acceleration mismatch");
      Expect(NearlyEqual(actual.signal_return_to_earth_years, expected.signal_return_to_earth_years, 3e-6),
             "sample signal return mismatch");
    }
  }
}

void TestMissionPresets() {
  const auto preset = relativity::FindMissionPreset("alpha-centauri");
  Expect(preset.has_value(), "alpha-centauri preset should exist");
  Expect(NearlyEqual(preset->distance_ly, 4.37), "alpha-centauri distance mismatch");
  Expect(!relativity::FindMissionPreset("unknown").has_value(), "unknown preset should not exist");
}

void TestInvalidInput() {
  bool threw = false;
  try {
    static_cast<void>(relativity::SimulateMission(relativity::MissionProfile {
        .distance_ly = 4.37,
        .beta = 1.1,
        .sample_count = 5,
    }));
  } catch (const std::invalid_argument&) {
    threw = true;
  }

  Expect(threw, "invalid beta should throw");
}

}  // namespace

int main() {
  try {
    TestLorentzGamma();
    TestMissionSummary();
    TestRk4MatchesAnalyticalPresets();
    TestMissionPresets();
    TestInvalidInput();
    std::cout << "All relativity tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
