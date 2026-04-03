import { gamma } from './physics.js';

const canvas = document.getElementById("spaceCanvas");
const ctx = canvas.getContext("2d");

// Simulation state
let shipX = 100;
let signalX = null;
let time = 0;

// Starfield
const stars = Array.from({ length: 100 }, () => ({
  x: Math.random() * canvas.width,
  y: Math.random() * canvas.height
}));

// Start animation loop
export function startVisualization() {
  requestAnimationFrame(loop);
}

// Main loop
function loop() {
  update(0.02);
  draw();
  requestAnimationFrame(loop);
}

// Update simulation
function update(dt) {
  const v = parseFloat(document.getElementById("velocity").value);
  const d = parseFloat(document.getElementById("distance").value);

  if (!v || !d || v >= 1) return;

  time += dt;

  // Ship speed scaled for screen
  const speed = v * 200;
  shipX += speed * dt;

  // Reset when reaching destination
  if (shipX > 700) {
    shipX = 100;
    time = 0;
    signalX = null;
  }

  // Signal moves at light speed
  if (signalX !== null) {
    signalX += 300 * dt;
    if (signalX > shipX) {
      signalX = null;
    }
  }
}

// Draw everything
function draw() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Background
  ctx.fillStyle = "black";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // Starfield
  ctx.fillStyle = "white";
  stars.forEach(s => ctx.fillRect(s.x, s.y, 1, 1));

  // Earth
  ctx.fillStyle = "blue";
  ctx.beginPath();
  ctx.arc(100, 200, 20, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillText("Earth", 80, 240);

  // Destination
  ctx.fillStyle = "gray";
  ctx.beginPath();
  ctx.arc(700, 200, 15, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillText("Target", 680, 240);

  // Ship
  ctx.fillStyle = "orange";
  ctx.fillRect(shipX, 190, 20, 10);
  ctx.fillText("Ship", shipX, 180);

  // Signal (light pulse)
  if (signalX !== null) {
    ctx.fillStyle = "yellow";
    ctx.beginPath();
    ctx.arc(signalX, 200, 5, 0, Math.PI * 2);
    ctx.fill();
  }

  // Time calculations
  const v = parseFloat(document.getElementById("velocity").value);

  if (!v || v >= 1) return;

  const g = gamma(v);

  const earthTime = time;
  const travelerTime = time / g;

  // Display
  ctx.fillStyle = "white";
  ctx.font = "14px Arial";

  ctx.fillText("Velocity: " + v.toFixed(2) + "c", 20, 20);
  ctx.fillText("Gamma: " + g.toFixed(3), 20, 40);

  ctx.fillText("Earth Time: " + earthTime.toFixed(2), 20, 70);
  ctx.fillText("Ship Time: " + travelerTime.toFixed(2), 20, 90);

  ctx.fillText("Time Difference: " + (earthTime - travelerTime).toFixed(2), 20, 110);
}

// Trigger signal from Earth
export function sendSignal() {
  signalX = 100;
}