#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace relativity {

struct MissionProfile {
  double distance_ly {};
  double beta {};
  std::size_t sample_count {400};
};

struct WorldlineSample {
  double progress {};
  double coordinate_time_years {};
  double proper_time_years {};
  double position_ly {};
  double gamma {};
  double beta {};
  double rapidity {};
  double signed_proper_acceleration_ly_per_year2 {};
  double signal_return_to_earth_years {};
};

struct MissionSummary {
  double gamma {};
  double peak_beta {};
  double proper_acceleration_ly_per_year2 {};
  double coordinate_time_years {};
  double proper_time_years {};
  double elapsed_difference_years {};
  double one_way_light_time_years {};
  double arrival_signal_to_earth_years {};
};

struct MissionResult {
  MissionProfile profile;
  MissionSummary summary;
  std::vector<WorldlineSample> samples;
};

double LorentzGamma(double beta);
MissionResult SimulateMission(const MissionProfile& profile);
std::string FormatMissionReport(const MissionResult& result);

}  // namespace relativity
