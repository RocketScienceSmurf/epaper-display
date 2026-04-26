#include "epd.h"

void EPD::init() {
    pinMode(EPD_CS,   OUTPUT);
    pinMode(EPD_DC,   OUTPUT);
    pinMode(EPD_RST,  OUTPUT);
    pinMode(EPD_BUSY, INPUT);

    digitalWrite(EPD_CS, HIGH);

    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

    reset();

    waitBusy();
    sendCommand(0x12);  // Software reset
    waitBusy();

    sendCommand(0x01);  // Driver output control: 200 gates
    sendData(0xC7);     // MUX = 199 (200 lines)
    sendData(0x00);
    sendData(0x01);

    sendCommand(0x11);  // Data entry mode: Y-decrement, X-increment
    sendData(0x01);

    sendCommand(0x44);  // RAM X address range: 0x00..0x18 (25 bytes = 200 bits)
    sendData(0x00);
    sendData(0x18);

    sendCommand(0x45);  // RAM Y address range: 199..0
    sendData(0xC7);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);

    sendCommand(0x3C);  // Border waveform
    sendData(0x05);

    sendCommand(0x18);  // Use internal temperature sensor
    sendData(0x80);

    setRamAddress();
    waitBusy();
}

void EPD::display(const uint8_t* black_buf, const uint8_t* red_buf) {
    setRamAddress();

    sendCommand(0x24);  // Write Black/White RAM (0=black, 1=white)
    for (int i = 0; i < EPD_BUF_SIZE; i++) {
        sendData(black_buf[i]);
    }

    setRamAddress();

    sendCommand(0x26);  // Write Red RAM (SSD1681: 1=red/chromatic, 0=not-red)
    for (int i = 0; i < EPD_BUF_SIZE; i++) {
        sendData(red_buf[i]);
    }

    sendCommand(0x22);  // Display update control sequence
    sendData(0xF7);
    sendCommand(0x20);  // Activate display update
    waitBusy();
}

void EPD::clear() {
    static uint8_t black_buf[EPD_BUF_SIZE];
    static uint8_t red_buf[EPD_BUF_SIZE];
    memset(black_buf, 0xFF, EPD_BUF_SIZE);
    memset(red_buf,   0xFF, EPD_BUF_SIZE);
    display(black_buf, red_buf);
}

void EPD::sleep() {
    sendCommand(0x10);  // Deep sleep mode
    sendData(0x01);
    delay(100);
    SPI.endTransaction();
}

// --- private ---

void EPD::reset() {
    digitalWrite(EPD_RST, HIGH); delay(200);
    digitalWrite(EPD_RST, LOW);  delay(2);
    digitalWrite(EPD_RST, HIGH); delay(200);
}

void EPD::sendCommand(uint8_t cmd) {
    digitalWrite(EPD_DC, LOW);
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(cmd);
    digitalWrite(EPD_CS, HIGH);
}

void EPD::sendData(uint8_t data) {
    digitalWrite(EPD_DC, HIGH);
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(data);
    digitalWrite(EPD_CS, HIGH);
}

void EPD::waitBusy() {
    while (digitalRead(EPD_BUSY) == HIGH) {
        delay(10);
    }
}

void EPD::setRamAddress() {
    sendCommand(0x4E);  // RAM X address counter
    sendData(0x00);
    sendCommand(0x4F);  // RAM Y address counter: start at row 199 (display bottom)
    sendData(0xC7);
    sendData(0x00);
}
