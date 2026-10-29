# Control de Giro con Sensor — EAI441 (Arquitectura Opción 1)

Sistema de control de giro de motor DC con velocidad regulada por PWM y monitorización de sensor ultrasónico HC-SR04 mediante **Arquitectura IoT Cliente-Servidor en la Nube de Respuesta Instantánea**.

---

## 🏗️ Arquitectura del Sistema (Opción 1)

En esta arquitectura, el Arduino **NO** actúa como servidor web, sino como un **Cliente IoT de baja latencia** conectado mediante un socket HTTPS persistente (`Keep-Alive`) al servidor en la nube:

```
[ Navegador Web ]  <=== SSE (/api/stream 0ms) + Fetch ===>  [ Servidor Cloud (Node.js/Express) ]
(Cualquier red/móvil)                                          (API REST + Frontend Central)
                                                                            ▲
                                                                            │ HTTPS POST /api/telemetry
                                                                            │ (Conexión Keep-Alive cada 60 ms)
                                                                            ▼
                                                              [ Arduino UNO R4 WiFi (Cliente IoT) ]
                                                              - Sensor HC-SR04 (Trig: 12, Echo: 11)
                                                              - Puente H L293D (IN1: 8, IN2: 7, ENA: 6)
                                                              - Control PWM por Timer Hardware (FspTimer)
```

### Características y Optimizaciones Clave:
1. **Sincronización Instantánea (~0 ms de retardo visual)**:
   - **Socket TLS Persistente (`Keep-Alive`)**: El Arduino UNO R4 WiFi reutiliza la conexión cifrada HTTPS con Render sin repetir el *handshake* TLS en cada envío, sincronizando cada **60 ms** (o al instante cuando cambia la distancia o el estado del motor).
   - **Server-Sent Events (`GET /api/stream`) + Optimistic UI**: El servidor empuja cualquier cambio en **0 ms** a todos los navegadores abiertos, respaldado por un sondeo anti-caché cada **80 ms**.
   - **Muestreo Ultrasónico Ultra Rápido**: Lectura del sensor HC-SR04 cada **25 ms** con *timeout* optimizado a `5000 µs`.
2. **Velocidad del Motor Regulada (Giro Lento y Controlado)**:
   - Implementa modulación PWM de `100 Hz` asistida por **Timer de Hardware (`FspTimer` a 2000 Hz)** directamente sobre los pines `IN1` (Pin 8) e `IN2` (Pin 7), además de salida PWM en `ENA` (Pin 6).
   - Funciona incluso si el pin `Enable 1,2` del puente H L293D está conectado directo a `5V`.
   - Incluye un micro-impulso de arranque de `20 ms` para vencer la inercia inicial y mantener una velocidad reducida (`VELOCIDAD_MOTOR = 100 / 255`, ~39% de potencia).
3. **Acceso Global**: Se puede monitorear y controlar desde cualquier red o dispositivo móvil sin abrir puertos en el router local.

---

## 📁 Estructura del Proyecto

```
ProyectoCatedraElectronica/
├── package.json
├── README.md
├── src/
│   └── server.js                                  # Servidor Node.js + Express (API REST, SSE y Keep-Alive)
├── public/
│   ├── index.html                                 # Interfaz web responsiva (Bootstrap 5.3)
│   ├── styles.css                                 # Estilos adicionales
│   ├── app.js                                     # Cliente SSE en tiempo real + Optimistic UI + Polling 80ms
│   └── img/                                       # Fotos de los integrantes del equipo
└── firmware/
    └── arduino_control_giro/
        ├── arduino_control_giro.ino               # Firmware para Arduino UNO R4 WiFi (Keep-Alive + PWM)
        └── secrets.h                              # Credenciales Wi-Fi (SSID y contraseña)
```

---

## 🔌 Conexiones de Hardware (Arduino UNO R4 WiFi)

