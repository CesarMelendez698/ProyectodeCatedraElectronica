const API_BASE = ""; // Vacío = API en el mismo servidor. Cambia a "http://IP_DEL_DISPOSITIVO:3000" si aplica.
const $ = (id) => document.getElementById(id);
let mode = "manual";
let currentDistance = null;
let toastTimer;

function notify(message) {
  const el = $("toast");
  el.textContent = message;
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove("show"), 2600);
}
function setModeUI(next) {
  mode = next;
  $("manualMode").classList.toggle("active", mode === "manual");
  $("automaticMode").classList.toggle("active", mode === "automatic");
  $("heroMode").textContent = mode === "manual" ? "Control manual activo" : "Control automático activo";
  $("controlTag").textContent = mode === "manual" ? "MANUAL" : "AUTOMÁTICO";
  $("modeDescription").textContent = mode === "manual"
    ? "Controla el motor con los botones de navegación."
    : "El sensor decide el giro según la distancia medida.";
  $("controlNote").textContent = mode === "manual"
    ? "En modo manual, el motor responde a los comandos enviados desde esta página."
    : "Los botones manuales quedan bloqueados mientras el control automático está activo.";
  ["leftBtn","rightBtn","stopBtn"].forEach(id => $(id).disabled = mode !== "manual");
}
function renderDistance(cm) {
  currentDistance = Number(cm);
  if (!Number.isFinite(currentDistance) || currentDistance < 0) return;
  $("distanceValue").textContent = currentDistance.toFixed(1);
  $("distanceBar").style.width = `${Math.max(0, Math.min(100, currentDistance / 50 * 100))}%`;
  let hint = "Fuera de los rangos de giro";
  if (currentDistance >= 10 && currentDistance <= 25) hint = "Objeto detectado: giro a la derecha";
  else if (currentDistance >= 26 && currentDistance <= 40) hint = "Objeto detectado: giro a la izquierda";
  else if (currentDistance < 10) hint = "Objeto demasiado cerca: motor detenido";
  $("distanceHint").textContent = hint;
}
function renderMotor(state) {
  const normalized = String(state || "detenido").toLowerCase();
  let label = "Detenido", icon = "Ⅱ", caption = "Sin movimiento", color = "var(--orange)";
  if (normalized.includes("derecha") || normalized === "right") {
    label = "A la derecha"; icon = "↻"; caption = "Giro a la derecha"; color = "var(--cyan)";
  } else if (normalized.includes("izquierda") || normalized === "left") {
    label = "A la izquierda"; icon = "↺"; caption = "Giro a la izquierda"; color = "var(--blue)";
  } else if (normalized.includes("error")) {
    label = "Error"; caption = "Revisa la conexión"; color = "var(--red)";
  }
  $("motorState").textContent = label;
  $("motorIcon").textContent = icon;
  $("motorHint").textContent = label === "Detenido" ? "El motor está en reposo" : "Comando de movimiento activo";
  $("motorStateCaption").textContent = caption;
  $("motorStateDot").style.background = color;
}
async function requestJSON(path, options = {}) {
  const response = await fetch(`${API_BASE}${path}`, {
    ...options,
    headers: { "Content-Type": "application/json", ...(options.headers || {}) }
  });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}
async function sendMode(next) {
  try {
    await requestJSON("/api/mode", { method: "POST", body: JSON.stringify({ mode: next }) });
    setModeUI(next);
    notify(`Modo ${next === "manual" ? "manual" : "automático"} activado`);
    await refreshStatus();
  } catch (e) {
    // La demo local funciona aunque todavía no se haya iniciado la API.
    setModeUI(next);
    notify("Modo actualizado en la interfaz. Inicia la API para conectar el dispositivo.");
  }
}
async function sendCommand(command) {
  if (mode !== "manual") return notify("Cambia a modo manual para usar los botones.");
  try {
    const result = await requestJSON("/api/motor", { method: "POST", body: JSON.stringify({ command }) });
    renderMotor(result.motorState || command);
    notify("Comando enviado al dispositivo");
  } catch (e) {
    renderMotor(command);
    notify("Vista de demostración: inicia la API para accionar el motor real.");
  }
}
async function refreshStatus() {
  try {
    const data = await requestJSON("/api/status");
    $("connectionText").textContent = "API conectada";
    $("connectionDot").style.background = "var(--cyan)";
    if (data.distanceCm !== undefined && data.distanceCm !== null) renderDistance(data.distanceCm);
    if (data.motorState) renderMotor(data.motorState);
    if (data.mode && data.mode !== mode) setModeUI(data.mode);
    $("lastUpdate").textContent = `Actualizado: ${new Date().toLocaleTimeString()}`;
  } catch (e) {
    $("connectionText").textContent = "Modo demostración";
    $("connectionDot").style.background = "var(--orange)";
    $("lastUpdate").textContent = "API no conectada · interfaz en demostración";
  }
}
$("manualMode").addEventListener("click", () => sendMode("manual"));
$("automaticMode").addEventListener("click", () => sendMode("automatic"));
$("leftBtn").addEventListener("click", () => sendCommand("left"));
$("rightBtn").addEventListener("click", () => sendCommand("right"));
$("stopBtn").addEventListener("click", () => sendCommand("stop"));
setModeUI("manual");
renderMotor("stop");
refreshStatus();
setInterval(refreshStatus, 1000);
