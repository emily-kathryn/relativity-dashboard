import { updateUI, setMission, showTab } from './ui.js';
import { createChart } from './charts.js';
import { startVisualization, sendSignal } from './visualization.js';

window.sendSignal = sendSignal;
window.setMission = setMission;
window.showTab = showTab;

document.querySelectorAll("input").forEach(input => {
  input.addEventListener("input", updateUI);
});

// Tab buttons
document.getElementById("tab-planner").addEventListener("click", () => showTab("planner"));
document.getElementById("tab-graph").addEventListener("click", () => showTab("graph"));
document.getElementById("tab-visual").addEventListener("click", () => showTab("visual"));

// Preset buttons
document.querySelectorAll(".preset").forEach(button => {
  button.addEventListener("click", () => {
    const v = parseFloat(button.dataset.v);
    const d = parseFloat(button.dataset.d);
    setMission(v, d);
  });
});

// Send signal button
document.getElementById("send-signal").addEventListener("click", sendSignal);

updateUI();
createChart();
startVisualization();