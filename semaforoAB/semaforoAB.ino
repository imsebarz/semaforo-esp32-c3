// =====================================================================
//  SEMÁFOROS A/B CON PRIORIDAD PARA VEHÍCULOS DE EMERGENCIA (ESP32-C3)
//
//  Se carga este MISMO archivo en las dos placas y solo cambia ROL:
//    'A' = maestro: semáforo A + cerebro del ciclo + página web + red Wi-Fi propia
//    'B' = esclavo: semáforo B, copia el ciclo que le manda el maestro
//
//  Cómo se conecta todo:
//    - La placa A CREA su propia red Wi-Fi (AP_SSID) en un canal fijo (CANAL_WIFI).
//    - El celular de la ambulancia se conecta a esa red y abre http://192.168.4.1
//    - La distancia se estima con la intensidad de la señal (RSSI) del celular.
//    - La placa B no se conecta a ninguna red: escucha ESP-NOW en ese mismo canal.
//
//  Archivos del sketch:
//    semaforoAB.ino  -> este archivo (lógica)
//    pagina.h        -> la página web (HTML, CSS y JavaScript)
//
//  Orden del archivo:
//    1. Configuración       5. Medición de distancia   9. Placa A (maestro)
//    2. Tipos y datos       6. ESP-NOW (lado A)       10. Placa B (esclavo)
//    3. LEDs y temporizador 7. Wi-Fi propio (lado A)  11. setup() y loop()
//    4. Ciclo y prioridad   8. Servidor web
// =====================================================================

#include <WiFi.h>                                   // Wi-Fi del ESP32
#include <WebServer.h>                              // servidor web sencillo
#include <esp_now.h>                                // comunicación directa entre las dos placas
#include <esp_wifi.h>                               // control del canal de radio
#include "pagina.h"                                 // la página web (constante PAGINA)

// =====================================================================
//  1. CONFIGURACIÓN (lo único que se toca para ajustar el sistema)
// =====================================================================

const char ROL = 'A';                               // rol de esta placa: 'A' maestro, 'B' esclavo

// ---------- Red Wi-Fi propia (la crea la placa A y el celular se conecta a ella) ----------
const char* AP_SSID  = "Semaforo_A";                // nombre de la red que crea la placa A
const char* AP_CLAVE = "semaforo123";               // clave de la red (mínimo 8 caracteres)
const uint8_t CANAL_WIFI = 11;                      // canal fijo de la red Y de ESP-NOW (use 1, 6 u 11)
const uint8_t MAX_CLIENTES = 1;                     // cuántos celulares pueden conectarse a la vez
#define POTENCIA_WIFI WIFI_POWER_15dBm              // potencia de transmisión (si falla, probar 11dBm o 8_5dBm)

// ---------- Pines de los LEDs de esta placa ----------
const int PIN_R = 0;                                // pin del LED rojo
const int PIN_Y = 1;                                // pin del LED amarillo
const int PIN_G = 2;                                // pin del LED verde
const int PIN_CAMARA = 3;                           // pin del indicador de cámara (solo placa A)

// ---------- Distancia por RSSI (calibrar) ----------
// OJO: ahora se mide la señal que el CELULAR envía a la placa A, así que hay que
// volver a calibrar RSSI_1M, RSSI_ON y RSSI_OFF con la nueva conexión.
const float RSSI_1M = -40;                          // RSSI medido a 1 metro
const float N_PATH  = 2.5;                          // exponente de pérdida (entre 2 y 4)
const int RSSI_ON   = -45;                          // más fuerte que esto: se habilita el botón
const int RSSI_OFF  = -52;                          // más débil que esto: se deshabilita (histéresis)
const int RSSI_SIN_SENAL = -100;                    // valor que se usa cuando no hay celular conectado
const int RSSI_UMBRAL_SENAL = -99;                  // por debajo de esto no hay medición válida
const float PESO_SUBIDA = 0.6;                      // peso de la lectura nueva si la señal sube (reacciona rápido)
const float PESO_BAJADA = 0.2;                      // peso de la lectura nueva si la señal baja (suaviza)

// ---------- Tiempos del ciclo (ms) ----------
const unsigned long T_VERDE = 5000;                 // verde fijo
const unsigned long T_PARPADEO = 1800;              // verde titilante antes del amarillo
const unsigned long T_AMARILLO = 2000;              // amarillo
const unsigned long T_ROJO = 1000;                  // todo en rojo entre una fase y otra
const unsigned long T_AVISO = 3000;                 // prioridad: amarillo de aviso a B
const unsigned long T_DESPEJE = 2000;               // prioridad: todo en rojo para despejar el cruce
const unsigned long T_PRIORIDAD = 10000;            // prioridad: verde titilante para A
const unsigned long T_RET_AM = 3000;                // retorno: amarillo de A
const unsigned long T_RET_ROJO = 2000;              // retorno: todo en rojo
const unsigned long T_MEDIO_PARPADEO = 300;         // ms encendido o apagado en cada parpadeo

