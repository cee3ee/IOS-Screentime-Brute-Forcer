#include <KeyboardBLE.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDR   0x3C

// RP2040 I2C0 mapping
#define PIN_SDA 0
#define PIN_SCL 1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int LED = LED_BUILTIN;

void initDisplay() {
    Wire.setSDA(PIN_SDA);
    Wire.setSCL(PIN_SCL);
    Wire.begin();

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
        // If OLED not found, stop execution (blink LED rapidly to signal error)
        while (true) {
            digitalWrite(LED, HIGH);
            delay(100);
            digitalWrite(LED, LOW);
            delay(100);
        }
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.display();
}

void setup() {
    pinMode(LED, OUTPUT);
    Serial.begin(115200);
    delay(1000);

    // Initialize OLED display
    initDisplay();

    // Show startup message
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("PicoKey");
    display.println("Starting...");
    display.display();
    delay(2000);

    KeyboardBLE.begin("PicoKey");

    // Existing blink sequence (15 seconds)
    for (int i = 0; i < 30; i++) {
        digitalWrite(LED, HIGH);
        delay(500);
        digitalWrite(LED, LOW);
        delay(500);
    }
    digitalWrite(LED, HIGH);

    // Main typing loop
    for (int i = 0; i < 10000; i++) {
        char combo[5];
        snprintf(combo, sizeof(combo), "%04d", i);

        // Calculate progress percentage
        int progress = (i * 100) / 10000;   // integer 0-100

        // --- Draw on OLED ---
        display.clearDisplay();

        // Title
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("PicoKey");

        // Status
        display.setCursor(0, 8);
        display.println("Typing:");

        // Current combo (large font)
        display.setTextSize(2);
        display.setCursor(0, 16);
        display.println(combo);

        // Progress percentage (small font above bar)
        display.setTextSize(1);
        display.setCursor(0, 34);
        display.print(progress);
        display.print("%");

        // Progress bar outline
        display.drawRect(0, 42, 128, 12, SSD1306_WHITE);

        // Progress bar fill (inner width 126, offset by 1 pixel)
        int barWidth = (i * 126) / 10000;
        if (barWidth > 0) {
            display.fillRect(1, 43, barWidth, 10, SSD1306_WHITE);
        }

        display.display();
        // --- End OLED drawing ---

        // Send combo via BLE keyboard
        for (int j = 0; j < 4; j++) {
            KeyboardBLE.write(combo[j]);
            delay(150);
        }

        Serial.println(combo);
        delay(500);
    }

    digitalWrite(LED, LOW);

    // Show done message
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("PicoKey");
    display.println("Finished!");
    display.display();
}

void loop() {
    delay(1000);
}