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

void TestLorentzGamma() {
  const double gamma = relativity::LorentzGamma(0.8);
  Expect(NearlyEqual(gamma, 1.6666666666666667), "gamma(0.8) should be 5/3");
}

void TestMissionSummary() {
  const auto result = relativity::SimulateMission(relativity::MissionProfile {
      .distance_ly = 4.37,
      .beta = 0.8,
      .sample_count = 5,
  });

  Expect(NearlyEqual(result.summary.coordinate_time_years, 8.74), "earth-frame time mismatch");
  Expect(NearlyEqual(result.summary.proper_time_years, 7.201403552219457, 1e-6), "proper time mismatch");
  Expect(NearlyEqual(result.summary.elapsed_difference_years, 1.5385964477805398, 1e-6), "time difference mismatch");
  Expect(NearlyEqual(result.summary.proper_acceleration_ly_per_year2, 0.30511060259344025, 1e-9),
         "proper acceleration mismatch");
  Expect(result.samples.size() == 5, "sample count mismatch");
  Expect(NearlyEqual(result.samples.back().position_ly, 4.37), "final position mismatch");
  Expect(NearlyEqual(result.samples.front().beta, 0.0), "initial beta mismatch");
  Expect(NearlyEqual(result.samples[2].beta, 0.8, 1e-9), "midpoint peak beta mismatch");
  Expect(NearlyEqual(result.samples.back().beta, 0.0, 1e-9), "final beta mismatch");
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
    TestInvalidInput();
    std::cout << "All relativity tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