// ---------- Tiempos de las tareas (ms) ----------
const unsigned long T_LECTURA_RSSI = 150;           // cada cuánto se mide la señal
const unsigned long T_LOG = 1000;                   // cada cuánto se imprime el estado por Serial
const unsigned long T_PAUSA_TAREA = 5;              // pausa de la tarea de semáforos entre vueltas

// ---------- Tarea de semáforos (FreeRTOS) ----------
const uint32_t TAM_PILA_TAREA = 4096;               // memoria de pila de la tarea (bytes)
const UBaseType_t PRIORIDAD_TAREA = 2;              // prioridad (loop() tiene 1, así que esta gana)

// ---------- Comunicación A -> B por ESP-NOW ----------
const uint8_t MAGIA = 0xA5;                         // código para reconocer paquetes propios
const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // dirección "para todos"
const unsigned long T_ENVIO = 50;                   // el maestro envía el ciclo cada 50 ms
const unsigned long T_RELEVO = 300;                 // sin paquetes en 0,3 s, B sigue el ciclo sola
const unsigned long T_SIN_ENLACE = 1000;            // sin paquetes en 1 s, B vuelve a fijar el canal por si acaso
const unsigned long T_REAFIRMAR_CANAL = 5000;       // mientras no oiga a A, B reafirma el canal cada 5 s
const unsigned long T_AUTONOMO = 60000;             // B sigue sola hasta 60 s sin oír a A; luego rojo

// ---------- Servidor web ----------
const size_t TAM_JSON = 256;                        // tamaño del texto JSON que se envía a la página

// =====================================================================
//  2. TIPOS Y DATOS COMPARTIDOS
// =====================================================================

struct Paquete {                                    // lo que A le envía a B
  uint8_t magia;                                    // código de validación (MAGIA)
  uint8_t estado;                                   // punto del ciclo en que va A
  uint16_t t;                                       // ms que lleva A en ese estado
  uint8_t sec;                                      // número de secuencia del paquete
};                                                  // fin de Paquete

enum Estado {                                       // todos los puntos del ciclo
  A_VERDE, A_PARPADEO, A_AMARILLO, ROJO_1,          // fase de la vía A
  B_VERDE, B_PARPADEO, B_AMARILLO, ROJO_2,          // fase de la vía B
  P_AVISO, P_DESPEJE, P_VERDE, P_RET_AM, P_RET_ROJO // fases de la prioridad
};                                                  // fin de Estado

Estado estado = ROJO_1;                             // estado actual (arranca en todo rojo)
unsigned long inicioEstado = 0;                     // momento (millis) en que empezó el estado actual
portMUX_TYPE muxCiclo = portMUX_INITIALIZER_UNLOCKED; // comparte la fase de A con el servidor

// =====================================================================
//  3. LEDS Y TEMPORIZADOR
// =====================================================================

void configurarPinesLeds() {                        // deja los tres pines de LED como salidas
  pinMode(PIN_R, OUTPUT);                           // rojo como salida
  pinMode(PIN_Y, OUTPUT);                           // amarillo como salida
  pinMode(PIN_G, OUTPUT);                           // verde como salida
}

void escribirLed(char luz) {                        // enciende solo la luz pedida: 'R', 'Y', 'G' o 'N' (ninguna)
  digitalWrite(PIN_R, luz == 'R');                  // rojo encendido solo si se pidió 'R'
  digitalWrite(PIN_Y, luz == 'Y');                  // amarillo encendido solo si se pidió 'Y'
  digitalWrite(PIN_G, luz == 'G');                  // verde encendido solo si se pidió 'G'
}

bool haPasado(unsigned long &ultimo, unsigned long intervalo) {  // true cada vez que se cumple el intervalo
  if (millis() - ultimo < intervalo) return false;  // todavía no toca
  ultimo = millis();                                // anota el momento de este aviso
  return true;                                      // ya tocaba
}

// =====================================================================
//  4. CICLO Y PRIORIDAD (lo usan las dos placas)
// =====================================================================

unsigned long tiempoEnEstado() {                    // ms que lleva el estado actual
  return millis() - inicioEstado;                   // ahora menos el momento en que empezó
}

void cambiarEstado(Estado nuevo) {                  // pasa a otro estado y reinicia su cronómetro
  estado = nuevo;                                   // guarda el estado nuevo
  inicioEstado = millis();                          // anota cuándo empezó
}

unsigned long duracion(Estado e) {                  // cuánto dura cada estado (ms)
  switch (e) {                                      // según el estado pedido
    case A_VERDE: case B_VERDE: return T_VERDE;     // verde fijo
    case A_PARPADEO: case B_PARPADEO: return T_PARPADEO;  // verde titilante
    case A_AMARILLO: case B_AMARILLO: return T_AMARILLO;  // amarillo
    case ROJO_1: case ROJO_2: return T_ROJO;        // todo en rojo
    case P_AVISO: return T_AVISO;                   // aviso de prioridad
    case P_DESPEJE: return T_DESPEJE;               // despeje de prioridad
    case P_VERDE: return T_PRIORIDAD;               // verde de prioridad
    case P_RET_AM: return T_RET_AM;                 // amarillo de retorno
    default: return T_RET_ROJO;                     // rojo de retorno
  }
}

