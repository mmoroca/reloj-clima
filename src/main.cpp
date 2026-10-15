// Reloj con tiempo atmosférico para el T-Keyboard (ESP32-C3, pantalla GC9D01N 40x160).
//
// - Hora por NTP, zona horaria Europe/Madrid (con cambio de hora). Sin red, la hora sigue
//   contando; se guarda cada 30 min y se restaura al arrancar hasta que vuelva NTP.
// - Tiempo por Open-Meteo (sin clave API) para la ciudad elegida en la configuración.
// - Configuración WiFi por portal: si no hay red guardada, o si mantienes ALT al encender,
//   la placa crea la red "RelojClima-XXXX" y la página de configuración está en 192.168.4.1.
// - Teclas: barra espaciadora cambia entre reloj y tiempo; ALT cambia el estilo del reloj.
//   Tras 5 s sin pulsar, la pantalla se apaga; cualquier tecla la enciende.
// - Icono de WiFi arriba a la derecha: blanco si hay red, rojo tachado si no.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Pines del T-Keyboard: los mismos que el firmware de Scrabble, que ya funciona en la placa.
#define TFT_BL_PIN 8
#define TFT_DC 2
#define TFT_MOSI 10
#define TFT_SCLK 20
#define TFT_CS -1
#define TFT_MISO 2
#define TFT_RST -1
#define KB_BACKLIGHT_PIN 9

#include <TFT_GC9D01N.h>
static TFT_GC9D01N_Class TFT_099;

#define SCREEN_BACKLIGHT 50
#define SCREEN_OFF_MS 5000UL
#define WEATHER_EVERY_MS (15UL * 60UL * 1000UL)
#define WEATHER_RETRY_MS 60000UL
#define TZ_MADRID "CET-1CEST,M3.5.0,M10.5.0/3"

// Filas de texto (eje de 40 px): y=7 es la línea de arriba, y=23 la de abajo.
#define ROW_TOP 7
#define ROW_BOTTOM 23
#define AXIS_A 40
#define AXIS_B 160

// ------------------------------------------------------------------ ciudades

struct City {
    const char *name;
    double lat;
    double lon;
};

// Se elige en el portal. Para añadir ciudades, agrega una línea aquí.
static const City CITIES[] = {
    // Capitales de cada comunidad autónoma y de Ceuta y Melilla.
    {"Sevilla", 37.3891, -5.9845},                        // Andalucía
    {"Zaragoza", 41.6488, -0.8891},                       // Aragón
    {"Oviedo", 43.3614, -5.8593},                         // Asturias
    {"Palma de Mallorca", 39.5696, 2.6502},               // Baleares
    {"Santa Cruz de Tenerife", 28.4636, -16.2518},        // Canarias
    {"Las Palmas de Gran Canaria", 28.1235, -15.4363},    // Canarias
    {"Santander", 43.4623, -3.8099},                      // Cantabria
    {"Toledo", 39.8628, -4.0273},                         // Castilla-La Mancha
    {"Valladolid", 41.6523, -4.7245},                     // Castilla y León
    {"Barcelona", 41.3874, 2.1686},                       // Cataluña
    {"Mérida", 38.9161, -6.3437},                         // Extremadura
    {"Santiago de Compostela", 42.8782, -8.5448},         // Galicia
    {"Logroño", 42.4627, -2.4449},                        // La Rioja
    {"Madrid", 40.4168, -3.7038},                         // Comunidad de Madrid
    {"Murcia", 37.9922, -1.1307},                         // Murcia
    {"Pamplona", 42.8125, -1.6458},                       // Navarra
    {"Vitoria-Gasteiz", 42.8467, -2.6716},                // País Vasco
    {"Valencia", 39.4699, -0.3763},                       // Comunidad Valenciana
    {"Ceuta", 35.8894, -5.3198},                          // Ceuta
    {"Melilla", 35.2923, -2.9381},                        // Melilla
    {"Tudela", 42.0614, -1.6045},                         // Navarra (no capital)
};
static const uint8_t CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

// ------------------------------------------------------------------ teclado

