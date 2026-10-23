const express = require("express");
const cors = require("cors");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use(express.static(path.join(__dirname, "../public")));

// =============================================================================
// ESTADO GLOBAL EN MEMORIA (Single Source of Truth en la Nube)
// =============================================================================
let state = {
  mode: "automatico",       // "automatico" | "manual"
  distanceCm: 0,            // Distancia reportada por el sensor ultrasónico HC-SR04
  motorState: "detenido",   // "derecha" | "izquierda" | "detenido"
  updatedAt: new Date().toISOString(),
  lastDeviceSeen: null      // Timestamp del último mensaje recibido de Arduino
};

// =============================================================================
// RUTAS PARA EL FRONTEND WEB (Compatibles con Bootstrap y Fetch)
// =============================================================================

// 1. Obtener estado para la página web
app.get(["/api/estado", "/api/status"], (req, res) => {
  const isOnline = state.lastDeviceSeen && (Date.now() - state.lastDeviceSeen < 8000);
  res.json({
    distancia: state.distanceCm,
    distanceCm: state.distanceCm,
    motor: state.motorState,
    motorState: state.motorState,
    modo: state.mode,
    mode: state.mode,
    deviceOnline: !!isOnline,
    updatedAt: state.updatedAt
  });
});

// 2. Cambiar modo (Vía GET estilo '/api/modo/manual' o POST con JSON)
app.get("/api/modo/:modo", (req, res) => {
  const modo = (req.params.modo || "").toLowerCase();
  if (["manual", "automatico", "automatic"].includes(modo)) {
    state.mode = (modo === "automatic" || modo === "automatico") ? "automatico" : "manual";
    if (state.mode === "manual") {
      state.motorState = "detenido"; // Por seguridad, al pasar a manual se detiene
    }
    state.updatedAt = new Date().toISOString();
    console.log(`[WEB] Modo cambiado a: ${state.mode}`);
    return res.json({ ok: true, modo: state.mode });
  }
  res.status(400).json({ error: "Modo no valido. Usa 'manual' o 'automatico'" });
});

app.post("/api/mode", (req, res) => {
  const { mode, modo } = req.body || {};
  const m = (mode || modo || "").toLowerCase();
  if (["manual", "automatico", "automatic"].includes(m)) {
    state.mode = (m === "automatic" || m === "automatico") ? "automatico" : "manual";
    if (state.mode === "manual") state.motorState = "detenido";
    state.updatedAt = new Date().toISOString();
    return res.json({ ok: true, modo: state.mode, mode: state.mode });
  }
  res.status(400).json({ error: "Modo no valido" });
});

// 3. Enviar comando de motor (Vía GET estilo '/api/motor/derecha' o POST con JSON)
app.get("/api/motor/:comando", (req, res) => {
  const cmd = (req.params.comando || "").toLowerCase();
  const validos = {
    "derecha": "derecha", "right": "derecha",
    "izquierda": "izquierda", "left": "izquierda",
    "detener": "detenido", "detenido": "detenido", "stop": "detenido"
  };

  if (!validos[cmd]) {
    return res.status(400).json({ error: "Comando invalido. Usa 'derecha', 'izquierda' o 'detener'" });
  }

  if (state.mode !== "manual") {
    return res.status(409).json({ error: "Cambia a modo manual antes de accionar el motor" });
  }

  state.motorState = validos[cmd];
  state.updatedAt = new Date().toISOString();
  console.log(`[WEB] Comando de motor: ${state.motorState}`);
  res.json({ ok: true, motor: state.motorState });
});

app.post("/api/motor", (req, res) => {
  const { command, comando } = req.body || {};
  const cmd = (command || comando || "").toLowerCase();
  const validos = {
    "derecha": "derecha", "right": "derecha",
    "izquierda": "izquierda", "left": "izquierda",
    "detener": "detenido", "detenido": "detenido", "stop": "detenido"
  };

  if (!validos[cmd]) {
    return res.status(400).json({ error: "Comando invalido" });
  }

  if (state.mode !== "manual") {
    return res.status(409).json({ error: "Cambia a modo manual primero" });
  }

  state.motorState = validos[cmd];
  state.updatedAt = new Date().toISOString();
  res.json({ ok: true, motor: state.motorState, motorState: state.motorState });
});

// =============================================================================
// RUTAS PARA EL CLIENTE IOT (ARDUINO UNO R4 WIFI)
// =============================================================================

// 4. Telemetría y sincronización:
// Arduino envía la distancia leída y su estado de motor (si está en automático).
// El servidor le responde con el modo actual y el comando manual a ejecutar.
app.post("/api/telemetry", (req, res) => {
  const body = req.body || {};
  const dist = body.distancia !== undefined ? body.distancia : body.distanceCm;
  const motor = body.motor || body.motorState;

  if (dist !== undefined && dist !== null && !isNaN(dist)) {
    state.distanceCm = Number(dist);
  }

  // Si el sistema está en automático, el servidor registra el movimiento calculado por el sensor
  if (state.mode === "automatico" && motor) {
    if (["derecha", "right"].includes(motor)) state.motorState = "derecha";
    else if (["izquierda", "left"].includes(motor)) state.motorState = "izquierda";
    else state.motorState = "detenido";
  }

  state.lastDeviceSeen = Date.now();
  state.updatedAt = new Date().toISOString();

  // Respuesta enviada al Arduino con la orden a cumplir
  res.json({
    ok: true,
    modo: state.mode,
    mode: state.mode,
    motor: state.motorState,
    command: state.motorState
  });
});

// 5. Endpoint GET alternativo para Arduino
app.get("/api/command", (req, res) => {
  state.lastDeviceSeen = Date.now();
  res.json({
    modo: state.mode,
    motor: state.motorState
  });
});

// =============================================================================
// INICIAR SERVIDOR
// =============================================================================
app.listen(PORT, () => {
  console.log(`=======================================================`);
  console.log(` Servidor Cloud API + Web (Opción 1 - EAI441)`);
  console.log(` Disponible en: http://localhost:${PORT}`);
  console.log(`=======================================================`);
});
