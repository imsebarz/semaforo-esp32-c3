# Semáforos A/B con ESP32-C3

Prototipo de dos semáforos para una maqueta. La placa A crea la red Wi-Fi, sirve la página de prioridad y envía la fase del ciclo a B mediante ESP-NOW.

## Cargar las placas

1. Instala el paquete **esp32** de Espressif en Arduino IDE. Selecciona tu modelo de ESP32-C3 y su puerto.
2. Abre `semaforoAB/semaforoAB.ino`. El archivo `pagina.h` debe estar en la misma carpeta.
3. Carga el sketch con `ROL = 'A'` en la placa que crea la red. Para cargar la segunda placa, cambia esa constante a `'B'` y selecciona su puerto. Ambas usan el mismo canal de radio.

## Probar la prioridad

Conecta el celular a **Semaforo_A** con la clave configurada en `AP_CLAVE`. Abre [la página del semáforo](http://192.168.4.1). El botón se habilita cuando el celular está en rango, la vía A no tiene verde y no hay una prioridad en curso.

Con B en verde, pulsa **Solicitar prioridad**. La página debe indicar el envío al pulsar. Comprueba que el contador aumente una sola vez, que B pase a amarillo durante 3 segundos y que ambas placas muestren rojo durante 2 segundos antes del verde de prioridad de A. Si ambas ya estaban en rojo, la secuencia empieza por los 2 segundos de despeje.

Si el celular pierde la conexión, consulta el monitor serie de A a **115200 baudios**. El mensaje `Celular desconectado` registra una salida de la red y `RED CREADA` vuelve a aparecer si la placa arranca de nuevo.
