/*
 * ==============================================================================
 * PROYECTO DE CÁTEDRA ELECTRÓNICA (EAI441) — ARDUINO UNO R4 WiFi
 * ==============================================================================
 * ARQUITECTURA: CLIENTE IoT EN LA NUBE (OPCIÓN 1 - CONTROL ESTRICTO 100%)
 * 
 * • Modo Automático:
 *     - Funciona leyendo activamente las lecturas del sensor ultrasónico HC-SR04.
 *     - 10 cm a 25 cm -> Gira a la DERECHA
 *     - 26 cm a 40 cm -> Gira a la IZQUIERDA
 *     - Fuera de rango (<10 cm o >40 cm) -> Se DETIENE
 * • Modo Manual:
 *     - NO USA LA LECTURA DEL SENSOR (el sensor no se mide ni se procesa).
 *     - El motor gira únicamente según los botones de la página web (Izquierda, Detener, Derecha).
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
  char pass[] = "12345678";    // Coloca la contraseña de tu red
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

const unsigned long INTERVALO_SENSOR_MS = 50;   // Evaluar motor y sensor cada 50 ms
const unsigned long INTERVALO_API_MS    = 350;  // Sincronizar con Render cada 350 ms

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
// MEDICIÓN DE DISTANCIA (HC-SR04)
// ==========================================
long medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  
  // Timeout de 15000 µs (~2.5 metros máx)
  long duracion = pulseIn(PIN_ECHO, HIGH, 15000UL);
  if (duracion == 0) return 999; // Sin obstáculo en rango
  return duracion * 0.034 / 2;
}

// ==========================================
// CONEXIÓN WI-FI
// ==========================================
void verificarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

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
void procesarRespuestaServidor(const String& body) {
  // 1. Extraer Modo ("manual" o "automatico") de forma infalible
  if (body.indexOf("\"modo\":\"manual\"") != -1 || 
      body.indexOf("\"modo\": \"manual\"") != -1 ||
      body.indexOf("\"mode\":\"manual\"") != -1 ||
      body.indexOf("\"mode\": \"manual\"") != -1) {
    modoActual = "manual";
  } 
  else if (body.indexOf("\"modo\":\"automatico\"") != -1 || 
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
  }
  else if (body.indexOf("\"motor\":\"izquierda\"") != -1 || 
           body.indexOf("\"motor\": \"izquierda\"") != -1 ||
           body.indexOf("\"command\":\"izquierda\"") != -1 ||
           body.indexOf("\"command\":\"left\"") != -1) {
    comandoManual = "izquierda";
  }
  else if (body.indexOf("\"motor\":\"detenido\"") != -1 || 
           body.indexOf("\"motor\": \"detenido\"") != -1 ||
           body.indexOf("\"command\":\"detenido\"") != -1 ||
           body.indexOf("\"command\":\"stop\"") != -1) {
    comandoManual = "detenido";
  }

  // 3. Si estamos en modo manual, aplicar de inmediato el comando de giro
  if (modoActual == "manual") {
    if (comandoManual == "derecha") girarDerecha();
    else if (comandoManual == "izquierda") girarIzquierda();
    else detener();
  }
}

// ==========================================
// SINCRONIZACIÓN CON LA API EN RENDER
// ==========================================
void sincronizarConAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    verificarWiFi();
    return;
  }

  // Abrir conexión segura HTTPS con Render
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("[HTTP] No se pudo conectar con Render");
    client.stop();
    return;
  }

  // Armar cuerpo JSON
  String jsonBody = "{\"modo\":\"";
  jsonBody += modoActual;
  jsonBody += "\",\"motor\":\"";
  jsonBody += estadoMotor;
  jsonBody += "\"";
  
  // Solo se envían lecturas del sensor en modo automático
  if (modoActual == "automatico") {
    jsonBody += ",\"distancia\":";
    jsonBody += String(distanciaActual);
  } else {
    jsonBody += ",\"distancia\":null";
  }
  jsonBody += "}";

  // Enviar petición POST
  client.println("POST /api/telemetry HTTP/1.1");
  client.print("Host: ");
  client.println(serverHost);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(jsonBody.length());
  client.println("Connection: close");
  client.println();
  client.println(jsonBody);

  // Leer respuesta con timeout seguro (sale de inmediato al recibir el JSON)
  unsigned long timeout = millis() + 3000;
  String respuesta = "";
  
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      char c = client.read();
      respuesta += c;
    }
    // Si ya llegó el JSON con la confirmación "ok":true, terminar de leer
    if (respuesta.indexOf("\"ok\":true") != -1 && respuesta.indexOf("}") != -1) {
      break;
    }
    delay(5);
  }

  client.stop();

  // Procesar respuesta
  if (respuesta.length() > 0) {
    procesarRespuestaServidor(respuesta);
    Serial.print("[NUBE] Modo: ");
    Serial.print(modoActual);
    Serial.print(" | Comando web: ");
    Serial.print(comandoManual);
    Serial.print(" | Motor fisico: ");
    Serial.println(estadoMotor);
  } else {
    Serial.println("[HTTP] Sin respuesta de la nube (timeout)");
  }
}

// ==========================================
// SETUP INICIAL
// ==========================================
void setup() {
  Serial.begin(9600);
  delay(400);

  Serial.println("==================================================");
  Serial.println(" ARDUINO UNO R4 WiFi — CONTROL DE GIRO Y SENSOR");
  Serial.println("==================================================");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  
  detener();
  verificarWiFi();
}

// ==========================================
// BUCLE PRINCIPAL
// ==========================================
void loop() {
  unsigned long tiempoActual = millis();

  // 1. CONTROL Y ACCIONAMIENTO DEL MOTOR (Cada 50 ms)
  if (tiempoActual - ultimoMuestreoSensor >= INTERVALO_SENSOR_MS) {
    ultimoMuestreoSensor = tiempoActual;

    // ----------------------------------------------------
    // CONTROL DEL MOTOR SEGÚN EL MODO ACTIVO:
    // ----------------------------------------------------
    if (modoActual == "automatico") {
      // 1. MODO AUTOMÁTICO:
      // Funciona leyendo activamente el sensor ultrasónico para decidir el giro
      distanciaActual = medirDistancia();

      if (distanciaActual >= 10 && distanciaActual <= 25) {
        girarDerecha();
      } else if (distanciaActual >= 26 && distanciaActual <= 40) {
        girarIzquierda();
      } else {
        detener();
      }
    } 
    else {
      // 2. MODO MANUAL:
      // NO usa la lectura del sensor (sensor inactivo).
      // El motor responde exclusivamente a los botones de la página web.
      distanciaActual = -1;

      if (comandoManual == "derecha") {
        girarDerecha();
      } else if (comandoManual == "izquierda") {
        girarIzquierda();
      } else {
        detener();
      }
    }

    // Monitor Serie
    if (modoActual == "automatico") {
      Serial.print("[AUTO] Dist: ");
      Serial.print(distanciaActual);
      Serial.print(" cm | Motor: ");
      Serial.println(estadoMotor);
    } else {
      Serial.print("[MANUAL - Sensor Inactivo] Boton Web: ");
      Serial.print(comandoManual);
      Serial.print(" | Motor: ");
      Serial.println(estadoMotor);
    }
  }

  // 2. SINCRONIZACIÓN PERIÓDICA CON RENDER (Cada 350 ms)
  if (tiempoActual - ultimaSincronizacionAPI >= INTERVALO_API_MS) {
    ultimaSincronizacionAPI = tiempoActual;
    sincronizarConAPI();
  }
}
