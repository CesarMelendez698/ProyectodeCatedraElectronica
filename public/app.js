/**
 * Lógica de conexión a la API REST y sincronización en tiempo real
 * Control de Giro con Sensor — Proyecto EAI441
 * 
 * • Modo Automático: Funciona leyendo las lecturas del sensor HC-SR04 para gobernar el motor.
 * • Modo Manual: NO usa las lecturas del sensor; el motor se acciona únicamente con los botones web.
 */

let isFetching = false;

// Sondeo periódico cada 150 ms para sincronización fluida en tiempo real
setInterval(actualizarEstado, 150);
document.addEventListener("DOMContentLoaded", actualizarEstado);
actualizarEstado();

function actualizarEstado() {
  if (isFetching) return;
  isFetching = true;

  fetch('/api/estado')
    .then(res => res.json())
    .then(data => {
      const modo = data.modo || data.mode || 'automatico';
      const dist = (data.distancia !== null && data.distancia !== undefined) ? data.distancia : data.distanceCm;
      const motor = data.motor || data.motorState || 'detenido';

      const distanciaEl = document.getElementById('distanciaVal');
      const distanciaUnidadEl = document.getElementById('distanciaUnidad');
      const modoBadge = document.getElementById('modoVal');
      const motorEl = document.getElementById('motorVal');
      const btnIzq = document.getElementById('btnIzq');
      const btnDer = document.getElementById('btnDer');
      const btnDet = document.getElementById('btnDet');
      const btnModoAuto = document.getElementById('btnModoAuto');
      const btnModoManual = document.getElementById('btnModoManual');
      const ayudaModo = document.getElementById('ayudaModo');
      const connBadge = document.getElementById('connStatus');

      // Estado de conexión del dispositivo Arduino / Nube
      if (connBadge) {
        if (data.deviceOnline) {
          connBadge.className = 'badge bg-success';
          connBadge.innerHTML = '<span class="status-dot bg-light pulse-dot"></span>Arduino En Línea';
        } else {
          connBadge.className = 'badge bg-secondary';
          connBadge.innerHTML = '<span class="status-dot bg-warning pulse-dot"></span>Nube Conectada';
        }
      }

      // Modo y motor
      if (modoBadge) modoBadge.innerText = modo.toUpperCase();
      if (motorEl) {
        motorEl.innerText = motor.toUpperCase();
        if (motor === 'derecha') motorEl.className = 'text-success fs-5 text-uppercase fw-bold';
        else if (motor === 'izquierda') motorEl.className = 'text-info fs-5 text-uppercase fw-bold';
        else motorEl.className = 'text-danger fs-5 text-uppercase fw-bold';
      }

      // =========================================================================
      // COMPORTAMIENTO SEGÚN EL MODO:
      // =========================================================================
      if (modo === 'automatico') {
        // EN MODO AUTOMÁTICO: Funciona leyendo las lecturas del sensor
        if (distanciaEl) {
          distanciaEl.innerText = (dist !== null && dist !== undefined && dist >= 0) ? dist : '--';
          distanciaEl.className = 'distance-display my-2 text-info';
        }
        if (distanciaUnidadEl) {
          distanciaUnidadEl.innerHTML = '<span class="badge bg-info text-dark me-1">Sensor Activo</span> centímetros';
        }

        if (btnModoAuto) btnModoAuto.className = 'btn btn-info text-dark fw-bold px-3 py-2 shadow';
        if (btnModoManual) btnModoManual.className = 'btn btn-outline-warning text-warning fw-bold px-3 py-2';
        if (modoBadge) modoBadge.className = 'badge bg-info text-dark fs-6';

        // En automático los botones manuales quedan deshabilitados
        if (btnIzq) btnIzq.disabled = true;
        if (btnDer) btnDer.disabled = true;
        if (btnDet) btnDet.disabled = true;

        if (ayudaModo) {
          ayudaModo.innerHTML = "<strong>Control Automático activo:</strong> El sistema funciona leyendo activamente las lecturas del sensor ultrasónico HC-SR04 (10-25 cm: Derecha | 26-40 cm: Izquierda | Fuera de rango: Detenido). Los botones manuales quedan deshabilitados.";
        }
      } else {
        // EN MODO MANUAL: NO se usa la lectura del sensor, solo los botones de la página
        if (distanciaEl) {
          distanciaEl.innerText = '--';
          distanciaEl.className = 'distance-display my-2 text-secondary opacity-50';
        }
        if (distanciaUnidadEl) {
          distanciaUnidadEl.innerHTML = '<span class="badge bg-warning text-dark me-1">Sensor Inactivo</span> Modo manual (solo botones)';
        }

        if (btnModoAuto) btnModoAuto.className = 'btn btn-outline-info fw-bold px-3 py-2';
        if (btnModoManual) btnModoManual.className = 'btn btn-warning text-dark fw-bold px-3 py-2 shadow';
        if (modoBadge) modoBadge.className = 'badge bg-warning text-dark fs-6';

        // En manual los botones de la página se habilitan
        if (btnIzq) btnIzq.disabled = false;
        if (btnDer) btnDer.disabled = false;
        if (btnDet) btnDet.disabled = false;

        if (ayudaModo) {
          ayudaModo.innerHTML = "<strong>Control Manual activo:</strong> La lectura del sensor NO se utiliza. El motor responde única y exclusivamente a los botones de la página web.";
        }
      }
    })
    .catch(err => {
      const connBadge = document.getElementById('connStatus');
      if (connBadge) {
        connBadge.className = 'badge bg-danger';
        connBadge.innerText = 'Sin Conexión API';
      }
    })
    .finally(() => {
      isFetching = false;
    });
}

