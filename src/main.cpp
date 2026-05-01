#include <Arduino.h>
#include <ETH.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "epd.h"
#include "config.h"

// LAN8720 Ethernet on Olimex ESP32-POE-ISO
#define ETH_PHY_ADDR   0
#define ETH_PHY_POWER 12
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18

// User button (KEY1) on GPIO34: input-only, external pull-up, active-low
#define BUTTON_PIN    34
#define DEBOUNCE_MS   50

static bool eth_connected  = false;
static bool g_initial_done = false;
static bool g_force_refresh = false;
static EPD  epd;

static void onEthEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            ETH.setHostname("epaper-display");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.printf("[ETH] IP: %s\n", ETH.localIP().toString().c_str());
            eth_connected = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
        case ARDUINO_EVENT_ETH_STOP:
            eth_connected = false;
            break;
        default:
            break;
    }
}

// Read exactly `len` bytes from stream, with 5s idle timeout.
static bool readBytes(WiFiClient* s, uint8_t* buf, size_t len) {
    size_t remaining = len;
    unsigned long deadline = millis() + 5000;
    while (remaining > 0) {
        if (millis() > deadline) return false;
        int avail = s->available();
        if (avail <= 0) { delay(1); continue; }
        size_t chunk = min((size_t)avail, remaining);
        s->readBytes(buf, chunk);
        buf += chunk;
        remaining -= chunk;
        deadline = millis() + 5000;
    }
    return true;
}

static bool fetchAndDisplay(const char* url) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[HTTP] Error: %d\n", code);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();

    // --- Parse BMP header (54 bytes) ---
    uint8_t hdr[54];
    if (!readBytes(stream, hdr, 54)) {
        Serial.println("[BMP] Header read failed");
        http.end();
        return false;
    }
    if (hdr[0] != 'B' || hdr[1] != 'M') {
        Serial.println("[BMP] Not a BMP file");
        http.end();
        return false;
    }

    uint32_t pixel_offset = *(uint32_t*)(hdr + 10);
    int32_t  img_width    = *(int32_t*) (hdr + 18);
    int32_t  img_height   = *(int32_t*) (hdr + 22);
    uint16_t bpp          = *(uint16_t*)(hdr + 28);
    uint32_t compression  = *(uint32_t*)(hdr + 30);

    if (img_width != EPD_WIDTH || abs(img_height) != EPD_HEIGHT) {
        Serial.printf("[BMP] Wrong size: %dx%d (expected %dx%d)\n",
                      img_width, abs(img_height), EPD_WIDTH, EPD_HEIGHT);
        http.end();
        return false;
    }
    if (bpp != 24 || compression != 0) {
        Serial.printf("[BMP] Unsupported format: %dbpp, compression=%u\n", bpp, compression);
        http.end();
        return false;
    }

    // Skip any extra header bytes between end of BITMAPFILEHEADER+DIB and pixel data
    for (uint32_t i = 54; i < pixel_offset; i++) {
        uint8_t dummy;
        if (!readBytes(stream, &dummy, 1)) { http.end(); return false; }
    }

    // --- Convert BMP rows to e-paper buffers ---
    // Display is configured with Y-decrement mode starting at Y=199.
    // First bytes sent → displayed at bottom (Y=199).
    // BMP with positive height is stored bottom-up: file row 0 = image bottom.
    // So file row order maps directly to display order. No reordering needed.
    //
    // For top-down BMP (negative height): file row 0 = image top, which must
    // be placed at buffer index (height-1) so it's sent last → displayed at top.

    static uint8_t black_buf[EPD_BUF_SIZE];  // 0=black,  1=white
    static uint8_t red_buf[EPD_BUF_SIZE];    // 1=red,    0=not-red  (SSD1681 native)
    memset(black_buf, 0xFF, EPD_BUF_SIZE);
    memset(red_buf,   0x00, EPD_BUF_SIZE);

    bool top_down   = (img_height < 0);
    int  height     = abs(img_height);
    int  row_stride = ((img_width * 3 + 3) / 4) * 4;  // rows padded to 4 bytes
    uint8_t row_buf[((EPD_WIDTH * 3 + 3) / 4) * 4];   // 600 bytes for 200px

    for (int row = 0; row < height; row++) {
        if (!readBytes(stream, row_buf, row_stride)) {
            Serial.printf("[BMP] Row %d read failed\n", row);
            break;
        }

        int display_row = top_down ? (height - 1 - row) : row;

        for (int x = 0; x < img_width; x++) {
            uint8_t b = row_buf[x * 3 + 0];
            uint8_t g = row_buf[x * 3 + 1];
            uint8_t r = row_buf[x * 3 + 2];

            int display_x = (EPD_WIDTH - 1) - x;  // mirror X to match display orientation
            int byte_idx = display_row * (EPD_WIDTH / 8) + (display_x / 8);
            int bit_mask = 1 << (7 - (display_x % 8));

            if (r < COLOR_DARK_MAX && g < COLOR_DARK_MAX && b < COLOR_DARK_MAX) {
                black_buf[byte_idx] &= ~bit_mask;  // black pixel
            } else if (r > COLOR_RED_MIN && g < COLOR_RED_MAX && b < COLOR_RED_MAX) {
                red_buf[byte_idx] |= bit_mask;     // red pixel (1=red)
            }
            // else: white (black bit stays 1, red bit stays 0)
        }
    }

    http.end();

    Serial.println("[EPD] Updating display...");
    epd.init();
    epd.display(black_buf, red_buf);
    epd.sleep();
    Serial.println("[EPD] Done.");

    return true;
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] e-Paper display starting");

    pinMode(BUTTON_PIN, INPUT);

    // Register event handler before ETH.begin so no events are missed.
    // ARDUINO_EVENT_ETH_GOT_IP sets eth_connected; loop() triggers the
    // first fetch as soon as that flag goes true via !g_initial_done.
    // No polling loop needed — the LAN8720A surfaces link changes as
    // events through the ESP-IDF EMAC driver.
    WiFi.onEvent(onEthEvent);
    ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO,
              ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);

    Serial.println("[ETH] Waiting for link (event-driven)...");
}

void loop() {
    static unsigned long last_update = 0;
    static int  btn_prev = HIGH;
    static unsigned long btn_time = 0;

    // Debounced button poll: GPIO34 external pull-up, active-low
    int btn_now = digitalRead(BUTTON_PIN);
    if (btn_prev == HIGH && btn_now == LOW && millis() - btn_time > DEBOUNCE_MS) {
        btn_time = millis();
        Serial.println("[BTN] Manual refresh triggered");
        g_force_refresh = true;
    }
    btn_prev = btn_now;

    if (!eth_connected) {
        delay(10);
        return;
    }

    if (g_force_refresh || !g_initial_done || millis() - last_update >= REFRESH_INTERVAL_MS) {
        g_initial_done  = true;
        g_force_refresh = false;
        last_update = millis();
        fetchAndDisplay(IMAGE_URL);
    }
    delay(10);
}
