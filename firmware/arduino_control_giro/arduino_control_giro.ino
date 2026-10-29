/*
 * ==============================================================================
 * PROYECTO DE CÁTEDRA ELECTRÓNICA (EAI441) — ARDUINO UNO R4 WiFi
 * ==============================================================================
 * ARQUITECTURA: CLIENTE IoT EN LA NUBE (OPCIÓN 1 - CONTROL ESTRICTO 100%)
 *
 * • Modo Automático:
 *     - Funciona leyendo activamente las lecturas del sensor ultrasónico
 * HC-SR04.
 *     - 10 cm a 25 cm -> Gira a la DERECHA
 *     - 26 cm a 40 cm -> Gira a la IZQUIERDA
 *     - Fuera de rango (<10 cm o >40 cm) -> Se DETIENE
 * • Modo Manual:
 *     - NO USA LA LECTURA DEL SENSOR (el sensor no se mide ni se procesa).
 *     - El motor gira únicamente según los botones de la página web (Izquierda,
 * Detener, Derecha).
 * ==============================================================================
 */

#include "WiFiS3.h"
#if __has_include("FspTimer.h")
#include "FspTimer.h"
FspTimer motorTimer;
#endif

// --- 1. CONFIGURACIÓN DE RED WI-FI ---
#if __has_include("secrets.h")
#include "secrets.h"
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
#else
char ssid[] = "RedArduino"; // Coloca el nombre de tu red
char pass[] = "12345678";   // Coloca la contraseña de tu red
#endif

// --- 2. CONFIGURACIÓN DEL SERVIDOR EN LA NUBE (RENDER) ---
const char *serverHost = "proyectodecatedraelectronica-1.onrender.com";
const int serverPort = 443;
WiFiSSLClient client; // Conexión segura HTTPS persistente (Keep-Alive)

// --- 3. PINES DE HARDWARE ---
// Sensor Ultrasónico HC-SR04
const int PIN_TRIG = 12;
const int PIN_ECHO = 11;

// Puente H L293D
const int PIN_IN1 = 8; // Dirección A (Pin 2 del L293D)
const int PIN_IN2 = 7; // Dirección B (Pin 7 del L293D)
const int PIN_ENA =
    6; // Pin PWM opcional si Enable (Pin 1 del L293D) se conecta a pin 6

// --- 4. VELOCIDAD DEL MOTOR (MODULACIÓN PWM SUAVE) ---
// Rango: 0 (detenido) a 255 (velocidad máxima).
// 100 (~39% de potencia) reduce notablemente la velocidad de giro manteniendo
// buen torque.
const int VELOCIDAD_MOTOR = 50;
const unsigned long PERIODO_PWM_US =
    10000UL; // Periodo PWM de 10 ms (100 Hz) para giro lento y suave
volatile unsigned long impulsoArranqueHasta = 0;

// --- 5. VARIABLES DE ESTADO ---
String modoActual = "automatico";  // "automatico" o "manual"
String comandoManual = "detenido"; // "derecha", "izquierda" o "detenido"
String estadoMotor = "detenido";
long distanciaActual = 0;
long ultimaDistanciaEnviada = -999;
String ultimoMotorEnviado = "";

// Temporizadores no bloqueantes de respuesta instantánea
unsigned long ultimoMuestreoSensor = 0;
unsigned long ultimaSincronizacionAPI = 0;
unsigned long ultimoLogSerial = 0;

const unsigned long INTERVALO_SENSOR_MS =
    25; // Evaluar sensor y motor cada 25 ms (instantáneo)
const unsigned long INTERVALO_API_MS =
    60; // Sincronizar con Render cada 60 ms vía Keep-Alive