Estado siguiente(Estado e) {                        // qué estado viene después de cada uno
  switch (e) {                                      // según el estado actual
    case A_VERDE: return A_PARPADEO;                // verde -> titila
    case A_PARPADEO: return A_AMARILLO;             // titila -> amarillo
    case A_AMARILLO: return ROJO_1;                 // amarillo -> rojo
    case ROJO_1: return B_VERDE;                    // rojo -> turno de B
    case B_VERDE: return B_PARPADEO;                // verde -> titila
    case B_PARPADEO: return B_AMARILLO;             // titila -> amarillo
    case B_AMARILLO: return ROJO_2;                 // amarillo -> rojo
    case ROJO_2: return A_VERDE;                    // rojo -> turno de A
    case P_AVISO: return P_DESPEJE;                 // aviso -> despeje
    case P_DESPEJE: return P_VERDE;                 // despeje -> verde de prioridad
    case P_VERDE: return P_RET_AM;                  // verde de prioridad -> amarillo
    case P_RET_AM: return P_RET_ROJO;               // amarillo -> rojo
    default: return B_VERDE;                        // tras la prioridad sigue el ciclo normal
  }
}

bool estadoCumplido() {                             // true si el estado actual ya agotó su tiempo
  return tiempoEnEstado() >= duracion(estado);      // compara lo transcurrido con la duración
}

void calcularLuces(Estado e, unsigned long t, char &a, char &b) {  // luz de A y de B para un estado y un tiempo
  unsigned long fase = (t / T_MEDIO_PARPADEO) % 2;  // 0 o 1: alterna cada medio parpadeo
  bool verdeOn = (fase == 1);                       // parpadeo normal: empieza apagado
  bool titila  = (fase == 0);                       // parpadeo de prioridad: empieza encendido
  switch (e) {                                      // según el estado
    case A_VERDE:                    a = 'G'; b = 'R'; break;                   // A verde, B rojo
    case P_VERDE:                    a = titila ? 'G' : 'N'; b = 'R'; break;    // A verde titilante, B rojo
    case A_PARPADEO:                 a = verdeOn ? 'G' : 'N'; b = 'R'; break;   // A titila antes del amarillo
    case A_AMARILLO: case P_RET_AM:  a = 'Y'; b = 'R'; break;                   // A amarillo, B rojo
    case B_VERDE:                    a = 'R'; b = 'G'; break;                   // A rojo, B verde
    case B_PARPADEO:                 a = 'R'; b = verdeOn ? 'G' : 'N'; break;   // B titila antes del amarillo
    case B_AMARILLO: case P_AVISO:   a = 'R'; b = 'Y'; break;                   // A rojo, B amarillo
    default:                         a = 'R'; b = 'R'; break;                   // todo en rojo
  }
}

bool enPrioridad(Estado fase) {
  return fase >= P_AVISO;
}

bool aTieneVerde(Estado fase) {
  return fase == A_VERDE || fase == A_PARPADEO;
}

// =====================================================================
//  5. MEDICIÓN DE DISTANCIA Y BOTÓN (placa A)
// =====================================================================

float rssiF = RSSI_SIN_SENAL;                       // señal filtrada (dBm)
bool habilitado = false;                            // true si el vehículo está lo bastante cerca
int eventos = 0;                                    // cuántas prioridades se han activado

float distanciaM(float rssi) {                      // convierte RSSI en metros aproximados
  return pow(10.0, (RSSI_1M - rssi) / (10.0 * N_PATH));  // modelo de pérdida por distancia
}

void reiniciarMedicion() {                          // olvida la medición (no hay celular conectado)
  rssiF = RSSI_SIN_SENAL;                           // vuelve al valor "sin señal"
  habilitado = false;                               // el botón queda bloqueado
}

void filtrarRssi(int lectura) {                     // mezcla la lectura nueva con el valor filtrado
  if (rssiF <= RSSI_UMBRAL_SENAL) rssiF = lectura;                            // primera lectura: se toma tal cual
  else if (lectura > rssiF) rssiF = (1 - PESO_SUBIDA) * rssiF + PESO_SUBIDA * lectura;  // acercándose: rápido
  else rssiF = (1 - PESO_BAJADA) * rssiF + PESO_BAJADA * lectura;             // alejándose: suave
}

void actualizarHabilitado() {                       // habilita o bloquea el botón con histéresis
  if (!habilitado && rssiF > RSSI_ON) habilitado = true;         // se acercó lo suficiente: habilita
  else if (habilitado && rssiF < RSSI_OFF) habilitado = false;   // se alejó lo suficiente: bloquea
}

int leerRssiCelular() {                             // señal del celular conectado a la red de la placa (0 = sin dato)
  wifi_sta_list_t lista;                            // lista de los celulares conectados a la red
  if (esp_wifi_ap_get_sta_list(&lista) != ESP_OK) return 0;  // si no se pudo leer la lista, no hay dato
  if (lista.num < 1) return 0;                      // no hay ningún celular conectado
  return lista.sta[0].rssi;                         // señal promedio de ese celular (el único permitido)
}