static const uint8_t rows[] = {0, 3, 18, 12, 11, 6, 7};
static const uint8_t cols[] = {1, 4, 5, 19, 13};
static const int rowCount = sizeof(rows) / sizeof(rows[0]);
static const int colCount = sizeof(cols) / sizeof(cols[0]);

#define KEY_ALT_C 0
#define KEY_ALT_R 4
#define KEY_SPACE_C 0
#define KEY_SPACE_R 5

static bool key_held[5][7];
static bool key_prev[5][7];
static bool key_edge[5][7];

static void scan_matrix() {
    for (int c = 0; c < colCount; c++) {
        pinMode(cols[c], OUTPUT);
        digitalWrite(cols[c], LOW);
        for (int r = 0; r < rowCount; r++) {
            pinMode(rows[r], INPUT_PULLUP);
            delay(1);
            bool now = (digitalRead(rows[r]) == LOW);
            key_edge[c][r] = now && !key_prev[c][r];
            key_prev[c][r] = now;
            key_held[c][r] = now;
            pinMode(rows[r], INPUT);
        }
        pinMode(cols[c], INPUT);
    }
}

// ------------------------------------------------------------------ dibujo

// Rectángulo relleno. a* es el eje de 40 px y b* el de 160 px, como en DispOneChar.
static void fill_rect(unsigned a0, unsigned a1, unsigned b0, unsigned b1, uint16_t color) {
    TFT_099.BlockWrite(a0, a1, b0, b1);
    unsigned long n = (unsigned long) (a1 - a0 + 1) * (b1 - b0 + 1);
    while (n--) TFT_099.WriteOneDot(color);
}

static void clear_screen() { fill_rect(0, AXIS_A - 1, 0, AXIS_B - 1, BLACK); }

static void text_line(const char *s, unsigned y) {
    TFT_099.DispStr((char *) s, 0, y, WHITE, BLACK);
}

static void show_lines(const char *top, const char *bottom) {
    clear_screen();
    text_line(top, ROW_TOP);
    text_line(bottom, ROW_BOTTOM);
}

// ------------------------------------------------------------------ estilos del reloj

// Colores RGB565.
#define C_WHITE 0xFFFF
#define C_CYAN 0x07FF
#define C_GREEN 0x07E0
#define C_YELLOW 0xFFE0
#define C_ORANGE 0xFD20
#define C_MAGENTA 0xF81F
#define C_BLUE 0x3DDF
#define C_GRAY 0x7BEF
#define C_SILVER 0xC618

// 0 blanco, 1 color por dígito, 2 nocturno (rojo sobre negro), 3 cyberpunk (negro sobre amarillo).
#define STYLE_COUNT 4
static uint8_t clock_style = 0;

// Dígitos 5x7 escalados x4 para la hora. Cada fila es un byte; los 5 bits bajos son las columnas.
#define BIG_S 4
#define BIG_COLS 5
#define BIG_ROWS 7
#define BIG_GAP 1

static const uint8_t DIGITS[10][BIG_ROWS] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
};
static const uint8_t GLYPH_COLON[BIG_ROWS] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
static const uint8_t GLYPH_DASH[BIG_ROWS] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
static const uint8_t GLYPH_BLANK[BIG_ROWS] = {0, 0, 0, 0, 0, 0, 0};

static const uint8_t *big_glyph(char ch) {
    if (ch >= '0' && ch <= '9') return DIGITS[ch - '0'];
    if (ch == ':') return GLYPH_COLON;
    if (ch == '-') return GLYPH_DASH;
    return GLYPH_BLANK;
}

// Dibuja un glifo grande. a_base es la base del eje de 40 px y b0 la columna inicial.
static void draw_big_glyph(const uint8_t *g, unsigned a_base, unsigned b0, uint16_t color) {
    for (int r = 0; r < BIG_ROWS; r++) {
        for (int c = 0; c < BIG_COLS; c++) {
            if (!((g[r] >> (BIG_COLS - 1 - c)) & 1)) continue;
            // La fila 0 es la de arriba, que tiene el eje a más alto.
            unsigned a_hi = a_base + BIG_ROWS * BIG_S - 1 - r * BIG_S;
            unsigned a_lo = a_hi - (BIG_S - 1);
            unsigned bl = b0 + c * BIG_S;
            fill_rect(a_lo, a_hi, bl, bl + BIG_S - 1, color);
        }
    }
}

