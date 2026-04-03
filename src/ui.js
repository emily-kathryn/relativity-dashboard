import { calculate } from './physics.js';

export function updateUI() {
  const v = parseFloat(document.getElementById("velocity").value);
  const d = parseFloat(document.getElementById("distance").value);

  if (v <= 0 || v >= 1 || !d) return;

  const result = calculate(v, d);

  document.getElementById("gamma").innerText = result.gamma.toFixed(3);
  document.getElementById("earthTime").innerText = result.earth.toFixed(3);
  document.getElementById("travelerTime").innerText = result.traveler.toFixed(3);
  document.getElementById("difference").innerText = result.diff.toFixed(3);
  document.getElementById("signal").innerText = result.signal.toFixed(3);
}

export function setMission(v, d) {
  document.getElementById("velocity").value = v;
  document.getElementById("distance").value = d;
  updateUI();
}

export function showTab(name) {
  document.getElementById("planner").style.display = "none";
  document.getElementById("graph").style.display = "none";
  document.getElementById("visual").style.display = "none";

  document.getElementById(name).style.display = "block";
}