void actualizarMedicion(bool hayCliente) {          // una pasada completa de medición
  if (!hayCliente) {                                // sin celular conectado no hay medición
    reiniciarMedicion();                            // se borra lo medido
    return;                                         // y no se hace nada más
  }
  int lectura = leerRssiCelular();                  // lee la señal del celular (0 = lectura fallida)
  if (lectura < 0) filtrarRssi(lectura);            // solo se usan lecturas válidas
  actualizarHabilitado();                           // decide si el botón se habilita
}

// =====================================================================
//  6. ESP-NOW (lado maestro) Y PRIORIDAD
// =====================================================================

bool espNowListo = false;                           // se configura antes de crear la tarea
uint8_t secuencia = 0;                              // contador que se agrega a cada paquete

bool iniciarEspNow() {                              // arranca ESP-NOW (lo usan las dos placas)
  bool ok = (esp_now_init() == ESP_OK);             // intenta iniciarlo
  if (!ok) Serial.println("Error al iniciar ESP-NOW");  // avisa si falló
  return ok;                                        // dice si funcionó
}

void agregarParBroadcast() {                        // registra la dirección "para todos" como destino
  esp_now_peer_info_t par = {};                     // ficha del destino, en blanco
  memcpy(par.peer_addr, BROADCAST, 6);              // dirección de difusión
  par.channel = 0;                                  // 0 = usa el canal actual (que es CANAL_WIFI)
  par.ifidx = WIFI_IF_AP;                           // el maestro envía por su interfaz de punto de acceso
  par.encrypt = false;                              // sin cifrado
  esp_now_add_peer(&par);                           // lo agrega a ESP-NOW
}

void iniciarEspNowMaestro() {                       // deja ESP-NOW listo para enviar (se llama una sola vez)
  espNowListo = false;                              // mientras tanto no se envía
  if (!iniciarEspNow()) return;                     // si no arranca, se queda sin enviar
  agregarParBroadcast();                            // registra el destino
  espNowListo = true;                               // ya se puede enviar
}

Paquete construirPaquete(Estado fase, unsigned long t) {
  Paquete q = {};
  q.magia = MAGIA;                                  // marca de validación
  q.estado = (uint8_t)fase;
  q.t = t > UINT16_MAX ? UINT16_MAX : (uint16_t)t;
  q.sec = secuencia++;                              // número de secuencia (y lo incrementa)
  return q;                                         // entrega el paquete
}

void enviarPuntoDelCicloSiToca(Estado fase, unsigned long t) {
  static unsigned long tEnvio = 0;                  // momento del último envío
  static int ultimaFase = -1;
  if (!espNowListo) return;                         // si ESP-NOW no está listo, no se envía
  if (fase == ultimaFase && millis() - tEnvio < T_ENVIO) return;
  tEnvio = millis();
  Paquete q = construirPaquete(fase, t);
  if (esp_now_send(BROADCAST, (uint8_t*)&q, sizeof(q)) == ESP_OK) ultimaFase = fase;
}

Estado estadoInicialDePrioridad() {                 // decide por dónde entra la prioridad según el ciclo
  switch (estado) {                                 // según dónde va el ciclo
    case B_VERDE: case B_PARPADEO: case B_AMARILLO: // hay que cortar a B
      return P_AVISO;                               // primero aviso y luego despeje
    default:                                        // A en amarillo o todo en rojo
      return P_DESPEJE;                             // se despeja el cruce directamente
  }
}

void iniciarPrioridad() {                           // activa una prioridad nueva
  eventos++;                                        // cuenta el evento
  cambiarEstado(estadoInicialDePrioridad());        // entra a la fase que corresponde
}

// =====================================================================
//  7. WI-FI PROPIO (lado maestro)
//  La placa A crea la red y el celular se conecta a ella. Como la placa
//  decide el canal, A y B siempre coinciden y B no tiene que buscarlo.
// =====================================================================

bool hayCelular() {                                 // true si hay un celular conectado a la red de la placa
  return WiFi.softAPgetStationNum() > 0;            // cuenta los clientes conectados
}

uint8_t canalActual() {                             // canal de radio en uso (lo usan las dos placas)
  uint8_t canal = 0;                                // aquí se guarda el canal principal
  wifi_second_chan_t secundario;                    // canal secundario (no se usa, pero la función lo pide)
  esp_wifi_get_channel(&canal, &secundario);        // pregunta al chip en qué canal está
  return canal;                                     // entrega el canal
}

void alConectarseCelular() {                        // se ejecuta cuando un celular entra a la red
  Serial.println(">> Celular conectado a la red");  // avisa por Serial
}

void alDesconectarseCelular() {                     // se ejecuta cuando un celular sale de la red
  Serial.println(">> Celular desconectado");        // avisa por Serial
}

void eventoWiFi(WiFiEvent_t evento, WiFiEventInfo_t info) {  // recibe todos los eventos del Wi-Fi
  if (evento == ARDUINO_EVENT_WIFI_AP_STACONNECTED) alConectarseCelular();        // entró un celular
  if (evento == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) alDesconectarseCelular();  // salió un celular
}