// ==========================================
// CONTROL DE VELOCIDAD PWM Y GIRO DEL MOTOR
// ==========================================
void actualizarPWM() {
  if (estadoMotor == "detenido") {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    return;
  }

  unsigned long fase = micros() % PERIODO_PWM_US;
  unsigned long tiempoEncendido =
      ((unsigned long)VELOCIDAD_MOTOR * PERIODO_PWM_US) / 255UL;
  // Breve impulso de 20 ms al arrancar desde detenido para vencer la inercia y
  // luego girar lento
  bool pulsoActivo =
      (millis() < impulsoArranqueHasta) || (fase < tiempoEncendido);

  if (estadoMotor == "derecha") {
    digitalWrite(PIN_IN2, LOW);
    digitalWrite(PIN_IN1, pulsoActivo ? HIGH : LOW);
  } else if (estadoMotor == "izquierda") {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, pulsoActivo ? HIGH : LOW);
  }
}

#if __has_include("FspTimer.h")
void motorTimerCallback(timer_callback_args_t __attribute((unused)) * p_args) {
  actualizarPWM();
}
#endif

void girarDerecha() {
  if (estadoMotor != "derecha") {
    impulsoArranqueHasta = millis() + 20;
    estadoMotor = "derecha";
  }
  analogWrite(PIN_ENA, VELOCIDAD_MOTOR);
  actualizarPWM();
}

void girarIzquierda() {
  if (estadoMotor != "izquierda") {
    impulsoArranqueHasta = millis() + 20;
    estadoMotor = "izquierda";
  }
  analogWrite(PIN_ENA, VELOCIDAD_MOTOR);
  actualizarPWM();
}

void detener() {
  estadoMotor = "detenido";
  analogWrite(PIN_ENA, 0);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
}

// ==========================================
// MEDICIÓN ULTRA RÁPIDA DE DISTANCIA (HC-SR04)
// ==========================================
long medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // Timeout corto de 5000 µs (~85 cm máx): como el rango activo es 10-40 cm,
  // responde en menos de 2.5 ms sin bloquear jamás el microcontrolador.
  long duracion = pulseIn(PIN_ECHO, HIGH, 5000UL);
  if (duracion == 0)
    return 999; // Sin obstáculo cercano
  return duracion * 0.034 / 2;
}

// ==========================================
// EVALUACIÓN LOCAL INSTANTÁNEA DEL MOTOR
// ==========================================
void actualizarControlLocal() {
  if (modoActual == "automatico") {
    distanciaActual = medirDistancia();

    if (distanciaActual >= 10 && distanciaActual <= 25) {
      girarDerecha();
    } else if (distanciaActual >= 26 && distanciaActual <= 40) {
      girarIzquierda();
    } else {
      detener();
    }
  } else {
    distanciaActual = -1;

    if (comandoManual == "derecha") {
      girarDerecha();
    } else if (comandoManual == "izquierda") {
      girarIzquierda();
    } else {
      detener();
    }
  }
  actualizarPWM();
}

// ==========================================
// CONEXIÓN WI-FI
// ==========================================
void verificarWiFi() {
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.print("[WiFi] Conectando a: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 8000) {
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Conectado exitosamente.");
    Serial.print("[WiFi] IP local: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Reintentando conexion...");
  }
}

