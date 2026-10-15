# Reloj con tiempo atmosférico para T-Keyboard

[🇬🇧 English](README.md) · 🇪🇸 Español

Reloj y estación meteorológica para el **T-Keyboard** de LilyGO (ESP32-C3 con pantalla GC9D01N de 0,99", 40×160 px). Muestra la hora en grande y, con la barra espaciadora, el tiempo actual de una ciudad española.

> por **@mmoroca & Leo.ai** · 2026 · [Licencia MIT](LICENSE)

![Plataforma](https://img.shields.io/badge/plataforma-ESP32--C3-blue) ![Licencia](https://img.shields.io/badge/licencia-MIT-green)

## Características

- **Reloj grande** sin segundos, con la hora por NTP y zona horaria `Europe/Madrid` (cambio de hora automático).
- **Tiempo actual** de [Open-Meteo](https://open-meteo.com) (sin clave API): temperatura, humedad y un icono a color (sol, nubes, lluvia, nieve, niebla, tormenta).
- **Portal de configuración**: no hay contraseñas WiFi en el código. La placa crea su propia red y la configuras desde el móvil o el ordenador.
- **Ciudad elegida** de una lista de capitales de comunidades autónomas, Ceuta y Melilla.
- **4 estilos de reloj**, guardados entre reinicios.
- **Apagado automático** de la pantalla a los 5 s sin pulsar ninguna tecla. Cualquier tecla la enciende.
- **Arranque sin bloqueos**: el teclado y la pantalla responden mientras la placa conecta el WiFi.
- **Icono de WiFi** arriba a la derecha: blanco si hay red, rojo tachado si no.
- **Hora sin red**: sin conexión, el reloj sigue contando mientras la placa tenga alimentación y, tras reiniciar, retoma la última hora guardada.

## Hardware

- LilyGO **T-Keyboard** (ESP32-C3, pantalla GC9D01N de 0,99" 40×160). Página del producto: <https://lilygo.cc/products/t-keyboard>
- Cable USB-C.

La documentación pública no menciona un reloj en tiempo real (RTC) con pila, así que la hora se sincroniza por NTP y, sin red, es aproximada (ver [Hora](#hora)).

## Controles

| Tecla | Vista de hora | Vista de tiempo |
|---|---|---|
| **Barra espaciadora** | Cambia a la vista de tiempo | Vuelve al reloj |
| **ALT** | Cambia el estilo del reloj (0 → 1 → 2 → 3 → 0) | Sin efecto |
| **ALT mantenida al encender** | Abre el portal de configuración | |
| **Cualquier tecla** | Enciende la pantalla (tras 5 s sin uso se apaga) | Igual |

### Estilos del reloj

| Nº | Estilo | Descripción |
|---|---|---|
| 0 | Blanco | Dígitos blancos sobre negro |
| 1 | Color por dígito | Cada dígito tiene su color |
| 2 | Nocturno | Dígitos rojos sobre negro |
| 3 | Cyberpunk | Fondo amarillo completo con dígitos negros |

## Primera configuración (portal)

El portal se abre si no hay red guardada, o si mantienes pulsada **ALT** al encender.

1. Enciende la placa. La pantalla muestra el modo de configuración.
2. En el móvil o el ordenador, conéctate a la red WiFi **`RelojClima-XXXX`** (las cuatro últimas letras cambian en cada placa). Es una red abierta, sin contraseña.
3. Debería abrirse la página de configuración automáticamente. Si no, abre **http://192.168.4.1** en el navegador.
4. Elige tu red WiFi (SSID), escribe su contraseña y elige la ciudad.
5. Pulsa **Guardar**. La placa **prueba primero la red** (hasta 25 s):
   - Si conecta, guarda los ajustes y **se reinicia sola**.
   - Si no conecta, muestra *«No conecta la red»* y sigue en el portal sin guardar. Revisa la contraseña y vuelve a intentarlo.

Si más adelante la red guardada no está al alcance, el reloj sigue funcionando (ver [Hora](#hora)). Para cambiar de red o de ciudad, mantén **ALT** al encender.

## Hora

- Sincronizada con `pool.ntp.org` y `time.google.com` cuando hay WiFi. Zona horaria: `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Madrid).
- **Sin red:** el reloj sigue contando mientras la placa tenga alimentación, aunque el oscilador interno puede desviarse varios minutos al día.
- **Cada 30 minutos** se guarda la hora sincronizada en la memoria flash. Tras reiniciar sin red, la placa muestra la última hora guardada. Irá atrasada en el tiempo que haya estado apagada hasta que vuelva la red y NTP la corrija.
- El **icono WiFi rojo tachado** indica que la hora y el tiempo pueden ser aproximados.

## Tiempo

- Datos de [Open-Meteo](https://open-meteo.com) (`current`: temperatura, humedad relativa y código de tiempo WMO).
- Se actualiza cada **15 minutos**; si falla, reintenta a los **60 s**.
- La vista de tiempo muestra `NN grados, HH%` en la primera línea y una descripción corta (hasta 16 caracteres) en la segunda.
- Códigos WMO: 0 despejado; 1–2 poco nuboso; 3 nublado; 45–48 niebla; 51–67 y 80–82 lluvia; 71–77 y 85–86 nieve; 95–99 tormenta.

### Ciudades

Sevilla, Zaragoza, Oviedo, Palma de Mallorca, Santa Cruz de Tenerife, Las Palmas de Gran Canaria, Santander, Toledo, Valladolid, Barcelona, Mérida, Santiago de Compostela, Logroño, Madrid, Murcia, Pamplona, Vitoria-Gasteiz, Valencia, Ceuta, Melilla y Tudela (por defecto). Para añadir o cambiar ciudades, edita la tabla `CITIES` en `src/main.cpp`.

## Compilar y subir (VSCode + PlatformIO)

1. Instala [Visual Studio Code](https://code.visualstudio.com/) y la extensión **PlatformIO IDE**.
2. Abre esta carpeta con **Archivo → Abrir carpeta**.
3. Espera a que PlatformIO instale la plataforma y las librerías (solo la primera vez).
4. Pulsa **Build** (✓ en la barra inferior) y después **Upload** (→).
5. Si la subida falla, pon el T-Keyboard en modo descarga: mantén **BOOT** pulsado, pulsa **RESET** (o conecta el USB) y suelta **BOOT**. Más detalles en el [README del T-Keyboard](https://github.com/Xinyuan-LilyGO/T-keyboard).

Desde la terminal (con PlatformIO CLI instalado):

```bash
pio run -e T-Keyboard-clima                 # compilar
pio run -e T-Keyboard-clima -t upload       # subir
pio device monitor -b 115200                # monitor serie
```

El `platformio.ini` usa `espressif32 @ 6.1` con la placa `ttgo-t-oi-plus`, y la tabla de particiones de `partitions_clima.csv` (una sola aplicación, sin OTA).

## Notas de seguridad

- La contraseña WiFi se guarda en la memoria flash de la placa (espacio `clima` de `Preferences`).
- El portal de configuración es una **red abierta sin contraseña**. Mientras está activo, cualquiera que esté al alcance puede conectarse, así que configura la placa en casa.
- La petición HTTPS a Open-Meteo usa `WiFiClientSecure::setInsecure()`, que **no verifica el certificado del servidor**. Es una decisión de compromiso: los datos son meteorológicos públicos y no se envían credenciales a ese servicio, pero alguien en la misma red podría falsear los datos del tiempo. Úsalo solo en redes de confianza.

## Limitaciones

- Sin RTC con pila: tras un corte de corriente, la hora es aproximada hasta que NTP la sincroniza.
- Algunos textos en pantalla evitan la letra **ó** mientras hay pendiente un problema de la fuente con los acentos.
- La API gratuita de Open-Meteo es solo para **uso no comercial** (menos de 10 000 llamadas al día). Para uso comercial hace falta un plan de pago.

## Créditos y licencias

- **Autor:** @mmoroca & Leo.ai (2026).
- **Licencia:** [MIT](LICENSE).
- **Driver de pantalla:** `lib/TFT_GC9D01N-Patched` (MIT, Copyright (c) 2022 Xinyuan-LilyGO), del [proyecto T-Keyboard](https://github.com/Xinyuan-LilyGO/T-keyboard).
- **Librerías:** [ArduinoJson](https://arduinojson.org) (MIT), Arduino-ESP32 (LGPL-2.1).
- **Datos del tiempo:** [Open-Meteo.com](https://open-meteo.com), bajo [CC BY 4.0](https://open-meteo.com/en/licence). Mantén la atribución si redistribuyes los datos.