void registrarEventosWiFi() {                       // prepara la escucha de eventos del Wi-Fi
  WiFi.onEvent(eventoWiFi);                         // registra la función que atiende los eventos
  WiFi.persistent(false);                           // no guarda datos de red en la memoria flash
}

void encenderWiFiPuntoAcceso() {                    // enciende la radio como punto de acceso
  WiFi.mode(WIFI_AP);                               // modo punto de acceso (la placa crea la red)
  delay(200);                                       // espera a que arranque
}

bool crearRed() {                                   // crea la red Wi-Fi propia
  bool ok = WiFi.softAP(AP_SSID, AP_CLAVE, CANAL_WIFI, 0, MAX_CLIENTES);  // nombre, clave, canal fijo, visible, máx. celulares
  if (!ok) Serial.println("Error al crear la red Wi-Fi");  // avisa si falló
  return ok;                                        // dice si funcionó
}

void configurarPotenciaWiFi() {                     // fija la potencia de transmisión (después de crear la red)
  WiFi.setTxPower(POTENCIA_WIFI);                   // aplica la potencia elegida arriba
}

void desactivarAhorroEnergiaWiFi() {                // evita que la radio duerma entre paquetes
  WiFi.setSleep(false);                             // sin ahorro: RSSI y web más ágiles
}

void imprimirDatosRed() {                           // muestra cómo conectarse y dónde abrir la página
  Serial.printf("RED CREADA: \"%s\" (canal %d)\n", AP_SSID, canalActual());  // nombre y canal
  Serial.print("Conecta el celular y abre: http://");  // texto de ayuda
  Serial.println(WiFi.softAPIP());                  // dirección de la placa (normalmente 192.168.4.1)
}

void iniciarRedPropia() {                           // secuencia completa de arranque de la red
  encenderWiFiPuntoAcceso();                        // 1. enciende como punto de acceso
  if (!crearRed()) return;                          // 2. crea la red (si falla, no sigue)
  configurarPotenciaWiFi();                         // 3. fija la potencia
  desactivarAhorroEnergiaWiFi();                    // 4. quita el ahorro de energía
  imprimirDatosRed();                               // 5. muestra nombre, canal y dirección
  iniciarEspNowMaestro();                           // 6. activa ESP-NOW sobre este canal
}

// =====================================================================
//  8. SERVIDOR WEB (lado maestro)
// =====================================================================

WebServer servidor(80);                             // servidor web en el puerto 80

void leerFaseMaestro(Estado &fase, unsigned long &t) {
  portENTER_CRITICAL(&muxCiclo);
  fase = estado;
  t = tiempoEnEstado();
  portEXIT_CRITICAL(&muxCiclo);
}

const char* nombreEstado(Estado fase) {
  switch (fase) {
    case P_AVISO: return "Aviso: amarillo";         // prioridad: aviso
    case P_DESPEJE: return "Despeje: todo en rojo"; // prioridad: despeje
    case P_VERDE: return "Prioridad activa";        // prioridad: verde
    case P_RET_AM: case P_RET_ROJO: return "Retorno al ciclo normal";  // prioridad: retorno
    default: return "Modo normal";                  // ciclo normal
  }
}

long segundosRestantes(Estado fase, unsigned long t) {
  long resto = (long)duracion(fase) - (long)t;
  if (resto < 0) resto = 0;                         // nunca negativo
  return (resto + 999) / 1000;                      // pasa a segundos redondeando hacia arriba
}

void construirJson(char* buf, size_t tam) {         // escribe el estado completo como JSON
  Estado fase;
  unsigned long t;
  leerFaseMaestro(fase, t);
  char luzA, luzB;                                  // luces actuales de A y de B
  calcularLuces(fase, t, luzA, luzB);
  snprintf(buf, tam,                                // escribe en el buffer sin pasarse del tamaño
    "{\"rssi\":%d,\"dist\":%.1f,\"hab\":%s,\"prio\":%s,\"est\":\"%s\","  // señal, distancia, botón, prioridad, texto
    "\"rest\":%ld,\"a\":\"%c\",\"b\":\"%c\",\"ev\":%d,\"s\":%d,\"t\":%lu,\"v\":%d}",  // resto, luces, eventos, estado, tiempo, verde
    (int)rssiF, distanciaM(rssiF), habilitado ? "true" : "false", enPrioridad(fase) ? "true" : "false",
    nombreEstado(fase), segundosRestantes(fase, t), luzA, luzB, eventos, (int)fase, t, aTieneVerde(fase) ? 1 : 0);
}

void enviarEstado() {                               // responde /estado con el JSON
  char buf[TAM_JSON];                               // espacio para el texto
  construirJson(buf, sizeof(buf));                  // lo llena con el estado actual
  servidor.sendHeader("Cache-Control", "no-store"); // que el navegador nunca guarde una copia vieja
  servidor.send(200, "application/json", buf);      // lo envía como JSON
}