// Color de cada posición (0..4 = H H : M M) según el estilo.
static uint16_t clock_color(int pos) {
    switch (clock_style) {
        case 1: {
            static const uint16_t per[5] = {C_CYAN, C_GREEN, C_YELLOW, C_ORANGE, C_MAGENTA};
            return per[pos];
        }
        case 2:
            return RED;    // nocturno
        case 3:
            return BLACK;  // cyberpunk
        default:
            return WHITE;
    }
}

// Fondo del reloj: amarillo solo en cyberpunk.
static uint16_t clock_bg() { return clock_style == 3 ? C_YELLOW : BLACK; }

// Escribe "HH:MM" centrado con glifos grandes. Si hh < 0, muestra "--:--".
static void draw_clock(int hh, int mm) {
    char txt[8];
    if (hh < 0) snprintf(txt, sizeof txt, "--:--");
    else snprintf(txt, sizeof txt, "%02d:%02d", hh, mm);

    const int n = 5;
    const int total_cols = n * BIG_COLS + (n - 1) * BIG_GAP;
    const unsigned total_px = total_cols * BIG_S;
    const unsigned b0 = (AXIS_B - total_px) / 2;
    const unsigned a_base = (AXIS_A - BIG_ROWS * BIG_S) / 2;

    fill_rect(0, AXIS_A - 1, 0, AXIS_B - 1, clock_bg());
    unsigned b = b0;
    for (int i = 0; i < n; i++) {
        draw_big_glyph(big_glyph(txt[i]), a_base, b, clock_color(i));
        b += (BIG_COLS + BIG_GAP) * BIG_S;
    }
}

// ------------------------------------------------------------------ iconos del tiempo (32x32)

#define ICON_W 32
enum Icon { ICON_NONE, ICON_SUN, ICON_PARTLY, ICON_CLOUD, ICON_FOG, ICON_RAIN, ICON_SNOW, ICON_STORM };
static uint16_t icon_img[ICON_W * ICON_W];  // y = 0 es la fila de arriba; x = 0 la de la izquierda

static void img_clear() { memset(icon_img, 0, sizeof icon_img); }

static void img_px(int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= ICON_W || y >= ICON_W) return;
    icon_img[y * ICON_W + x] = c;
}

static void img_disc(int cx, int cy, int r, uint16_t c) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) img_px(x, y, c);
}

static void img_rect(int x0, int y0, int x1, int y1, uint16_t c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) img_px(x, y, c);
}

static void img_line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        img_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void img_cloud(int ox, int oy, uint16_t c) {
    img_disc(ox + 12, oy + 18, 6, c);
    img_disc(ox + 19, oy + 14, 8, c);
    img_disc(ox + 25, oy + 19, 5, c);
    img_rect(ox + 6, oy + 18, ox + 25, oy + 23, c);
}

static void img_sun(int cx, int cy, int r) {
    img_disc(cx, cy, r, C_YELLOW);
    static const float dirs[8][2] = {
        {1, 0}, {0.7071f, 0.7071f}, {0, 1}, {-0.7071f, 0.7071f},
        {-1, 0}, {-0.7071f, -0.7071f}, {0, -1}, {0.7071f, -0.7071f},
    };
    for (int i = 0; i < 8; i++) {
        float x0 = cx + dirs[i][0] * (r + 2), y0 = cy + dirs[i][1] * (r + 2);
        float x1 = cx + dirs[i][0] * (r + 5), y1 = cy + dirs[i][1] * (r + 5);
        img_line((int) lroundf(x0), (int) lroundf(y0), (int) lroundf(x1), (int) lroundf(y1), C_ORANGE);
    }
}

