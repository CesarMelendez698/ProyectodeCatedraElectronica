/*
  ==============================================================================
  CLIENTE IoT: ARDUINO UNO R4 WiFi — CONTROL DE GIRO Y SENSOR (OPCIÓN 1)
  ==============================================================================
  Arquitectura: Cliente HTTP en la Nube
  - El Arduino NO es servidor web (se eliminó WiFiServer y toda la variable htmlPage).
  - El Arduino lee el sensor HC-SR04 y envía la distancia al servidor central.
  - Recibe de vuelta el modo (automático/manual) y el comando del motor.
  ==============================================================================
*/

#include "WiFiS3.h"
#include <ArduinoJson.h>
#include "secrets.h"

// --- CONFIGURACIÓN WI-FI ---
char ssid[] = SECRET_SSID; 
char pass[] = SECRET_PASS; 

// --- CONFIGURACIÓN DEL SERVIDOR EN LA NUBE O LOCAL ---
// Para pruebas locales: coloca la IP privada de tu PC (ej: "192.168.1.15")
// Para despliegue en la nube (Render / VPS): coloca tu dominio (ej: "tu-api.onrender.com")
const char* serverHost = "192.168.1.15";
const int   serverPort = 3000; // 3000 para local, 80 para HTTP nube, 443 para HTTPS

// Cliente HTTP para Arduino UNO R4 WiFi:
WiFiClient client;
// WiFiSSLClient client; // <- Descomenta esta línea si tu hosting en la nube exige HTTPS (puerto 443)

// --- PINES SENSOR ULTRASÓNICO (HC-SR04) ---
const int PIN_TRIG = 12;
const int PIN_ECHO = 11;

// --- PINES PUENTE H L293D ---
const int PIN_IN1  = 8; 
const int PIN_IN2  = 7;

// --- VARIABLES GLOBALES DE ESTADO ---
String modoActual = "automatico";
String estadoMotor = "detenido";
long distanciaActual = 0;

unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO_ENVIO_MS = 500; // Enviar telemetría cada 500 ms

// ==========================================
// ACCIONES DEL MOTOR
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
// LECTURA DEL SENSOR HC-SR04
// ==========================================
long medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long duracion = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duracion == 0) return 999;
  return duracion * 0.034 / 2;
}

// ==========================================
// CONEXIÓN Y RECONEXIÓN A WI-FI
// ==========================================
void verificarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Conectado exitosamente.");
    Serial.print("[WiFi] IP local asignada: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Error al conectar. Se reintentará en el siguiente ciclo.");
  }
}

// ==========================================
// COMUNICACIÓN CON LA API EN LA NUBE (HTTP POST)
// ==========================================
void sincronizarConNube() {
  verificarWiFi();
  if (WiFi.status() != WL_CONNECTED) return;

  if (!client.connect(serverHost, serverPort)) {
    Serial.println("[HTTP] No se pudo conectar con el servidor API.");
    client.stop();
    return;
  }

  // 1. Armar el JSON de telemetría con ArduinoJson
  StaticJsonDocument<150> requestDoc;
  requestDoc["distancia"] = distanciaActual;
  requestDoc["motor"] = estadoMotor;

  String jsonBody;
  serializeJson(requestDoc, jsonBody);

  // 2. Enviar petición HTTP POST estándar a /api/telemetry
  client.println("POST /api/telemetry HTTP/1.1");
  client.print("Host: ");
  client.println(serverHost);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(jsonBody.length());
  client.println("Connection: close");
  client.println();
  client.println(jsonBody);

  // 3. Leer respuesta del servidor
  unsigned long timeout = millis();
  while (client.connected() && (millis() - timeout < 2000)) {
    if (client.available()) {
      String headerLine = client.readStringUntil('\n');
      // Al encontrar línea en blanco terminan los headers HTTP y comienza el cuerpo JSON
      if (headerLine == "\r" || headerLine.length() == 0) {
        String responseBody = client.readString();
        
        StaticJsonDocument<256> responseDoc;
        DeserializationError error = deserializeJson(responseDoc, responseBody);
        
        if (!error) {
          // El servidor devuelve el modo ("automatico" / "manual") y el comando activo
          if (responseDoc.containsKey("modo")) {
            modoActual = responseDoc["modo"].as<String>();
          }
          
          // En modo manual, el Arduino ejecuta el comando del motor ordenado desde la web
          if (modoActual == "manual" && responseDoc.containsKey("motor")) {
            String comandoManual = responseDoc["motor"].as<String>();
            if (comandoManual == "derecha") girarDerecha();
            else if (comandoManual == "izquierda") girarIzquierda();
            else detener();
          }

          Serial.print("[NUBE] Distancia enviada: ");
          Serial.print(distanciaActual);
          Serial.print(" cm | Modo: ");
          Serial.print(modoActual);
          Serial.print(" | Motor: ");
          Serial.println(estadoMotor);
        } else {
          Serial.print("[JSON] Error al deserializar respuesta: ");
          Serial.println(error.c_str());
        }
        break;
      }
    }
  }

  client.stop();
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(9600);
  delay(500);

  Serial.println("==============================================");
  Serial.println(" ARDUINO UNO R4 WiFi - CLIENTE IoT (OPCION 1)");
  Serial.println("==============================================");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  detener();

  verificarWiFi();
}

// ==========================================
// LOOP PRINCIPAL
// ==========================================
void loop() {
  // 1. MEDIR DISTANCIA CON EL HC-SR04
  distanciaActual = medirDistancia();

  // 2. CONTROL DEL MOTOR EN MODO AUTOMÁTICO
  // Si está en automático, el sensor decide el giro según las distancias requeridas:
  if (modoActual == "automatico") {
    if (distanciaActual >= 10 && distanciaActual <= 25) {
      girarDerecha();
    } else if (distanciaActual >= 26 && distanciaActual <= 40) {
      girarIzquierda();
    } else {
      detener();
    }
  }

  // 3. SINCRONIZACIÓN PERIÓDICA CON LA API REST
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO_MS) {
    ultimoEnvio = millis();
    sincronizarConNube();
  }

  delay(20);
}