| Componente | Pin del Módulo | Pin en Arduino UNO R4 WiFi | Descripción |
|---|---|---|---|
| **HC-SR04** | `VCC` / `GND` | `5V` / `GND` | Alimentación del sensor ultrasónico |
| **HC-SR04** | `TRIG` | **Pin 12** | Disparo ultrasónico (Salida) |
| **HC-SR04** | `ECHO` | **Pin 11** | Recepción de eco (Entrada) |
| **L293D** | `Pin 1 (Enable 1,2)` | `5V` o **Pin 6** | Habilitación de canal (soporta 5V fijo o PWM en Pin 6) |
| **L293D** | `Pin 2 (IN1)` | **Pin 8** | Dirección A con modulación PWM por Timer |
| **L293D** | `Pin 7 (IN2)` | **Pin 7** | Dirección B con modulación PWM por Timer |
| **L293D** | `Pines 3 y 6 (OUT1, OUT2)` | Terminales del Motor DC | Salida de potencia al motor |

### Ajuste de la Velocidad del Motor
En [`firmware/arduino_control_giro/arduino_control_giro.ino`](file:///c:/Users/cesar/Downloads/ProyectoCatedraElectronica/firmware/arduino_control_giro/arduino_control_giro.ino), puedes personalizar la velocidad modificando la constante en la línea 52:
```cpp
const int VELOCIDAD_MOTOR = 100; // Rango: 0 a 255 (ej. 85 = más lento, 130 = velocidad media)
```

---

## 🚀 1. Ejecución del Servidor (Local y en la Nube)

### Ejecución Local
1. Instala las dependencias e inicia el servidor:
   ```powershell
   npm install
   npm start
   ```
2. Abre en el navegador: `http://localhost:3000`

### Despliegue en Render
1. Sube los cambios a tu repositorio de GitHub.
2. En **Render**, crea o actualiza tu **Web Service** con:
   - **Build Command**: `npm install`
   - **Start Command**: `npm start`

---

## ⚙️ 2. Carga del Firmware en Arduino UNO R4 WiFi

1. Abre [`firmware/arduino_control_giro/arduino_control_giro.ino`](file:///c:/Users/cesar/Downloads/ProyectoCatedraElectronica/firmware/arduino_control_giro/arduino_control_giro.ino) en **Arduino IDE**.
2. Configura el nombre y contraseña de tu red Wi-Fi en [`firmware/arduino_control_giro/secrets.h`](file:///c:/Users/cesar/Downloads/ProyectoCatedraElectronica/firmware/arduino_control_giro/secrets.h) (o directamente en el archivo `.ino`).
3. Verifica que `serverHost` apunte a tu dominio de Render (sin `https://`).
4. Selecciona la placa **Arduino UNO R4 WiFi**, sube el programa y abre el Monitor Serie a **9600 baudios**.

---

## 📡 3. Especificación de la API REST y Tiempo Real

| Método | Endpoint | Emisor | Descripción |
|---|---|---|---|
| `GET` | `/api/stream` | Web (SSE) | Canal *Server-Sent Events* que transmite cambios de estado en **0 ms**. |
| `GET` | `/api/estado` o `/api/status` | Web | Consulta el estado actual (`modo`, `distancia`, `motor`, `deviceOnline`). |
| `GET` | `/api/modo/:modo` | Web | Cambia el modo activo (`automatico` o `manual`). |
| `POST` | `/api/mode` | Web | Cambia el modo vía JSON (`{"modo": "manual" \| "automatico"}`). |
| `GET` | `/api/motor/:comando` | Web | Acciona el motor en modo manual (`izquierda`, `detener`, `derecha`). |
| `POST` | `/api/motor` | Web | Acciona el motor en modo manual vía JSON. |
| `POST` | `/api/telemetry` | Arduino | Recibe la telemetría del Arduino y devuelve el modo y orden actual en la misma respuesta. |
| `GET` | `/api/command` | Arduino | Endpoint ligero alternativo para consultar modo y estado del motor. |

---

## 🎮 4. Reglas de Operación

- **Modo Automático**:
  - El sistema mide activamente la distancia con el sensor ultrasónico HC-SR04 cada `25 ms`:
    - **10 cm a 25 cm**: Gira a la **DERECHA** (a velocidad reducida PWM).
    - **26 cm a 40 cm**: Gira a la **IZQUIERDA** (a velocidad reducida PWM).
    - **< 10 cm o > 40 cm**: **DETENIDO**.
  - Los botones manuales de la interfaz web permanecen deshabilitados.
- **Modo Manual**:
  - **No utiliza la lectura del sensor** (sensor inactivo).
  - El motor obedece de forma exclusiva e instantánea a los botones de la página web (**Girar a la Izquierda**, **Detener Motor**, **Girar a la Derecha**).
