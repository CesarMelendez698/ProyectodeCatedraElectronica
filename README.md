# Control de Giro con Sensor — EAI441 (Arquitectura Opción 1)

Sistema de control de motor DC y monitorización de sensor ultrasónico HC-SR04 mediante **Arquitectura IoT Cliente-Servidor en la Nube**.

---

## 🏗️ Arquitectura del Sistema (Opción 1)

En esta arquitectura, el Arduino **NO** es servidor web:

```
[ Navegador Web ]  <==== HTTP (GET/POST) ====>  [ Servidor en la Nube (Node.js/Express) ]
(Cualquier red/móvil)                                  (API REST + Frontend Central)
                                                                 ▲
                                                                 │ HTTP POST /api/telemetry
                                                                 │ (Distancia y sincronización)
                                                                 ▼
                                                    [ Arduino UNO R4 WiFi (Cliente IoT) ]
                                                    - Sensor HC-SR04 (Trig: 8, Echo: 9)
                                                    - Puente H / Motor (IN1: 4, IN2: 5, EN: 6)
```

### Ventajas clave:
1. **Acceso Global**: Se puede controlar y monitorear desde cualquier red del mundo sin abrir puertos en el router ni estar amarrado a una red local.
2. **Arduino Ultraligero**: El microcontrolador no almacena HTML/CSS ni gestiona conexiones web complejas. Su memoria se preserva para el control en tiempo real.
3. **Escalabilidad**: El servidor en la nube mantiene el estado centralizado en memoria (*Single Source of Truth*).

---

## 📁 Estructura del Proyecto

```
control_giro_sensor/
├── package.json
├── README.md
├── src/
│   └── server.js               # Servidor Node.js + Express (API REST y servidor estático)
├── public/
│   ├── index.html              # Interfaz web responsiva con Bootstrap y diseño moderno
│   ├── styles.css              # Estilos y animaciones
│   ├── app.js                  # Lógica de conexión a la API y refresco automático
│   └── img/                    # Escudo y fotos de los integrantes
└── firmware/
    └── arduino_control_giro.ino # Código C++ para Arduino UNO R4 WiFi (<WiFiS3.h>)
```

---

## 1. 🚀 Servidor en la Nube / Local (Node.js + Express)

### Ejecución Local en Windows
1. Asegúrate de tener Node.js instalado (`node -v`).
2. Abre una terminal (PowerShell o CMD) en la carpeta `control_giro_sensor`:
   ```powershell
   npm install
   npm start
   ```
3. Accede en tu navegador a: `http://localhost:3000`

### Despliegue en la Nube (Gratis y con URL Pública)
Puedes alojar este backend en plataformas en la nube como **Render**, **Railway**, **Vercel** o un **VPS**:

#### Opción A: Despliegue en Render (Recomendado - 100% Gratuito)
1. Sube tu proyecto a un repositorio de GitHub.
2. Regístrate en [Render.com](https://render.com).
3. Selecciona **New +** -> **Web Service**.
4. Conecta tu repositorio de GitHub.
5. Configura:
   - **Root Directory**: `Control_Giro_Sensor_EAI441_Madrid/control_giro_sensor` (o la ruta donde esté tu `package.json`).
   - **Build Command**: `npm install`
   - **Start Command**: `npm start`
6. Haz clic en **Create Web Service**. En 2 minutos obtendrás una URL pública segura (ej: `https://control-giro-eai441.onrender.com`).

---

## 2. 🔌 Cliente IoT: Arduino UNO R4 WiFi

1. Abre el archivo [`firmware/arduino_control_giro.ino`](file:///c:/Users/cesar/Downloads/Actividad-Electronica/Control_Giro_Sensor_EAI441_Madrid/control_giro_sensor/firmware/arduino_control_giro.ino) en el **Arduino IDE**.
2. En la sección de configuración del sketch:
   - Ingresa tu `WIFI_SSID` y `WIFI_PASS` (puede ser la red de tu casa o el Hotspot de tu teléfono móvil).
   - Ingresa el host del servidor:
     - **Si pruebas en red local**: la IP privada de tu PC (ej: `const char* SERVER_HOST = "192.168.1.50";`).
     - **Si usas la nube (Render, Railway)**: el dominio sin `https://` (ej: `const char* SERVER_HOST = "tu-app.onrender.com";`).
     - Si la nube usa HTTPS (puerto 443), descomenta `WiFiSSLClient client;`.
3. Selecciona la placa **Arduino UNO R4 WiFi** y el puerto COM correspondiente.
4. Presiona **Subir (Upload)**.
5. Abre el Monitor Serie a **115200 baudios** para ver el estado de conexión y la sincronización con la nube.

---

## 3. 📡 Especificación de la API REST

| Método | Endpoint | Emisor | Descripción |
|---|---|---|---|
| `GET` | `/api/status` | Web | Consulta estado actual (`mode`, `distanceCm`, `motorState`, `deviceOnline`). |
| `POST` | `/api/mode` | Web | Cambia modo: `{"mode": "manual"}` o `{"mode": "automatic"}`. |
| `POST` | `/api/motor` | Web | Envía comando manual: `{"command": "left" \| "right" \| "stop"}`. |
| `POST` | `/api/telemetry` | Arduino | Envía lectura de distancia y recibe el comando/modo en la misma respuesta. |
| `GET` | `/api/command` | Arduino | Endpoint alternativo para consultar solo el comando y modo actuales. |

---

## 4. ⚙️ Reglas de Control del Modo Automático

- **Modo Automático**: El sistema lee activamente el sensor ultrasónico HC-SR04 y decide el giro según la distancia medida:
  - **10 cm a 25 cm**: Giro a la **derecha**.
  - **26 cm a 40 cm**: Giro a la **izquierda**.
  - **< 10 cm o > 40 cm**: **Detenido**.
- **Modo Manual**: NO se utiliza la lectura del sensor (el sensor se desactiva/pausa); el motor se acciona única y exclusivamente mediante los botones de la página web (Izquierda, Detener, Derecha).