// Escribe la imagen en la pantalla, con la esquina superior izquierda del icono en b0.
// El eje de 40 px va a a = 4..35 (centrado). El eje a crece hacia arriba, así que la fila 0 queda arriba.
static void draw_icon(int kind, unsigned b0) {
    img_clear();
    switch (kind) {
        case ICON_SUN:
            img_sun(16, 16, 7);
            break;
        case ICON_PARTLY:
            img_sun(11, 11, 5);
            img_cloud(5, 5, C_WHITE);
            break;
        case ICON_CLOUD:
            img_cloud(0, 0, C_SILVER);
            break;
        case ICON_FOG:
            for (int y = 8; y <= 28; y += 5) img_rect(3, y, 28, y + 1, C_SILVER);
            break;
        case ICON_RAIN:
            img_cloud(0, -4, C_SILVER);
            for (int x = 9; x <= 23; x += 7) img_line(x, 22, x - 2, 28, C_BLUE);
            break;
        case ICON_SNOW:
            img_cloud(0, -4, C_SILVER);
            for (int x = 8; x <= 24; x += 8) {
                img_px(x, 25, C_WHITE);
                img_px(x - 1, 25, C_WHITE);
                img_px(x + 1, 25, C_WHITE);
                img_px(x, 24, C_WHITE);
                img_px(x, 26, C_WHITE);
            }
            break;
        case ICON_STORM:
            img_cloud(0, -4, C_GRAY);
            img_line(17, 21, 13, 26, C_YELLOW);
            img_line(13, 26, 18, 26, C_YELLOW);
            img_line(18, 26, 14, 31, C_YELLOW);
            break;
        default:
            break;
    }

    const unsigned a0 = (AXIS_A - ICON_W) / 2;
    TFT_099.BlockWrite(a0, a0 + ICON_W - 1, b0, b0 + ICON_W - 1);
    // La ventana se escribe con a rápido y b lento: para cada columna b, las filas de a de abajo a arriba.
    for (int bb = 0; bb < ICON_W; bb++) {
        for (int aa = 0; aa < ICON_W; aa++) {
            int y = ICON_W - 1 - aa;
            TFT_099.WriteOneDot(icon_img[y * ICON_W + bb]);
        }
    }
}

// ------------------------------------------------------------------ tiempo

struct Weather {
    bool valid;
    float temp;
    int hum;
    int code;
};
static Weather weather = {false, 0.0f, 0, -1};

// Texto corto en español para la vista de tiempo (16 celdas como máximo).
static const char *wmo_short(int code) {
    switch (code) {
        case 0: return "Despejado";
        case 1: return "Casi despejado";
        case 2: return "Parc. nublado";
        case 3: return "Nublado";
        case 45: case 48: return "Niebla";
        case 51: case 53: case 55: return "Llovizna";
        case 56: case 57: return "Llovizna helada";
        case 61: return "Lluvia débil";
        case 63: return "Lluvia";
        case 65: return "Lluvia fuerte";
        case 66: case 67: return "Lluvia helada";
        case 71: case 73: case 75: return "Nieve";
        case 77: return "Granizo fino";
        case 80: return "Chubasco débil";
        case 81: return "Chubascos";
        case 82: return "Chub. fuertes";
        case 85: case 86: return "Nevadas";
        case 95: return "Tormenta";
        case 96: case 99: return "Tormenta granizo";
        default: return "Sin datos";
    }
}

// Icono según el código WMO.
static int wmo_icon(int code) {
    if (code == 0) return ICON_SUN;
    if (code == 1 || code == 2) return ICON_PARTLY;
    if (code == 3) return ICON_CLOUD;
    if (code == 45 || code == 48) return ICON_FOG;
    if (code >= 51 && code <= 67) return ICON_RAIN;
    if (code >= 71 && code <= 77) return ICON_SNOW;
    if (code >= 80 && code <= 82) return ICON_RAIN;
    if (code == 85 || code == 86) return ICON_SNOW;
    if (code >= 95 && code <= 99) return ICON_STORM;
    return ICON_NONE;
}

