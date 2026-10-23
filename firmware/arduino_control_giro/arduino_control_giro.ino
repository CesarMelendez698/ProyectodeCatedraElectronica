/*
 * ==============================================================================
 * PROYECTO DE CÁTEDRA ELECTRÓNICA (EAI441) — ARDUINO UNO R4 WiFi
 * ==============================================================================
 * ARQUITECTURA: CLIENTE IoT EN LA NUBE (OPCIÓN 1 - 100% OPTIMIZADO)
 * 
 * • Lectura ultrasónica en tiempo real y sin bloqueos (HC-SR04).
 * • Modo Automático:
 *     - 10 cm a 25 cm -> Gira a la DERECHA
 *     - 26 cm a 40 cm -> Gira a la IZQUIERDA
 *     - Fuera de rango (<10 cm o >40 cm) -> Se DETIENE
 * • Modo Manual:
 *     - El motor obedece en tiempo real a los botones de la página web.
 * • Sincronización continua con la API REST vía HTTP / WiFiS3.
 * ==============================================================================
 */

#include "WiFiS3.h"

// --- 1. CONFIGURACIÓN DE RED WI-FI ---
#if __has_include("secrets.h")
  #include "secrets.h"
  char ssid[] = SECRET_SSID;
  char pass[] = SECRET_PASS;
#else
  char ssid[] = "RedArduino";  // Coloca el nombre de tu red
  char pass[] = "12345678";      // Coloca la contraseña de tu red
#endif

// --- 2. CONFIGURACIÓN DEL SERVIDOR EN LA NUBE (RENDER) ---
const char* serverHost = "proyectodecatedraelectronica.onrender.com";
const int   serverPort = 443;
WiFiSSLClient client; // Conexión segura HTTPS para Render

// --- 3. PINES DE HARDWARE ---
// Sensor Ultrasónico HC-SR04
const int PIN_TRIG = 12;
const int PIN_ECHO = 11;

// Puente H L293D
const int PIN_IN1  = 8;  // Dirección A (Pin 2 del L293D)
const int PIN_IN2  = 7;  // Dirección B (Pin 7 del L293D)

// --- 4. VARIABLES DE ESTADO ---
String modoActual = "automatico";    // "automatico" o "manual"
String comandoManual = "detenido";   // "derecha", "izquierda" o "detenido"
String estadoMotor = "detenido";
long distanciaActual = 0;

// Temporizadores no bloqueantes
unsigned long ultimoMuestreoSensor = 0;
unsigned long ultimaSincronizacionAPI = 0;

const unsigned long INTERVALO_SENSOR_MS = 60;   // Medir distancia cada 60 ms
const unsigned long INTERVALO_API_MS    = 350;  // Sincronizar con la API cada 350 ms

// ==========================================
// CONTROL DEL MOTOR
// ==========================================
void girarDerecha() {
  digitalWrite(PIN_IN1, HIGH);
  digitalWrite(PIN_IN2, LOW);
  estadoMotor = "derecha";
}

void girarIzquierda() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, HIGH);
  estadoMotor = "izquierda";
}

void detener() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  estadoMotor = "detenido";
}

// ==========================================
// MEDICIÓN RÁPIDA DE DISTANCIA
// ==========================================
long medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  
  // Timeout optimizado a 12000 microsegundos (~2 metros máx).
  long duracion = pulseIn(PIN_ECHO, HIGH, 12000UL);
  if (duracion == 0) return 999; // Fuera de rango
  return duracion * 0.034 / 2;
}

// ==========================================
// CONEXIÓN WI-FI SEGURA Y AUTOMÁTICA
// ==========================================
void verificarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WiFi] Conectando a: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 6000) {
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Conectado exitosamente.");
    Serial.print("[WiFi] IP asignada: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] En espera de conexión...");
  }
}

