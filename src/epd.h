#pragma once
#include <Arduino.h>
#include <SPI.h>

// Wiring: Waveshare 1.54inch e-Paper B V2 via UEXT connector on Olimex ESP32-POE-ISO
//
//   e-Paper module (P1)   UEXT pin   ESP32 GPIO
//   1  VCC (3.3V)         1          +3.3V
//   2  GND                2          GND
//   3  DIN (MOSI)         8          GPIO2
//   4  CLK (SCK)          9          GPIO14
//   5  CS                 10         GPIO5
//   6  D/C                6          GPIO13
//   7  RST                5          GPIO16
//   8  BUSY               4          GPIO36  (input-only)

#define EPD_SCK   14
#define EPD_MOSI   2
#define EPD_MISO  15  // not used by display but required for SPI.begin()
#define EPD_CS     5
#define EPD_DC    13
#define EPD_RST   16
#define EPD_BUSY  36  // GPIO36 is input-only on ESP32, perfect for reading BUSY

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define EPD_BUF_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)  // 5000 bytes per channel

class EPD {
public:
    void init();

    // Display image from two 5000-byte buffers.
    // black_buf: 0 = black pixel,  1 = white pixel
    // red_buf:   1 = red pixel,    0 = not-red pixel  (SSD1681 native: 1=chromatic)
    void display(const uint8_t* black_buf, const uint8_t* red_buf);

    void clear();   // fill white, no red
    void sleep();   // deep sleep - requires init() to wake

private:
    void reset();
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void waitBusy();
    void setRamAddress();
};
