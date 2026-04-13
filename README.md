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
- State vector: `[tau, t, x, eta]`
- Integrated equations of motion in proper time:
  `deta / dtau = a(tau)`, `dx / dtau = sinh(eta)`, `dt / dtau = cosh(eta)`
- The current presets still use the symmetric constant-proper-acceleration mission, but the sampler now uses RK4 so non-constant proper-acceleration profiles can be added later without changing the simulation structure.
- Proper time still comes from the worldline, so the traveler ages less than the Earth frame across the same trip.

The visualizer now renders a single cinematic scene:

- a proper-time-rate world-tube whose radius follows `dτ/dt = 1 / gamma`
- traveler proper-time pulse rings and Earth-frame coordinate-time pulse rings
- a ship accelerating away from Earth and braking toward the destination
- an Earth-anchored solar-system context layer from a bundled NASA/JPL Horizons snapshot
- a first-person traveler view with relativistic aberration, Doppler shift, and beaming in the starfield
- frame-specific overlays for Earth-frame and traveler-frame quantities, including a simultaneity dashboard
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

CLI preset example:

```bash
./build/relativity_cli --destination alpha-centauri
```

Export worldline samples as CSV:

```bash
./build/relativity_cli --distance 4.37 --beta 0.8 --samples 600 --csv mission.csv
```

CSV columns include sampled coordinate time, proper time, `gamma`, proper-time rate `dτ/dt`, and return-signal time.

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Launch the visualizer:

```bash
./build/relativity_visualizer --distance 4.37 --beta 0.8
```

Launch the visualizer with a named destination preset:

```bash
./build/relativity_visualizer --destination trappist-1
```

Export a mission summary PNG and mission visual PNG:

```bash
./build/relativity_visualizer --destination alpha-centauri --export-prefix exports/alpha_centauri
```

This writes:

- `exports/alpha_centauri_summary.png`
- `exports/alpha_centauri_visual.png`

Visualizer controls:

- click `Play` or press `Space` to start or pause travel
- click `Reset` or press `Home` to return to departure
- drag the slider to choose the mission stage directly
- `Left` / `Right`: scrub backward or forward
- mouse wheel outside the viewport: fine stage adjustments
- mouse wheel over the viewport: 3D zoom
- right-drag in the viewport: orbit camera
- middle-drag in the viewport: pan camera
- `W` / `A` / `S` / `D`: translate the third-person view target
- `Q` / `Z`: move the third-person view target up or down
- `1`: snap to the default isometric view
- `2`: snap to a side view
- `3`: snap to a front view
- `4`: snap to a top view
- `F`: toggle traveler view
- right-drag in traveler view: look around from the traveler frame
- `I` / `J` / `K` / `L`: traveler-view look-around with the keyboard
- `C`: reset view and cockpit look direction
- `H`: open the in-app controls overlay
- `E`: export `mission_export_summary.png` and `mission_export_visual.png` from the current view
- `End`: jump to arrival

Traveler view is not a ship-interior view. It is a traveler-frame sky view meant to show how the forward sky changes under relativistic aberration, Doppler shift, and beaming.

The third-person scene also includes a contextual solar-system snapshot. The Sun and major planets come from a bundled NASA/JPL Horizons heliocentric vector table dated January 1, 2026 (TDB). Those body positions are anchored to Earth and radially compressed for visibility, so they are intended as orientation context rather than a true-to-scale local dynamics model.

Available destination presets:

- `alpha-centauri`
- `barnards-star`
- `sirius`
- `trappist-1`

If a preset is selected without `--distance` or `--beta`, the preset distance and suggested beta are used.

## Scope Note

The mission presets and visualizer remain special-relativistic only. Earth and the destination are assumed to be stationary in one inertial frame. Distances on the order of millions of light-years require cosmological expansion and curved-spacetime modeling, which this project does not yet implement.

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
