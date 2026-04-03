# Relativity Mission Dashboard

An interactive tool for modeling time dilation in high-velocity space travel using special relativity.

## Features
- Lorentz factor (gamma) calculation
- Earth vs traveler time comparison
- Mission presets for different destinations
- Time difference graph showing relativistic effects
- Real-time visualization with spaceship animation and signal propagation
- Modular JavaScript architecture with ES modules

## Physics Formulas

The Lorentz factor γ is calculated as:
γ = 1 / √(1 - v²/c²)

Where:
- v is the velocity as a fraction of c (speed of light)
- c is the speed of light

Time dilation:
- Traveler time (proper time): t_traveler = distance / v
- Earth time (dilated time): t_earth = t_traveler * γ
- Time difference: Δt = t_earth - t_traveler

Signal delay is the time for light to travel the distance: delay = distance (since c = 1 in these units)

## How to Run Locally

1. Ensure you have Python 3 installed
2. Navigate to the project directory
3. Run: `python3 -m http.server 8000`
4. Open your browser to `http://localhost:8000`

Alternatively, use any local server that serves static files (e.g., Node.js http-server, Apache, etc.)

## Project Structure

- `index.html`: Main HTML layout
- `style.css`: Dark theme styling
- `src/main.js`: Entry point and event connections
- `src/ui.js`: UI updates and interactions
- `src/physics.js`: Physics calculations
- `src/charts.js`: Chart.js graph rendering
- `src/visualization.js`: Canvas animation and visualization

## Future Improvements
- Unit conversion (years ↔ days, light-years ↔ km)
- Round-trip mission calculations
- More realistic starfield generation
- Audio effects for signal sending