void enviarPagina() {                               // responde / con la página web
  servidor.send_P(200, "text/html; charset=utf-8", PAGINA);
}

void pedirPrioridad() {                             // responde /prioridad (POST)
  if (!habilitado) {                                // el vehículo está lejos
    servidor.send(403, "text/plain", "lejos");      // se rechaza
    return;                                         // fin
  }
  const char* rechazo = nullptr;
  portENTER_CRITICAL(&muxCiclo);
  if (enPrioridad(estado)) rechazo = "en curso";
  else if (aTieneVerde(estado)) rechazo = "ya tienes verde";
  else iniciarPrioridad();
  portEXIT_CRITICAL(&muxCiclo);
  servidor.send(rechazo ? 409 : 200, "text/plain", rechazo ? rechazo : "ok");
}

void registrarRutas() {                             // asocia cada dirección con su función
  servidor.on("/", enviarPagina);                   // la página principal
  servidor.on("/estado", enviarEstado);             // los datos para la página
  servidor.on("/prioridad", HTTP_POST, pedirPrioridad);  // la solicitud de prioridad
}

void iniciarServidorWeb() {                         // deja el servidor funcionando
  registrarRutas();                                 // define las direcciones
  servidor.begin();                                 // empieza a escuchar
}

// =====================================================================
//  9. PLACA A (MAESTRO)
// =====================================================================

void configurarPinesMaestro() {                     // pines que usa la placa A
  configurarPinesLeds();                            // los tres LED del semáforo A
  pinMode(PIN_CAMARA, OUTPUT);                      // el indicador de cámara
}

void refrescarSalidasMaestro(Estado fase, unsigned long t) {
  char luzA, luzB;                                  // luces de A y de B (B se ignora aquí)
  calcularLuces(fase, t, luzA, luzB);
  escribirLed(luzA);                                // enciende el LED de A
  digitalWrite(PIN_CAMARA, enPrioridad(fase));
}

void avanzarCicloSiToca() {                         // pasa al siguiente estado cuando se acaba el tiempo
  if (estadoCumplido()) cambiarEstado(siguiente(estado));  // avanza si ya cumplió su duración
}

void tareaSemaforo(void*) {                         // tarea aparte: nada del Wi-Fi o la web la congela
  for (;;) {                                        // se repite para siempre
    portENTER_CRITICAL(&muxCiclo);
    avanzarCicloSiToca();
    Estado fase = estado;
    unsigned long t = tiempoEnEstado();
    portEXIT_CRITICAL(&muxCiclo);
    refrescarSalidasMaestro(fase, t);
    enviarPuntoDelCicloSiToca(fase, t);
    const TickType_t pausa = pdMS_TO_TICKS(T_PAUSA_TAREA);
    vTaskDelay(pausa > 0 ? pausa : 1);              // deja correr la web aun con un tick mayor de 5 ms
  }
}

void crearTareaSemaforo() {                         // lanza la tarea de semáforos
  xTaskCreate(tareaSemaforo, "semaforo", TAM_PILA_TAREA, NULL, PRIORIDAD_TAREA, NULL);  // nombre, pila y prioridad
}

void medirDistanciaSiToca() {                       // mide la señal cada T_LECTURA_RSSI
  static unsigned long tRssi = 0;                   // momento de la última medición
  if (!haPasado(tRssi, T_LECTURA_RSSI)) return;     // todavía no toca
  actualizarMedicion(hayCelular());                 // mide solo si hay un celular conectado
}

void imprimirLogMaestro() {                         // escribe una línea de estado por Serial
  Estado fase;
  unsigned long t;
  leerFaseMaestro(fase, t);
  char luzA, luzB;                                  // luces actuales
  calcularLuces(fase, t, luzA, luzB);
  Serial.printf("[A] celular %s | canal %d | RSSI %.1f dBm | dist %.1f m | boton %s | %s | A=%c B=%c\n",  // formato de la línea
                hayCelular() ? "CONECTADO" : "sin conectar", canalActual(),  // celular y canal
                rssiF, distanciaM(rssiF), habilitado ? "HABILITADO" : "bloqueado",  // señal, distancia, botón
                nombreEstado(fase), luzA, luzB);
}

void imprimirLogMaestroSiToca() {                   // imprime el log cada T_LOG
  static unsigned long tLog = 0;                    // momento del último log
  if (!haPasado(tLog, T_LOG)) return;               // todavía no toca
  imprimirLogMaestro();                             // escribe la línea
}

void iniciarMaestro() {                             // arranque completo de la placa A
  configurarPinesMaestro();                         // pines de salida
  escribirLed('R');
  registrarEventosWiFi();                           // escucha los eventos del Wi-Fi
  iniciarRedPropia();                               // crea la red Wi-Fi y activa ESP-NOW
  cambiarEstado(ROJO_1);
  crearTareaSemaforo();
  iniciarServidorWeb();                             // levanta la página
}

void cicloMaestro() {                               // lo que hace loop() en la placa A
  servidor.handleClient();                          // atiende a quien abra la página
  medirDistanciaSiToca();                           // actualiza distancia y botón
  imprimirLogMaestroSiToca();                       // informa por Serial
}

