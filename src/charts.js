export function createChart() {
  const ctx = document.getElementById('chart');

  const velocities = [];
  const differences = [];

  for (let v = 0.01; v < 0.99; v += 0.01) {
    let g = 1 / Math.sqrt(1 - v * v);
    let tEarth = 10 / v;
    let tTraveler = tEarth / g;

    velocities.push(v.toFixed(2));
    differences.push(tEarth - tTraveler);
  }

  new Chart(ctx, {
    type: 'line',
    data: {
      labels: velocities,
      datasets: [{
        label: 'Time Difference',
        data: differences
      }]
    },
    options: {
      responsive: true,
      plugins: {
        legend: {
          labels: { color: 'white' }
        }
      },
      scales: {
        x: {
          ticks: { color: 'white' }
        },
        y: {
          ticks: { color: 'white' }
        }
      }
    }
  });
}