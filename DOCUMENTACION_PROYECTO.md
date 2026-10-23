# Documento Técnico de Proyecto — Cátedra de Electrónica (EAI441)

**Proyecto:** Sistema de Control de Giro de Motor DC con Sensor Ultrasónico HC-SR04 y Plataforma Web en la Nube  
**Plataforma IoT:** Arduino UNO R4 WiFi  
**Servidor en la Nube:** Node.js + Express (Desplegado en Render)  
**Repositorio GitHub:** [https://github.com/CesarMelendez698/ProyectodeCatedraElectronica](https://github.com/CesarMelendez698/ProyectodeCatedraElectronica)  
**URL Pública en Producción:** [https://proyectodecatedraelectronica.onrender.com/](https://proyectodecatedraelectronica.onrender.com/)  

---

## 👥 Equipo de Desarrollo

| Integrante | Rol | Fotografía de Referencia |
|---|---|---|
| **Melendez Arias** | Desarrollo Hardware & Firmware | `img/imagen1.jpeg` |
| **Escobar Morán** | Arquitectura Cloud & API REST | `img/imagen2.jpeg` |
| **Lara Franco** | Frontend & Interfaz de Usuario | `img/imagen3.jpeg` |
| **Franco Rivera** | Pruebas de Integración y Red | `img/imagen4.jpeg` |

---

## 1. Introducción y Justificación Arquitectónica (Opción 1)

El objetivo del proyecto consiste en controlar el sentido de giro de un motor DC mediante un puente H (L293D) y supervisar la distancia medida por un sensor ultrasónico (HC-SR04), ofreciendo operación en **Modo Automático** y **Modo Manual** a través de una página web accesible desde cualquier lugar del mundo.

### De Servidor Local a Servidor en la Nube:
Inicialmente, los microcontroladores solían cargar con todo el código HTML/CSS mediante librerías como `WiFiServer` dentro de la memoria flash del Arduino. Esto presentaba tres grandes desventajas:
1. **Límite local:** La página solo abría si el usuario estaba conectado a la misma red Wi-Fi o Hotspot del Arduino.
2. **Sobrecarga del microcontrolador:** Generar cadenas HTML y atender peticiones web bloqueaba la lectura física del sensor.
3. **Falta de escalabilidad:** Múltiples navegadores saturaban la memoria RAM del chip.

Para resolver esto, se adoptó la **Arquitectura Opción 1 (Cliente-Servidor Cloud)**, dividiendo el sistema en dos bloques independientes:
* **El Servidor en la Nube (API + Web):** Aloja la lógica web, almacena el estado global en memoria (*Single Source of Truth*) y expone una API REST pública.
* **El Cliente IoT (Arduino UNO R4 WiFi):** Se despojó de todo el HTML. Se convirtió en un cliente ligero que únicamente mide la distancia, acciona el motor y reporta la telemetría periódicamente por peticiones HTTP/HTTPS seguras.

```mermaid
flowchart TD
    subgraph Internet_Nube["Servidor en la Nube (Render.com)"]
        Server[Node.js + Express API REST]
        Memory[(Estado Global en Memoria)]
        WebUI[Interfaz Web Bootstrap 5.3]
        Server <--> Memory
        Server --> WebUI
    end

    subgraph Clientes_Web["Usuarios / Navegadores"]
        PC[PC / Navegador Web]
        Mobile[Teléfono Móvil / Red Celular]
        PC <-->|HTTP GET / POST| Server
        Mobile <-->|HTTP GET / POST| Server
    end

    subgraph Hardware_IoT["Cliente IoT (Arduino UNO R4 WiFi)"]
        Arduino[Microcontrolador Arduino UNO R4]
        HCSR04[Sensor Ultrasónico HC-SR04]
        L293D[Driver Puente H L293D]
        Motor[Motor DC]
        
        HCSR04 -->|Pines 12 Trig / 11 Echo| Arduino
        Arduino -->|Pines 8 IN1 / 7 IN2| L293D
        L293D --> Motor
        Arduino <-->|HTTPS POST /api/telemetry (WiFiS3)| Server
    end
```

---

## 2. Bloque 1: Servidor en la Nube y API REST (`src/server.js`)

El backend fue desarrollado con **Node.js** y el framework **Express**. Mantiene el estado del sistema en memoria sin depender de bases de datos externas, garantizando respuestas en milisegundos.

### Estado Central en Memoria:
```javascript
let state = {
  mode: "automatico",       // "automatico" | "manual"
  distanceCm: 0,            // Distancia del HC-SR04
  motorState: "detenido",   // "derecha" | "izquierda" | "detenido"
  updatedAt: new Date().toISOString(),
  lastDeviceSeen: null      // Marca de tiempo de conexión del Arduino
};
```

### Especificación de Endpoints:

| Método | Endpoint | Emisor | Función y Comportamiento |
|---|---|---|---|
| `GET` | `/api/estado` o `/api/status` | Web | Retorna `{ distancia, motor, modo, deviceOnline }` para actualizar las tarjetas y botones en pantalla. |
| `GET` / `POST` | `/api/modo/:modo` | Web | Alterna entre `"automatico"` y `"manual"`. Al pasar a manual, detiene el motor por seguridad. |
| `GET` / `POST` | `/api/motor/:comando` | Web | Recibe `"derecha"`, `"izquierda"` o `"detener"`. Solo se permite si el modo actual es manual. |
| `POST` | `/api/telemetry` | Arduino | Recibe `{ distancia, motor }` del Arduino. Actualiza la distancia en el servidor y le responde en el mismo paquete con `{ modo, motor }`. |
| `GET` | `/api/command` | Arduino | Endpoint alternativo para consultar solo el comando y modo actuales. |

---

## 3. Bloque 2: Interfaz Web Responsiva (`public/index.html`)

La página web está construida con **Bootstrap 5.3**, tipografía *Inter* y una línea estética deportiva basada en el **Real Madrid C.F.** (tonos azul real y dorado).

### Elementos Principales de la Pantalla:
1. **Encabezado con Escudo Oficial:** Muestra el escudo vectorial SVG del Real Madrid C.F. y el título del proyecto.
2. **Tarjeta de Distancia:**
   * Muestra la lectura del HC-SR04 en números gigantes (`<span id="distanciaVal">`).
   * Indicador visual de estado (*Badge* dinámico que cambia a verde *"Arduino En Línea"* cuando el microcontrolador está transmitiendo).
3. **Panel de Control de Modos y Giros:**
   * **Botón `⚡ Activar Modo Automático`:** Transfiere el control al sensor ultrasónico. Los botones manuales se deshabilitan automáticamente para evitar colisiones.
   * **Botón `🎮 Activar Modo Manual`:** Habilita los botones de navegación (`↺ Girar Izquierda`, `⏹ Detener Motor`, `↻ Girar Derecha`).
   * **Optimistic UI (0 ms de latencia percibida):** Al hacer clic en un botón, la interfaz web actualiza el texto y color en pantalla inmediatamente antes de esperar el viaje por internet hacia el servidor.
   * **Sondeo en Tiempo Real (150 ms):** Realiza peticiones asíncronas con bandera anti-solapamiento (`isFetching`), garantizando máxima fluidez.
4. **Equipo de Desarrollo:** Tarjetas con avatares circulares y marcos dorados enlazados a las fotos reales de los integrantes.

---

## 4. Bloque 3: Firmware del Cliente IoT (`arduino_control_giro.ino`)

El código C++ para el **Arduino UNO R4 WiFi** utiliza la librería nativa `<WiFiS3.h>`.

### Conexión de Pines:
* **Sensor HC-SR04:**
  * `PIN_TRIG = 12` (Disparo de pulso)
  * `PIN_ECHO = 11` (Recepción de eco)
* **Puente H L293D:**
  * `PIN_IN1 = 8` (Control de dirección A)
  * `PIN_IN2 = 7` (Control de dirección B)

### Lógica de Control por Modos:

```cpp
// 1. MODO AUTOMÁTICO: Gobierna el sensor ultrasónico
if (modoActual == "automatico") {
  if (distanciaActual >= 10 && distanciaActual <= 25) {
    girarDerecha();
  } else if (distanciaActual >= 26 && distanciaActual <= 40) {
    girarIzquierda();
  } else {
    detener();
  }
} 
// 2. MODO MANUAL: Gobierna el botón web (IGNORANDO LA DISTANCIA)
else {
  if (comandoManual == "derecha") {
    girarDerecha();
  } else if (comandoManual == "izquierda") {
    girarIzquierda();
  } else {
    detener();
  }
}
```

### Características Técnicas de Optimización:
* **Medición sin bloqueos:** Se redujo el timeout de `pulseIn` a **15,000 µs** (~2.5 m). Si no hay un objeto enfrente, la función retorna inmediatamente sin congelar la placa.
* **Conexión Segura HTTPS (`WiFiSSLClient`):** El Arduino establece conexión directa cifrada en el puerto **443** con el balanceador de Render.
* **Parseo Robusto de Respuestas:** En lugar de manipular índices de subcadenas propensos a fallar con caracteres de escape, busca directamente las claves `"modo":"manual"`, `"modo":"automatico"`, `"motor":"derecha"`, etc.

---

## 5. Control de Versiones y Despliegue en Producción

### 1. Repositorio en GitHub:
Se creó el repositorio oficial con estructura limpia (ignorando `node_modules` mediante `.gitignore`):
* Repositorio: `https://github.com/CesarMelendez698/ProyectodeCatedraElectronica`
* Rama principal: `main`

### 2. Despliegue Continuo (CI/CD) en Render.com:
* El servicio web está vinculado directamente a la rama `main` de GitHub.
* Cada vez que se realiza un `git push`, Render detecta los cambios, ejecuta `npm install` y reinicia el servicio con `npm start` de manera automática y sin tiempo de inactividad.
* Enlace en vivo: **`https://proyectodecatedraelectronica.onrender.com/`**

---

## 6. Resultados y Conclusión

1. **Acceso Global Verificado:** El sistema fue probado exitosamente accediendo desde navegadores en computadoras y teléfonos móviles conectados a redes móviles celulares, logrando controlar el motor y visualizar la distancia del sensor sin depender de la red local del hogar.
2. **Independencia de Modos Cumplida:**
   * En **Modo Automático**, el motor responde estrictamente a la posición del objeto (10–25 cm derecha, 26–40 cm izquierda, otros detenido).
   * En **Modo Manual**, el motor obedece exclusivamente los botones de navegación web, mientras que el sensor ultrasónico continúa midiendo y transmitiendo la distancia en pantalla de manera ininterrumpida.
3. **Respuesta Rápida:** La combinación de sondeo a 150 ms, actualización optimista en la web y sincronización no bloqueante en el Arduino permitió una experiencia de usuario fluida e instantánea.
