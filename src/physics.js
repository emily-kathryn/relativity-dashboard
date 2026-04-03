export function gamma(v) {
  return 1 / Math.sqrt(1 - v * v);
}

export function calculate(v, d) {
  const g = gamma(v);
  const tEarth = d / v;
  const tTraveler = tEarth / g;

  return {
    gamma: g,
    earth: tEarth,
    traveler: tTraveler,
    diff: tEarth - tTraveler,
    signal: d
  };
}