// ==========================================
// PARSEO ROBUSTO DE LA RESPUESTA DE LA NUBE
// ==========================================
void procesarRespuestaServidor(const String &body) {
  // 1. Extraer Modo ("manual" o "automatico") de forma infalible
  if (body.indexOf("\"modo\":\"manual\"") != -1 ||
      body.indexOf("\"modo\": \"manual\"") != -1 ||
      body.indexOf("\"mode\":\"manual\"") != -1 ||
      body.indexOf("\"mode\": \"manual\"") != -1) {
    modoActual = "manual";
  } else if (body.indexOf("\"modo\":\"automatico\"") != -1 ||
             body.indexOf("\"modo\": \"automatico\"") != -1 ||
             body.indexOf("\"mode\":\"automatic\"") != -1 ||
             body.indexOf("\"mode\": \"automatic\"") != -1) {
    modoActual = "automatico";
  }

  // 2. Extraer Comando Manual ("derecha", "izquierda", "detenido")
  if (body.indexOf("\"motor\":\"derecha\"") != -1 ||
      body.indexOf("\"motor\": \"derecha\"") != -1 ||
      body.indexOf("\"command\":\"derecha\"") != -1 ||
      body.indexOf("\"command\":\"right\"") != -1) {
    comandoManual = "derecha";
  } else if (body.indexOf("\"motor\":\"izquierda\"") != -1 ||
             body.indexOf("\"motor\": \"izquierda\"") != -1 ||
             body.indexOf("\"command\":\"izquierda\"") != -1 ||
             body.indexOf("\"command\":\"left\"") != -1) {
    comandoManual = "izquierda";
  } else if (body.indexOf("\"motor\":\"detenido\"") != -1 ||
             body.indexOf("\"motor\": \"detenido\"") != -1 ||
             body.indexOf("\"command\":\"detenido\"") != -1 ||
             body.indexOf("\"command\":\"stop\"") != -1) {
    comandoManual = "detenido";
  }

  // 3. Si estamos en modo manual, aplicar de inmediato el comando de giro
  if (modoActual == "manual") {
    if (comandoManual == "derecha")
      girarDerecha();
    else if (comandoManual == "izquierda")
      girarIzquierda();
    else
      detener();
  }
}

// ==========================================
// SINCRONIZACIÓN INSTANTÁNEA CON RENDER (KEEP-ALIVE)
// ==========================================
bool enviarPeticionKeepAlive(const String &jsonBody) {
  // Reutilizar conexión TLS abierta para evitar el handshake de 1.5s en cada
  // envío
  if (!client.connected()) {
    client.stop();
    if (!client.connect(serverHost, serverPort)) {
      return false;
    }
  }

  // Limpiar cualquier byte residual del buffer de entrada
  while (client.available()) {
    client.read();
  }

  // Construir y enviar toda la petición en un solo paquete TCP/SSL (mínima
  // latencia)
  String peticion = "POST /api/telemetry HTTP/1.1\r\n";
  peticion += "Host: ";
  peticion += serverHost;
  peticion += "\r\nContent-Type: application/json\r\n";
  peticion += "Connection: keep-alive\r\n";
  peticion += "Content-Length: ";
  peticion += String(jsonBody.length());
  peticion += "\r\n\r\n";
  peticion += jsonBody;

  client.print(peticion);

  // Leer respuesta sin bloquear el PWM ni el control local del motor
  unsigned long timeout = millis() + 900;
  String respuesta = "";
  respuesta.reserve(256);

  while (client.connected() && millis() < timeout) {
    actualizarPWM();

    while (client.available()) {
      char c = client.read();
      respuesta += c;
    }

    // Salir en el instante exacto en que llega el cierre del JSON de
    // confirmación
    int posOk = respuesta.indexOf("\"ok\":true");
    if (posOk != -1 && respuesta.indexOf("}", posOk) != -1) {
      break;
    }

    // Mantener el sensor y motor reactivos incluso mientras viaja el paquete de
    // red
    if (millis() - ultimoMuestreoSensor >= INTERVALO_SENSOR_MS) {
      ultimoMuestreoSensor = millis();
      actualizarControlLocal();
    }
    delayMicroseconds(400);
  }

  // Drenar caracteres finales de salto de línea (\r\n) si quedaron en el buffer
  unsigned long tDrain = millis() + 5;
  while (client.available() && millis() < tDrain) {
    respuesta += (char)client.read();
  }

  if (respuesta.indexOf("Connection: close") != -1 ||
      respuesta.indexOf("connection: close") != -1) {
    client.stop();
  }

  if (respuesta.length() > 0 && respuesta.indexOf("\"ok\":true") != -1) {
    procesarRespuestaServidor(respuesta);
    return true;
  }

  // Si el servidor cerró el socket Keep-Alive por inactividad, cerramos para
  // reconectar
  client.stop();
  return false;
}

void sincronizarConAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    verificarWiFi();
    return;
  }

  // Armar cuerpo JSON compacto
  String jsonBody = "{\"modo\":\"";
  jsonBody += modoActual;
  jsonBody += "\",\"motor\":\"";
  jsonBody += estadoMotor;
  jsonBody += "\"";

  if (modoActual == "automatico") {
    jsonBody += ",\"distancia\":";
    jsonBody += String(distanciaActual);
  } else {
    jsonBody += ",\"distancia\":null";
  }
  jsonBody += "}";

  // Enviar por socket persistente; si el socket previo expiró, reintenta 1 vez
  // al instante
  if (!enviarPeticionKeepAlive(jsonBody)) {
    enviarPeticionKeepAlive(jsonBody);
  }

  ultimaDistanciaEnviada = distanciaActual;
  ultimoMotorEnviado = estadoMotor;
}

// ==========================================
// SETUP INICIAL
// ==========================================
void setup() {
  Serial.begin(9600);
  delay(200);

  Serial.println("==================================================");
  Serial.println(" ARDUINO UNO R4 WiFi — CONTROL INSTANTÁNEO Y PWM");
  Serial.println("==================================================");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_ENA, OUTPUT);

  detener();

  // Configurar Timer por Hardware (2000 Hz) en Arduino UNO R4 para que el PWM
  // regule la velocidad del motor continuamente sin interrupciones de red
#if __has_include("FspTimer.h")
  uint8_t timer_type = GPT_TIMER;
  int8_t tindex = FspTimer::get_available_timer(timer_type);
  if (tindex < 0) {
    tindex = FspTimer::get_available_timer(timer_type, true);
  }
  if (tindex >= 0) {
    if (motorTimer.begin(TIMER_MODE_PERIODIC, timer_type, tindex, 2000.0f, 0.0f,
                         motorTimerCallback)) {
      if (motorTimer.setup_overflow_irq()) {
        if (motorTimer.open()) {
          motorTimer.start();
        }
      }
    }
  }
#endif

  verificarWiFi();
}

// ==========================================
// BUCLE PRINCIPAL
// ==========================================
void loop() {
  actualizarPWM();
  unsigned long tiempoActual = millis();

  // 1. CONTROL Y ACCIONAMIENTO INSTANTÁNEO DEL MOTOR (Cada 25 ms)
  if (tiempoActual - ultimoMuestreoSensor >= INTERVALO_SENSOR_MS) {
    ultimoMuestreoSensor = tiempoActual;
    actualizarControlLocal();

    // Si cambió el estado del motor o la distancia varió >= 2 cm, sincronizar
    // de inmediato
    bool cambioImportante =
        (estadoMotor != ultimoMotorEnviado) ||
        (modoActual == "automatico" &&
         abs(distanciaActual - ultimaDistanciaEnviada) >= 2);
    if (cambioImportante) {
      ultimaSincronizacionAPI = tiempoActual;
      sincronizarConAPI();
    }
  }

  // 2. SINCRONIZACIÓN CONTINUA CON RENDER (Cada 60 ms vía Keep-Alive)
  if (tiempoActual - ultimaSincronizacionAPI >= INTERVALO_API_MS) {
    ultimaSincronizacionAPI = tiempoActual;
    sincronizarConAPI();
  }

  // 3. MONITOR SERIE SIN BLOQUEAR EL BUCLE (Cada 300 ms)
  if (tiempoActual - ultimoLogSerial >= 300) {
    ultimoLogSerial = tiempoActual;
    if (modoActual == "automatico") {
      Serial.print("[AUTO] Dist: ");
      Serial.print(distanciaActual);
      Serial.print(" cm | Motor: ");
      Serial.print(estadoMotor);
      Serial.print(" (PWM: ");
      Serial.print(VELOCIDAD_MOTOR);
      Serial.println("/255)");
    } else {
      Serial.print("[MANUAL] Boton Web: ");
      Serial.print(comandoManual);
      Serial.print(" | Motor: ");
      Serial.print(estadoMotor);
      Serial.print(" (PWM: ");
      Serial.print(VELOCIDAD_MOTOR);
      Serial.println("/255)");
    }
  }
}