// Respuesta instantánea al hacer clic en los modos (0 ms de retardo visual)
function cambiarModo(modo) {
  const modoBadge = document.getElementById('modoVal');
  const btnModoAuto = document.getElementById('btnModoAuto');
  const btnModoManual = document.getElementById('btnModoManual');
  const btnIzq = document.getElementById('btnIzq');
  const btnDer = document.getElementById('btnDer');
  const btnDet = document.getElementById('btnDet');
  const distanciaEl = document.getElementById('distanciaVal');
  const distanciaUnidadEl = document.getElementById('distanciaUnidad');
  const ayudaModo = document.getElementById('ayudaModo');

  if (modoBadge) modoBadge.innerText = modo.toUpperCase();

  if (modo === 'automatico') {
    if (modoBadge) modoBadge.className = 'badge bg-info text-dark fs-6';
    if (btnModoAuto) btnModoAuto.className = 'btn btn-info text-dark fw-bold px-3 py-2 shadow';
    if (btnModoManual) btnModoManual.className = 'btn btn-outline-warning text-warning fw-bold px-3 py-2';
    if (btnIzq) btnIzq.disabled = true;
    if (btnDer) btnDer.disabled = true;
    if (btnDet) btnDet.disabled = true;
    if (distanciaUnidadEl) distanciaUnidadEl.innerHTML = '<span class="badge bg-info text-dark me-1">Sensor Activo</span> centímetros';
    if (ayudaModo) {
      ayudaModo.innerHTML = "<strong>Control Automático activo:</strong> El sistema funciona leyendo activamente las lecturas del sensor ultrasónico HC-SR04. Los botones manuales quedan deshabilitados.";
    }
  } else {
    if (modoBadge) modoBadge.className = 'badge bg-warning text-dark fs-6';
    if (btnModoAuto) btnModoAuto.className = 'btn btn-outline-info fw-bold px-3 py-2';
    if (btnModoManual) btnModoManual.className = 'btn btn-warning text-dark fw-bold px-3 py-2 shadow';
    if (btnIzq) btnIzq.disabled = false;
    if (btnDer) btnDer.disabled = false;
    if (btnDet) btnDet.disabled = false;
    if (distanciaEl) {
      distanciaEl.innerText = '--';
      distanciaEl.className = 'distance-display my-2 text-secondary opacity-50';
    }
    if (distanciaUnidadEl) distanciaUnidadEl.innerHTML = '<span class="badge bg-warning text-dark me-1">Sensor Inactivo</span> Modo manual (solo botones)';
    if (ayudaModo) {
      ayudaModo.innerHTML = "<strong>Control Manual activo:</strong> La lectura del sensor NO se utiliza. El motor responde única y exclusivamente a los botones de la página web.";
    }
  }

  fetch('/api/modo/' + modo).then(actualizarEstado).catch(console.error);
}

// Respuesta instantánea al presionar los comandos de motor (0 ms de retardo visual)
function enviarComando(comando) {
  const motorEl = document.getElementById('motorVal');
  if (motorEl) {
    motorEl.innerText = comando.toUpperCase();
    if (comando === 'derecha') motorEl.className = 'text-success fs-5 text-uppercase fw-bold';
    else if (comando === 'izquierda') motorEl.className = 'text-info fs-5 text-uppercase fw-bold';
    else motorEl.className = 'text-danger fs-5 text-uppercase fw-bold';
  }

  fetch('/api/motor/' + comando).then(actualizarEstado).catch(console.error);
}
