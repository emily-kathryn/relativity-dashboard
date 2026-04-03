# Relativity Mission Control

Relativity Mission Control is now a native C++ project for modeling one-way relativistic travel between two stationary points in flat spacetime. The project is built around a reusable simulation library, a command-line interface for repeatable calculations, and a native visualizer that focuses on the felt effect of time dilation rather than a dashboard of charts.

This version is intentionally narrow and explicit:

- It models special relativity, not cosmology.
- It assumes Earth and the destination are stationary in the same inertial frame.
- It models a symmetric outbound trip: accelerate for the first half, brake for the second half.
- It computes Earth-frame time and traveler proper time directly from the relativistic worldline.

That makes the physics transparent enough to extend later to round trips, Doppler effects, and eventually cosmological travel models.

## Why C++

The project now favors:

- a single compiled simulation core
- deterministic numeric behavior
- low-overhead real-time visualization
- room to grow into richer rendering without changing languages again

## Physics Model

The current mission model uses units where:

- distance is measured in light-years
- time is measured in years
- the speed of light is `c = 1 light-year / year`

For a one-way mission with total Earth-frame distance `D` and midpoint peak speed `beta * c`:

- Lorentz factor: `gamma = 1 / sqrt(1 - beta^2)`
- Rapidity: `eta = atanh(beta)`
- Proper acceleration for the symmetric half-trip:
  `a = (gamma - 1) / (D / 2)`
- During acceleration:
  `x(tau) = (cosh(a tau) - 1) / a`
  `t(tau) = sinh(a tau) / a`
- Proper time still comes from the worldline, so the traveler ages less than the Earth frame across the same trip.

The visualizer now renders a single cinematic scene:

- two synchronized clocks that visibly drift apart
- a ship accelerating away from Earth and braking toward the destination
- pulse rings and aging bars that show Earth time accumulating faster
- direct playback controls and a mission-stage scrubber

## Project Layout

```text
.
├── CMakeLists.txt
├── include/relativity/simulation.hpp
├── src/lib/simulation.cpp
├── src/cli/main.cpp
├── src/visualizer/main.cpp
└── tests/simulation_tests.cpp
```

## Build

Configure and build everything:

```bash
cmake -S . -B build
cmake --build build
```

If you only want the simulation library, CLI, and tests:

```bash
cmake -S . -B build -DRELATIVITY_BUILD_VISUALIZER=OFF
cmake --build build
```

## Run

CLI example:

```bash
./build/relativity_cli --distance 4.37 --beta 0.8
```

Export worldline samples as CSV:

```bash
./build/relativity_cli --distance 4.37 --beta 0.8 --samples 600 --csv mission.csv
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Launch the visualizer:

```bash
./build/relativity_visualizer --distance 4.37 --beta 0.8
```

Visualizer controls:

- click `Play` or press `Space` to start or pause travel
- click `Reset` or press `Home` to return to departure
- drag the slider to choose the mission stage directly
- `Left` / `Right`: scrub backward or forward
- mouse wheel: fine stage adjustments
- `End`: jump to arrival

## Next Physics Steps

- round-trip missions and the twin paradox
- Doppler shift and delayed observations
- apparent versus measured effects
- cosmology mode for intergalactic distances

## Research Reading

Useful starting points for understanding what the code is modeling:

- Einstein (1905), *On the Electrodynamics of Moving Bodies*
- Savage et al., *Real Time Relativity*
- Weiskopf et al., *A Tutorial on Relativistic Visualization*
- McGrath et al., *Visualizing relativity: The OpenRelativity project*
- Davis and Lineweaver, *Expanding Confusion* for why distant galaxies need cosmology, not just special relativity