// =====================================================================
//  10. PLACA B (ESCLAVO)
//  Copia el punto del ciclo que manda A. Si deja de oírla, sigue el mismo
//  ciclo por su cuenta. No se conecta a ninguna red: solo escucha ESP-NOW
//  en el canal fijo CANAL_WIFI, el mismo de la red que crea la placa A.
// =====================================================================

portMUX_TYPE muxRx = portMUX_INITIALIZER_UNLOCKED;  // candado para compartir datos con el callback de ESP-NOW
uint8_t rxEstado = ROJO_1;                          // último estado recibido de A
unsigned long rxInicio = 0;                         // cuándo empezó ese estado, en el reloj de B
unsigned long ultimoMensaje = 0;                    // cuándo llegó el último paquete
bool rxNuevo = false;                               // true si hay un paquete sin procesar
bool sincronizado = false;                          // true si ya recibió al menos un paquete de A

bool decodificarPaquete(const uint8_t* datos, int len, Paquete& q) {  // valida y copia un paquete recibido
  if (len != (int)sizeof(Paquete)) return false;    // tamaño equivocado: no es nuestro
  memcpy(&q, datos, sizeof(q));                     // copia los bytes al paquete
  if (q.magia != MAGIA) return false;               // código equivocado: no es nuestro
  if (q.estado > P_RET_ROJO) return false;          // estado fuera de rango
  return true;                                      // paquete válido
}