// Pide las condiciones actuales a Open-Meteo. v1: no verifica el certificado TLS (datos públicos).
static bool fetch_weather(const City &city) {
    WiFiClientSecure client;
    client.setInsecure();

    char url[256];
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code",
             city.lat, city.lon);

    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url)) {
        Serial.println("Tiempo: no se pudo abrir la conexión");
        return false;
    }
    int status = http.GET();
    if (status != 200) {
        Serial.printf("Tiempo: HTTP %d\n", status);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("Tiempo: JSON no válido (%s)\n", err.c_str());
        return false;
    }
    JsonObject cur = doc["current"];
    if (cur.isNull()) {
        Serial.println("Tiempo: falta el bloque 'current'");
        return false;
    }
    weather.temp = cur["temperature_2m"] | 0.0f;
    weather.hum = cur["relative_humidity_2m"] | 0;
    weather.code = cur["weather_code"] | -1;
    weather.valid = true;
    Serial.printf("Tiempo: %.1f C, %d%%, codigo %d\n", weather.temp, weather.hum, weather.code);
    return true;
}

static bool time_ready() { return time(nullptr) > 1700000000; }

static bool local_hm(int &hh, int &mm) {
    if (!time_ready()) return false;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    hh = t.tm_hour;
    mm = t.tm_min;
    return true;
}

// La hora sigue corriendo mientras la placa está encendida. Para no perderla al reiniciar,
// se guarda cada 30 min y, al arrancar, se restaura esa hora (aproximada) hasta que llegue NTP.
#define TIME_SAVE_EVERY_MS (30UL * 60UL * 1000UL)
static bool restore_saved_time() {
    Preferences p;
    p.begin("clima", true);
    uint64_t t = p.getULong64("last_epoch", 0);
    p.end();
    if (t <= 1700000000ULL) return false;
    struct timeval tv = {(time_t) t, 0};
    settimeofday(&tv, nullptr);
    Serial.println("Hora aproximada restaurada de la última vez");
    return true;
}

static void save_time_maintenance() {
    static unsigned long next_save_ms = 0;
    if (!time_ready()) return;
    unsigned long now = millis();
    if (now < next_save_ms) return;
    next_save_ms = now + TIME_SAVE_EVERY_MS;
    Preferences p;
    p.begin("clima", false);
    p.putULong64("last_epoch", (uint64_t) time(nullptr));
    p.end();
}

// ------------------------------------------------------------------ configuración

struct Config {
    String ssid;
    String pass;
    uint8_t city;
    bool valid;
};
static Config cfg;

static uint8_t city_index_of(const char *name) {
    for (uint8_t i = 0; i < CITY_COUNT; i++) {
        if (strcmp(CITIES[i].name, name) == 0) return i;
    }
    return 0;
}

static Config load_config() {
    Preferences p;
    p.begin("clima", true);
    Config c;
    c.ssid = p.getString("ssid", "");
    c.pass = p.getString("pass", "");
    // La ciudad se guarda por nombre, no por índice, para que los cambios de la lista no la cambien.
    String city_name = p.getString("city_name", "Tudela");
    p.end();
    c.valid = c.ssid.length() > 0;
    c.city = city_index_of(city_name.c_str());
    return c;
}

static WebServer web(80);
static DNSServer dns;
static int scan_count = 0;

