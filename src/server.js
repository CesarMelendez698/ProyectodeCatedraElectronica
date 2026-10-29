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

// Clientes suscritos a eventos en tiempo real (Server-Sent Events - Latencia 0 ms)
const sseClients = new Set();

function construirPayloadEstado() {
  const isOnline = state.lastDeviceSeen && (Date.now() - state.lastDeviceSeen < 8000);
  const dist = state.mode === "automatico" ? state.distanceCm : null;
  return {
    distancia: dist,
    distanceCm: dist,
    motor: state.motorState,
    motorState: state.motorState,
    modo: state.mode,
    mode: state.mode,
    deviceOnline: !!isOnline,
    updatedAt: state.updatedAt
  };
}

function notificarClientesInstantaneo() {
  if (sseClients.size === 0) return;
  const data = `data: ${JSON.stringify(construirPayloadEstado())}\n\n`;
  for (const clientRes of sseClients) {
    try {
      clientRes.write(data);
    } catch (_) {
      sseClients.delete(clientRes);
    }
  }
}

// Evitar caché en todas las rutas de la API para respuesta instantánea
app.use("/api", (req, res, next) => {
  res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate");
  res.setHeader("Pragma", "no-cache");
  res.setHeader("Expires", "0");
  next();
});

// =============================================================================
// RUTAS PARA EL FRONTEND WEB (Compatibles con Bootstrap, SSE y Fetch)
// =============================================================================

// 0. Canal de Eventos en Tiempo Real (SSE) para actualización instantánea (0 ms)
app.get("/api/stream", (req, res) => {
  res.setHeader("Content-Type", "text/event-stream");
  res.setHeader("Cache-Control", "no-cache, no-transform");
  res.setHeader("Connection", "keep-alive");
  res.setHeader("X-Accel-Buffering", "no");
  if (res.flushHeaders) res.flushHeaders();

  // Enviar estado inicial de inmediato
  res.write(`data: ${JSON.stringify(construirPayloadEstado())}\n\n`);
  sseClients.add(res);

  req.on("close", () => {
    sseClients.delete(res);
  });
});

// 1. Obtener estado para la página web
app.get(["/api/estado", "/api/status"], (req, res) => {
  res.json(construirPayloadEstado());
});

// 2. Cambiar modo (Vía GET estilo '/api/modo/manual' o POST con JSON)
app.get("/api/modo/:modo", (req, res) => {
  const modo = (req.params.modo || "").toLowerCase();
  if (["manual", "automatico", "automatic"].includes(modo)) {
    state.mode = (modo === "automatic" || modo === "automatico") ? "automatico" : "manual";
    if (state.mode === "manual") {
      state.motorState = "detenido"; // Por seguridad, al pasar a manual se detiene
      state.distanceCm = null;       // En manual no se usa la lectura del sensor
    }
    state.updatedAt = new Date().toISOString();
    notificarClientesInstantaneo();
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
    if (state.mode === "manual") {
      state.motorState = "detenido";
      state.distanceCm = null;
    }
    state.updatedAt = new Date().toISOString();
    notificarClientesInstantaneo();
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
  notificarClientesInstantaneo();
  console.log(`[WEB] Comando de motor manual: ${state.motorState}`);
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
  notificarClientesInstantaneo();
  res.json({ ok: true, motor: state.motorState, motorState: state.motorState });
});

// =============================================================================
// RUTAS PARA EL CLIENTE IOT (ARDUINO UNO R4 WIFI)
// =============================================================================

// 4. Telemetría y sincronización:
// Arduino envía la distancia leída y su estado de motor (en modo automático).
// En modo manual el sensor no se usa y el servidor responde con el comando manual de los botones web.
app.post("/api/telemetry", (req, res) => {
  const body = req.body || {};
  const dist = body.distancia !== undefined ? body.distancia : body.distanceCm;

  // MODO AUTOMÁTICO: Funciona leyendo las lecturas del sensor para gobernar el motor
  if (state.mode === "automatico") {
    if (dist !== undefined && dist !== null && !isNaN(dist) && Number(dist) >= 0) {
      state.distanceCm = Number(dist);
      if (state.distanceCm >= 10 && state.distanceCm <= 25) {
        state.motorState = "derecha";
      } else if (state.distanceCm >= 26 && state.distanceCm <= 40) {
        state.motorState = "izquierda";
      } else {
        state.motorState = "detenido";
      }
    }
  } else {
    // MODO MANUAL: NO se usa la lectura del sensor ultrasónico.
    // El motor NO se altera con la telemetría, responde solo a los botones web.
    state.distanceCm = null;
  }

  state.lastDeviceSeen = Date.now();
  state.updatedAt = new Date().toISOString();
  notificarClientesInstantaneo();

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
  notificarClientesInstantaneo();
  res.json({
    modo: state.mode,
    motor: state.motorState
  });
});

// =============================================================================
// INICIAR SERVIDOR CON SOPORTE KEEP-ALIVE PERSISTENTE
// =============================================================================
const server = app.listen(PORT, () => {
  console.log(`=======================================================`);
  console.log(` Servidor Cloud API + Web (Opción 1 - EAI441)`);
  console.log(` Disponible en: http://localhost:${PORT}`);
  console.log(`=======================================================`);
});

server.keepAliveTimeout = 65000;
server.headersTimeout = 66000;