// ==========================================
// PARSEO RÁPIDO DE LA RESPUESTA DE LA API
// ==========================================
void procesarRespuestaServidor(const String& body) {
  // 1. Extraer modo ("automatico" o "manual")
  int posModo = body.indexOf("\"modo\":\"");
  if (posModo != -1) {
    int inicio = posModo + 8;
    int fin = body.indexOf("\"", inicio);
    if (fin != -1) {
      modoActual = body.substring(inicio, fin);
    }
  }

  // 2. Extraer comando del motor ("derecha", "izquierda", "detenido")
  int posMotor = body.indexOf("\"motor\":\"");
  if (posMotor != -1) {
    int inicio = posMotor + 9;
    int fin = body.indexOf("\"", inicio);
    if (fin != -1) {
      comandoManual = body.substring(inicio, fin);
    }
  }

  // 3. Si está en modo manual, accionar el motor según la orden web
  if (modoActual == "manual") {
    if (comandoManual == "derecha") girarDerecha();
    else if (comandoManual == "izquierda") girarIzquierda();
    else detener();
  }
}

// ==========================================
// COMUNICACIÓN CON LA API (SIN BLOQUEOS)
// ==========================================
void sincronizarConAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    verificarWiFi();
    return;
  }

  if (!client.connect(serverHost, serverPort)) {
    client.stop();
    return;
  }

  // Construir cuerpo JSON
  String jsonBody = "{\"distancia\":";
  jsonBody += String(distanciaActual);
  jsonBody += ",\"motor\":\"";
  jsonBody += estadoMotor;
  jsonBody += "\"}";

  // Enviar petición HTTP POST
  client.println("POST /api/telemetry HTTP/1.1");
  client.print("Host: ");
  client.println(serverHost);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(jsonBody.length());
  client.println("Connection: close");
  client.println();
  client.println(jsonBody);

  // Leer respuesta con timeout corto (máximo 400ms)
  unsigned long t0 = millis();
  while (client.connected() && (millis() - t0 < 400)) {
    if (client.available()) {
      String linea = client.readStringUntil('\n');
      if (linea == "\r" || linea.length() == 0) {
        String respuesta = client.readString();
        procesarRespuestaServidor(respuesta);
        break;
      }
    }
  }

  client.stop();
}

// ==========================================
// SETUP INICIAL
// ==========================================
void setup() {
  Serial.begin(9600);
  delay(300);

  Serial.println("==================================================");
  Serial.println(" ARDUINO UNO R4 WiFi — SISTEMA DE CONTROL DE GIRO");
  Serial.println("==================================================");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  
  detener();
  verificarWiFi();
}

// ==========================================
// BUCLE PRINCIPAL (Tiempo Real Continuo)
// ==========================================
void loop() {
  unsigned long tiempoActual = millis();

  // 1. LECTURA DEL SENSOR Y CONTROL AUTOMÁTICO (Cada 60 ms)
  if (tiempoActual - ultimoMuestreoSensor >= INTERVALO_SENSOR_MS) {
    ultimoMuestreoSensor = tiempoActual;
    
    distanciaActual = medirDistancia();

    // Solo si estamos en Modo Automático el sensor decide el giro:
    if (modoActual == "automatico") {
      if (distanciaActual >= 10 && distanciaActual <= 25) {
        girarDerecha();
      } else if (distanciaActual >= 26 && distanciaActual <= 40) {
        girarIzquierda();
      } else {
        detener();
      }
    }

    // Diagnóstico en Monitor Serie
    Serial.print("Distancia: ");
    Serial.print(distanciaActual);
    Serial.print(" cm | Modo: ");
    Serial.print(modoActual);
    Serial.print(" | Motor: ");
    Serial.println(estadoMotor);
  }

  // 2. SINCRONIZACIÓN CON LA API (Cada 350 ms)
  if (tiempoActual - ultimaSincronizacionAPI >= INTERVALO_API_MS) {
    ultimaSincronizacionAPI = tiempoActual;
    sincronizarConAPI();
  }
}