static String html_escape(const String &s) {
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

static void portal_page() {
    String h;
    h.reserve(2048);
    h += "<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Reloj clima</title></head>"
         "<body style='font-family:sans-serif;max-width:420px;margin:20px auto'>"
         "<h2>Configurar reloj</h2><form method='POST' action='/save'>"
         "<label>Red WiFi<br><input name='ssid' list='redes' required "
         "style='width:100%;font-size:18px'></label>"
         "<datalist id='redes'>";
    for (int i = 0; i < scan_count; i++) {
        h += "<option value='" + html_escape(WiFi.SSID(i)) + "'>";
    }
    h += "</datalist><br><br>"
         "<label>Contraseña<br><input name='pass' type='password' "
         "style='width:100%;font-size:18px'></label><br><br>"
         "<label>Ciudad<br><select name='city' style='width:100%;font-size:18px'>";
    for (uint8_t i = 0; i < CITY_COUNT; i++) {
        h += "<option value='" + String(i) + "'";
        if (cfg.valid && cfg.city == i) h += " selected";
        h += ">";
        h += CITIES[i].name;
        h += "</option>";
    }
    h += "</select></label><br><br>"
         "<button style='font-size:18px;padding:10px 20px'>Guardar</button>"
         "</form></body></html>";
    web.send(200, "text/html; charset=utf-8", h);
}

static String pend_ssid, pend_pass;
static uint8_t pend_city = 0;
enum PortalTest { PT_IDLE, PT_RUNNING, PT_OK };
static PortalTest portal_test = PT_IDLE;
static unsigned long portal_test_start = 0;
static unsigned long portal_restart_at = 0;

static void portal_save() {
    String ssid = web.arg("ssid");
    if (ssid.length() == 0) {
        web.send(400, "text/plain; charset=utf-8", "Falta la red WiFi");
        return;
    }
    if (portal_test == PT_RUNNING) {
        web.send(409, "text/plain; charset=utf-8", "Ya hay una prueba en curso");
        return;
    }
    int city = web.arg("city").toInt();
    pend_city = (city >= 0 && city < CITY_COUNT) ? (uint8_t) city : 0;
    pend_ssid = ssid;
    pend_pass = web.arg("pass");

    // No se guarda nada todavía: primero se comprueba que la red conecta.
    portal_test = PT_RUNNING;
    portal_test_start = millis();
    WiFi.begin(pend_ssid.c_str(), pend_pass.c_str());
    char shown[21];
    strncpy(shown, pend_ssid.c_str(), sizeof shown - 1);
    shown[sizeof shown - 1] = '\0';
    show_lines("Probando red...", shown);
    Serial.printf("Portal: probando la red %s\n", pend_ssid.c_str());

    web.send(200, "text/html; charset=utf-8",
             "<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif'>"
             "<h2>Probando la red</h2><p>Mira la pantalla del reloj. Si conecta, se guarda y el reloj "
             "se reinicia solo. Si no conecta, vuelve a esta página y repite los datos.</p></body></html>");
}

// Guarda la red y la ciudad que acaban de funcionar.
static void portal_commit() {
    Preferences p;
    p.begin("clima", false);
    p.putString("ssid", pend_ssid);
    p.putString("pass", pend_pass);
    p.putString("city_name", CITIES[pend_city].name);
    p.end();
}

// Portal de configuración. Solo sale reiniciando, y únicamente cuando la red de prueba conectó.
static void run_portal() {
    Serial.println("Modo configuración: red RelojClima");
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
    scan_count = WiFi.scanNetworks();
    if (scan_count < 0) scan_count = 0;

    char ap[24];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(ap, sizeof ap, "RelojClima-%04X", (unsigned) (mac & 0xFFFF));
    WiFi.softAP(ap);
    IPAddress ip = WiFi.softAPIP();
    show_lines(ap, ip.toString().c_str());
    Serial.printf("Red %s, abre http://%s\n", ap, ip.toString().c_str());

    dns.start(53, "*", ip);
    web.on("/", HTTP_GET, portal_page);
    web.on("/save", HTTP_POST, portal_save);
    web.onNotFound([]() {
        web.sendHeader("Location", "http://192.168.4.1/", true);
        web.send(302, "text/plain", "");
    });
    web.begin();

    while (true) {
        dns.processNextRequest();
        web.handleClient();

        if (portal_test == PT_RUNNING) {
            if (WiFi.status() == WL_CONNECTED) {
                portal_commit();
                portal_test = PT_OK;
                portal_restart_at = millis() + 2000;
                show_lines("Red correcta", "Reiniciando...");
                Serial.println("Portal: red correcta, guardada; reiniciando");
            } else if (millis() - portal_test_start > 25000) {
                WiFi.disconnect();
                portal_test = PT_IDLE;
                show_lines("No conecta la red", "Repite en la web");
                Serial.println("Portal: la red de prueba no conectó");
            }
        }
        if (portal_test == PT_OK && millis() > portal_restart_at) ESP.restart();
        delay(5);
    }
}

// ------------------------------------------------------------------ icono de WiFi

// Arriba a la derecha: eje a 30..39 y columnas b 146..158. Verde/blanco si hay red, rojo tachado si no.
#define WIFI_W 13
#define WIFI_H 10
#define WIFI_A0 30
#define WIFI_B0 146
static uint16_t wifi_img[WIFI_W * WIFI_H];
static int wifi_icon_shown = -1;  // -1: todavía no dibujado

static void draw_status_icon(bool connected, uint16_t bg) {
    const uint16_t ink = connected ? (bg == BLACK ? WHITE : BLACK) : RED;
    for (int i = 0; i < WIFI_W * WIFI_H; i++) wifi_img[i] = bg;

    // Tres arcos concéntricos y un punto, en el abanico de 90° hacia arriba.
    const float cx = 6.0f, cy = 9.0f;
    const float radii[3] = {3.0f, 6.0f, 9.0f};
    for (int y = 0; y < WIFI_H; y++) {
        for (int x = 0; x < WIFI_W; x++) {
            float dx = x - cx, dy = cy - y;
            if (dy < fabsf(dx)) continue;  // fuera del abanico
            float d = sqrtf(dx * dx + dy * dy);
            bool on = d <= 1.3f;           // punto central
            for (int k = 0; k < 3; k++)
                if (fabsf(d - radii[k]) < 0.75f) on = true;
            if (on) wifi_img[y * WIFI_W + x] = ink;
        }
    }
    if (!connected) {
        // Tachado: diagonal de arriba-izquierda a abajo-derecha.
        for (int x = 0; x < WIFI_W; x++) {
            int y = (int) lroundf(x * (WIFI_H - 1) / (float) (WIFI_W - 1));
            wifi_img[y * WIFI_W + x] = RED;
            if (y + 1 < WIFI_H) wifi_img[(y + 1) * WIFI_W + x] = RED;
        }
    }

    TFT_099.BlockWrite(WIFI_A0, WIFI_A0 + WIFI_H - 1, WIFI_B0, WIFI_B0 + WIFI_W - 1);
    for (int bb = 0; bb < WIFI_W; bb++) {
        for (int aa = 0; aa < WIFI_H; aa++) {
            int y = WIFI_H - 1 - aa;
            TFT_099.WriteOneDot(wifi_img[y * WIFI_W + bb]);
        }
    }
}

// ------------------------------------------------------------------ estado de la interfaz

enum View { VIEW_CLOCK, VIEW_WEATHER };
static View view = VIEW_CLOCK;
static bool screen_on = true;
static unsigned long last_key_ms = 0;
static bool redraw = true;
static int shown_key = -2;  // minuto mostrado; -1 si no hay hora
static unsigned long next_weather_ms = 0;
static unsigned long next_wifi_check_ms = 0;

// Vista de tiempo: icono a la izquierda (32 px), temperatura y humedad arriba y descripción abajo.
static void draw_weather_view() {
    clear_screen();
    if (!weather.valid) {
        text_line("Sin datos del tiempo", ROW_TOP);
        text_line(WiFi.status() == WL_CONNECTED ? "Actualizando..." : "Sin WiFi", ROW_BOTTOM);
        return;
    }
    draw_icon(wmo_icon(weather.code), 0);
    char line1[20];
    snprintf(line1, sizeof line1, "%d grados, %d%%", (int) lroundf(weather.temp), weather.hum);
    TFT_099.DispStr(line1, 32, ROW_TOP, WHITE, BLACK);
    TFT_099.DispStr((char *) wmo_short(weather.code), 32, ROW_BOTTOM, WHITE, BLACK);
}

static void draw_clock_view(int key) {
    if (key < 0) {
        draw_clock(-1, -1);
    } else {
        draw_clock(key / 60, key % 60);
    }
}

static void save_style() {
    Preferences p;
    p.begin("clima", false);
    p.putUChar("style", clock_style);
    p.end();
}

static void run_clock() {
    unsigned long now = millis();
    scan_matrix();

    bool any = false;
    bool space = false;
    for (int c = 0; c < colCount; c++) {
        for (int r = 0; r < rowCount; r++) {
            if (key_edge[c][r]) {
                any = true;
                if (c == KEY_SPACE_C && r == KEY_SPACE_R) space = true;
            }
        }
    }

    if (any) {
        last_key_ms = now;
        if (!screen_on) {
            TFT_099.backlight(SCREEN_BACKLIGHT);
            screen_on = true;
            redraw = true;
        }
    } else if (screen_on && now - last_key_ms > SCREEN_OFF_MS) {
        TFT_099.backlight(0);
        screen_on = false;
    }

    if (key_edge[KEY_ALT_C][KEY_ALT_R] && view == VIEW_CLOCK) {
        clock_style = (clock_style + 1) % STYLE_COUNT;
        save_style();
        redraw = true;
    }

    if (space) {
        view = (view == VIEW_CLOCK) ? VIEW_WEATHER : VIEW_CLOCK;
        redraw = true;
    }

    if (!screen_on) return;

    bool drew = false;
    if (view == VIEW_CLOCK) {
        int hh = 0, mm = 0;
        int key = local_hm(hh, mm) ? hh * 60 + mm : -1;
        if (redraw || key != shown_key) {
            draw_clock_view(key);
            shown_key = key;
            redraw = false;
            drew = true;
        }
    } else if (redraw) {
        draw_weather_view();
        redraw = false;
        drew = true;
    }

    bool wifi_now = (WiFi.status() == WL_CONNECTED);
    if (drew || wifi_icon_shown != (wifi_now ? 1 : 0)) {
        draw_status_icon(wifi_now, view == VIEW_CLOCK ? clock_bg() : BLACK);
        wifi_icon_shown = wifi_now ? 1 : 0;
    }
}

static void time_maintenance() {
    static bool ntp_started = false;
    static bool time_logged = false;
    if (!ntp_started && WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi conectado, IP %s\n", WiFi.localIP().toString().c_str());
        configTzTime(TZ_MADRID, "pool.ntp.org", "time.google.com");
        ntp_started = true;
    }
    if (ntp_started && !time_logged && time_ready()) {
        Serial.println("Hora sincronizada");
        time_logged = true;
        redraw = true;
    }
}

static void weather_maintenance() {
    if (WiFi.status() != WL_CONNECTED) return;
    unsigned long now = millis();
    if (now < next_weather_ms) return;

    bool ok = fetch_weather(CITIES[cfg.city]);
    next_weather_ms = now + (ok ? WEATHER_EVERY_MS : WEATHER_RETRY_MS);
    if (ok) redraw = true;
}

static void wifi_maintenance() {
    unsigned long now = millis();
    if (now < next_wifi_check_ms) return;
    next_wifi_check_ms = now + 30000UL;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi caído, reconectando");
        WiFi.disconnect();
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    }
}