void guardarPaquete(const Paquete& q) {             // deja el paquete listo para que loop() lo use
  unsigned long ahora = millis();                   // momento de llegada
  portENTER_CRITICAL(&muxRx);                       // evita lecturas a medias
  rxEstado = q.estado;                              // guarda el estado
  rxInicio = ahora - q.t;                           // calcula cuándo empezó ese estado en este reloj
  ultimoMensaje = ahora;                            // anota la llegada
  rxNuevo = true;                                   // avisa que hay un paquete nuevo
  portEXIT_CRITICAL(&muxRx);                        // libera el candado
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void alRecibir(const esp_now_recv_info_t* info, const uint8_t* datos, int len) {  // callback (núcleo Arduino 3.x)
#else
void alRecibir(const uint8_t* mac, const uint8_t* datos, int len) {               // callback (núcleo Arduino 2.x)
#endif
  Paquete q;                                        // aquí se copia lo recibido
  if (decodificarPaquete(datos, len, q)) guardarPaquete(q);  // solo se guarda si es válido
}

void fijarCanal(uint8_t canal) {                    // cambia el canal de radio
  esp_wifi_set_promiscuous(true);                   // necesario para poder cambiar el canal
  esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);  // fija el canal pedido
  esp_wifi_set_promiscuous(false);                  // vuelve al modo normal
}

void configurarPinesEsclavo() {                     // pines que usa la placa B
  configurarPinesLeds();                            // los tres LED del semáforo B
  escribirLed('R');                                 // arranca en rojo hasta oír a A
}

void prepararRadioEsclavo() {                       // deja la radio lista solo para ESP-NOW
  WiFi.persistent(false);                           // no guarda datos de red en la memoria flash
  WiFi.mode(WIFI_STA);                              // modo estación (sin conectarse a nada)
  WiFi.disconnect();                                // asegura que no hay conexión
  delay(200);                                       // espera a que se asiente
  fijarCanal(CANAL_WIFI);                           // se queda en el canal fijo de la red de A
}

void iniciarRecepcion() {                           // activa la recepción de paquetes de A
  if (!iniciarEspNow()) return;                     // si no arranca, B queda en rojo
  esp_now_register_recv_cb(alRecibir);              // registra la función que recibe los paquetes
}

bool tomarPaqueteNuevo(uint8_t &e, unsigned long &ini) {  // saca el paquete nuevo si lo hay
  bool nuevo;                                       // resultado
  portENTER_CRITICAL(&muxRx);                       // evita lecturas a medias
  nuevo = rxNuevo;                                  // ¿hay paquete nuevo?
  e = rxEstado;                                     // estado recibido
  ini = rxInicio;                                   // inicio de ese estado
  rxNuevo = false;                                  // se marca como procesado
  portEXIT_CRITICAL(&muxRx);                        // libera el candado
  return nuevo;                                     // dice si había paquete nuevo
}

unsigned long leerUltimoMensaje() {                 // lee de forma segura cuándo llegó el último paquete
  unsigned long um;                                 // resultado
  portENTER_CRITICAL(&muxRx);                       // evita lecturas a medias
  um = ultimoMensaje;                               // copia el dato
  portEXIT_CRITICAL(&muxRx);                        // libera el candado
  return um;                                        // entrega el dato
}

void sincronizarConPaquete(uint8_t e, unsigned long ini) {  // copia el punto del ciclo de A
  estado = (Estado)e;                               // mismo estado que A
  inicioEstado = ini;                               // mismo momento de inicio que A
  sincronizado = true;                              // desde ahora B puede funcionar sola
}

void recibirPaqueteSiHay() {                        // si llegó un paquete, se sincroniza con él
  uint8_t e;                                        // estado recibido
  unsigned long ini;                                // inicio del estado recibido
  if (!tomarPaqueteNuevo(e, ini)) return;           // no hay paquete nuevo
  sincronizarConPaquete(e, ini);                    // se copia el ciclo de A
}

unsigned long msSinPaquete() {                      // ms desde el último paquete de A
  unsigned long ahora = millis();                   // momento actual (se lee antes que el dato)
  unsigned long um = leerUltimoMensaje();           // momento del último paquete
  return (um > ahora) ? 0 : ahora - um;             // si llegó justo ahora, cuenta como 0
}

bool esclavoActivo(unsigned long sinPaquete) {      // true si B puede mostrar luces (no rojo de seguridad)
  return sincronizado && sinPaquete < T_AUTONOMO;   // ya sincronizó y A no lleva demasiado callada
}

void avanzarCicloAutonomoSiToca(unsigned long sinPaquete) {  // si A no se oye, B avanza el ciclo sola
  if (sinPaquete < T_RELEVO) return;                // A se oye bien: B solo la sigue
  if (!estadoCumplido()) return;                    // el estado aún no termina
  inicioEstado += duracion(estado);                 // el siguiente empieza justo cuando terminó este
  estado = siguiente(estado);                       // pasa al siguiente estado
}

char calcularLuzB() {                               // luz que le toca a B ahora
  char luzA, luzB;                                  // luces de A y de B
  calcularLuces(estado, tiempoEnEstado(), luzA, luzB);  // misma función que usa A
  return luzB;                                      // solo interesa la de B
}

void reafirmarCanalSiHaceFalta(unsigned long sinPaquete) {  // sin oír a A, vuelve a fijar el canal por seguridad
  static unsigned long tCanal = 0;                  // momento de la última reafirmación
  if (sinPaquete < T_SIN_ENLACE) return;            // el enlace está bien: no hace falta
  if (!haPasado(tCanal, T_REAFIRMAR_CANAL)) return; // todavía no toca
  fijarCanal(CANAL_WIFI);                           // aplica otra vez el canal fijo
}

const char* describirModoEsclavo(unsigned long sinPaquete, bool activo) {  // texto del modo actual de B
  if (!sincronizado) return "ESPERANDO AL MAESTRO";  // aún no ha oído a A
  if (!activo) return "MAESTRO PERDIDO (rojo)";     // A lleva demasiado callada
  if (sinPaquete < T_RELEVO) return "ENLACE OK";    // oye a A con normalidad
  return "AUTONOMO";                                // sigue el ciclo por su cuenta
}

void imprimirLogEsclavoSiToca(unsigned long sinPaquete, bool activo, char luz) {  // log de B cada T_LOG
  static unsigned long tLog = 0;                    // momento del último log
  if (!haPasado(tLog, T_LOG)) return;               // todavía no toca
  Serial.printf("[B] %s | canal %d | luz %c\n",     // formato de la línea
                describirModoEsclavo(sinPaquete, activo), canalActual(), luz);  // modo, canal real y luz
}

void iniciarEsclavo() {                             // arranque completo de la placa B
  configurarPinesEsclavo();                         // pines y rojo inicial
  prepararRadioEsclavo();                           // radio lista para ESP-NOW
  iniciarRecepcion();                               // empieza a escuchar a A
  cambiarEstado(ROJO_1);                            // ciclo local en su punto inicial
}

void cicloEsclavo() {                               // lo que hace loop() en la placa B
  recibirPaqueteSiHay();                            // 1. se sincroniza si llegó un paquete
  unsigned long sinPaquete = msSinPaquete();        // 2. mide cuánto lleva sin oír a A
  bool activo = esclavoActivo(sinPaquete);          // 3. decide si puede mostrar luces
  char luz = 'R';                                   // por seguridad, rojo por defecto
  if (activo) {                                     // si puede mostrar luces
    avanzarCicloAutonomoSiToca(sinPaquete);         // avanza sola si A no se oye
    luz = calcularLuzB();                           // calcula la luz que toca
  }
  escribirLed(luz);                                 // 4. enciende el LED
  reafirmarCanalSiHaceFalta(sinPaquete);            // 5. reafirma el canal si no oye a A
  imprimirLogEsclavoSiToca(sinPaquete, activo, luz);  // 6. informa por Serial
}

// =====================================================================
//  11. SETUP Y LOOP
// =====================================================================

bool esMaestro() {                                  // true si esta placa es la A
  return ROL == 'A';                                // según la constante de configuración
}

void setup() {                                      // se ejecuta una vez al encender
  Serial.begin(115200);                             // abre el monitor serie
  if (esMaestro()) iniciarMaestro();                // placa A: arranque del maestro
  else iniciarEsclavo();                            // placa B: arranque del esclavo
}

void loop() {                                       // se ejecuta una y otra vez
  if (esMaestro()) cicloMaestro();                  // placa A: trabajo del maestro
  else cicloEsclavo();                              // placa B: trabajo del esclavo
}