// ------------------------------------------------------------------ arranque

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Reloj con tiempo atmosférico iniciando");

    for (int r = 0; r < rowCount; r++) pinMode(rows[r], INPUT);
    for (int c = 0; c < colCount; c++) pinMode(cols[c], INPUT_PULLUP);
    pinMode(KB_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(KB_BACKLIGHT_PIN, HIGH);

    memset(key_held, 0, sizeof key_held);
    memset(key_prev, 0, sizeof key_prev);
    memset(key_edge, 0, sizeof key_edge);
    scan_matrix();  // lee ALT antes de arrancar la interfaz
    memset(key_edge, 0, sizeof key_edge);
    bool alt_at_boot = key_held[KEY_ALT_C][KEY_ALT_R];

    SPI.end();
    TFT_099.begin();
    TFT_099.DispColor(0, 0, TFT_WIDTH, TFT_HEIGHT, BLACK);
    TFT_099.backlight(SCREEN_BACKLIGHT);

    cfg = load_config();
    {
        Preferences p;
        p.begin("clima", true);
        clock_style = p.getUChar("style", 0) % STYLE_COUNT;
        p.end();
    }
    if (alt_at_boot || !cfg.valid) {
        run_portal();  // no vuelve
    }

    // Conexión WiFi sin bloquear: el teclado y la pantalla siguen respondiendo mientras conecta.
    restore_saved_time();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    next_wifi_check_ms = millis() + 30000UL;
    Serial.println("Conectando WiFi en segundo plano");

    last_key_ms = millis();
    redraw = true;
}

void loop() {
    run_clock();
    wifi_maintenance();
    time_maintenance();
    save_time_maintenance();
    weather_maintenance();
    delay(20);
}
