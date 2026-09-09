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

// Input pins (active LOW with internal pull-ups)
#define BTN_NAV 2   // GP2 – navigate (left)
#define BTN_ACT 3   // GP3 – action / right

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int LED = LED_BUILTIN;

// ----- Adjustable parameters (defaults) -----
unsigned int charDelayMs = 150;      // delay after each character
unsigned int codeDelayMs = 500;      // delay after sending all 4 digits
unsigned int startIndex   = 0;       // custom start index for sequential mode

// ----- State variables -----
enum OperationMode { SEQUENTIAL, RANDOM };
OperationMode mode = SEQUENTIAL;

bool running = false;                // is the typing currently active?
bool inMenu = false;                 // are we in the settings menu?

// Menu sub‑state
enum MenuState { MENU_MAIN, MENU_CHAR_DELAY, MENU_CODE_DELAY, MENU_START_INDEX, MENU_MODE, MENU_RESET };
MenuState menuState = MENU_MAIN;
int menuSelectedItem = 0;            // index of highlighted item in main menu
int tempAdjustValue = 0;             // temporary value while adjusting in a submenu

// Current sending state
int currentIndex = 0;                // sequential: next code index to send
char currentCombo[5] = "0000";
int currentCharPos = 0;              // which character (0-3) to send next
unsigned long lastCharTime = 0;      // timestamp for character delay
unsigned long lastCodeTime = 0;      // timestamp for code delay
bool sendingCode = false;            // true while we are in the middle of a code
unsigned long attempts = 0;          // total attempts (for random mode)

// Button debouncing and long‑press detection
bool lastNavState = HIGH;
bool lastActState = HIGH;
unsigned long actPressStart = 0;     // when the action button was pressed
bool actLongPressTriggered = false;  // prevents repeated long‑press triggers

// ----- Function prototypes -----
void initDisplay();
void drawMainScreen();
void drawMenuScreen();
void handleButtons();
void startTyping();
void stopTyping();
void enterMenu();
void exitMenu();
void applySettings();
void resetToDefaults();
void generateNextCode();

// =====================================================================
void setup() {
    pinMode(LED, OUTPUT);
    pinMode(BTN_NAV, INPUT_PULLUP);
    pinMode(BTN_ACT, INPUT_PULLUP);

    Serial.begin(115200);
    delay(1000);

    initDisplay();

    // Startup message
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("PicoKey");
    display.println("Starting...");
    display.display();
    delay(2000);

    KeyboardBLE.begin("PicoKey");

    // Initial LED blink sequence (15 seconds)
    for (int i = 0; i < 30; i++) {
        digitalWrite(LED, HIGH);
        delay(500);
        digitalWrite(LED, LOW);
        delay(500);
    }
    digitalWrite(LED, HIGH);

    // Seed random generator for random mode
    randomSeed(analogRead(A0) ^ millis());

    currentIndex = startIndex;
    generateNextCode();   // prepare first combo
}

// =====================================================================
void loop() {
    handleButtons();   // non‑blocking button handling

    // If running and not in menu, handle code sending
    if (running && !inMenu) {
        unsigned long now = millis();

        if (!sendingCode) {
            // Start sending a new code after the code delay has elapsed
            if (now - lastCodeTime >= codeDelayMs) {
                sendingCode = true;
                currentCharPos = 0;
                lastCharTime = now;
                // The combo to send is already in currentCombo
            }
        } else {
            // Send characters one by one with charDelayMs between them
            if (currentCharPos < 4) {
                if (now - lastCharTime >= charDelayMs) {
                    KeyboardBLE.write(currentCombo[currentCharPos]);
                    Serial.print(currentCombo[currentCharPos]);
                    currentCharPos++;
                    lastCharTime = now;
                }
            } else {
                // Finished sending all 4 digits
                sendingCode = false;
                lastCodeTime = now;
                attempts++;

                // Prepare next code
                generateNextCode();
            }
        }
    }

    // Update display (only redraw when necessary, but simple to redraw each loop)
    if (inMenu) {
        drawMenuScreen();
    } else {
        drawMainScreen();
    }
}

// =====================================================================
void initDisplay() {
    Wire.setSDA(PIN_SDA);
    Wire.setSCL(PIN_SCL);
    Wire.begin();

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
        // OLED error: blink LED fast forever
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

// =====================================================================
void drawMainScreen() {
    display.clearDisplay();

    // Title
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("PicoKey");

    // Status line
    display.setCursor(0, 8);
    if (running) {
        display.print("Typing:");
    } else {
        display.print("Stopped");
    }

    // Current combo (large font)
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.println(currentCombo);

    // Progress / status line
    display.setTextSize(1);
    display.setCursor(0, 34);

    if (mode == SEQUENTIAL) {
        // Percentage and ETA
        int progress = (currentIndex * 100) / 10000;
        unsigned long totalMs = (unsigned long)(charDelayMs * 4 + codeDelayMs) * (10000 - currentIndex);
        unsigned int etaSec = totalMs / 1000;
        unsigned int etaMin = etaSec / 60;
        etaSec %= 60;

        display.print(progress);
        display.print("%  ETA ");
        display.print(etaMin);
        display.print(":");
        if (etaSec < 10) display.print("0");
        display.print(etaSec);
    } else {
        // Random mode: show attempts
        display.print("Random  ");
        display.print(attempts);
        display.print(" tries");
    }

    // Progress bar (only for sequential)
    if (mode == SEQUENTIAL) {
        display.drawRect(0, 42, 128, 12, SSD1306_WHITE);
        int barWidth = (currentIndex * 126) / 10000;
        if (barWidth > 0) {
            display.fillRect(1, 43, barWidth, 10, SSD1306_WHITE);
        }
    } else {
        // For random, draw outline but no fill
        display.drawRect(0, 42, 128, 12, SSD1306_WHITE);
    }

    display.display();
}

// =====================================================================
void drawMenuScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Settings Menu");

    switch (menuState) {
        case MENU_MAIN: {
            // List menu items
            const char* items[] = {
                "Char Delay",
                "Code Delay",
                "Start Index",
                "Mode",
                "Reset Defaults",
                "Exit"
            };
            int numItems = 6;

            for (int i = 0; i < numItems; i++) {
                display.setCursor(0, 16 + i * 8);
                if (i == menuSelectedItem) {
                    display.print("> ");
                } else {
                    display.print("  ");
                }
                display.println(items[i]);
            }
            break;
        }

        case MENU_CHAR_DELAY:
            display.setCursor(0, 16);
            display.print("Char Delay:");
            display.setCursor(0, 24);
            display.print(tempAdjustValue);
            display.print(" ms");
            display.setCursor(0, 40);
            display.print("GP2: +10 (loop)");
            display.setCursor(0, 48);
            display.print("GP3: OK");
            break;

        case MENU_CODE_DELAY:
            display.setCursor(0, 16);
            display.print("Code Delay:");
            display.setCursor(0, 24);
            display.print(tempAdjustValue);
            display.print(" ms");
            display.setCursor(0, 40);
            display.print("GP2: +10 (loop)");
            display.setCursor(0, 48);
            display.print("GP3: OK");
            break;

        case MENU_START_INDEX:
            display.setCursor(0, 16);
            display.print("Start Index:");
            display.setCursor(0, 24);
            display.print(tempAdjustValue);
            display.setCursor(0, 40);
            display.print("GP2: +100 (loop)");
            display.setCursor(0, 48);
            display.print("GP3: OK");
            break;

        case MENU_MODE:
            display.setCursor(0, 16);
            display.print("Mode:");
            display.setCursor(0, 24);
            display.print(tempAdjustValue == SEQUENTIAL ? "Sequential" : "Random");
            display.setCursor(0, 40);
            display.print("GP2: toggle");
            display.setCursor(0, 48);
            display.print("GP3: OK");
            break;

        case MENU_RESET:
            display.setCursor(0, 16);
            display.println("Reset all settings");
            display.println("to defaults?");
            display.setCursor(0, 40);
            display.print("GP2: Cancel");
            display.setCursor(0, 48);
            display.print("GP3: Confirm");
            break;
    }
    display.display();
}

// =====================================================================
void handleButtons() {
    bool nav = digitalRead(BTN_NAV);
    bool act = digitalRead(BTN_ACT);
    unsigned long now = millis();
    const unsigned long debounceTime = 50;

    // Action button (GP3)
    if (act == LOW && lastActState == HIGH) {
        actPressStart = now;
        actLongPressTriggered = false;
    }

    // Check for long press on action button (only when not in menu)
    if (!inMenu && act == LOW && !actLongPressTriggered && (now - actPressStart >= 5000)) {
        actLongPressTriggered = true;
        enterMenu();
        // Do not process further this iteration
        lastActState = act;
        lastNavState = nav;
        return;
    }

    // Short press on action button on release
    if (act == HIGH && lastActState == LOW) {
        // Debounce check
        if (now - actPressStart > debounceTime && now - actPressStart < 5000) {
            if (!inMenu) {
                // Toggle running
                if (running) stopTyping();
                else startTyping();
            } else {
                // In menu: action depends on current sub‑state
                switch (menuState) {
                    case MENU_MAIN:
                        // Select the highlighted item
                        if (menuSelectedItem == 5) {
                            // Exit menu
                            exitMenu();
                        } else {
                            // Enter corresponding sub‑menu
                            switch (menuSelectedItem) {
                                case 0:
                                    menuState = MENU_CHAR_DELAY;
                                    tempAdjustValue = charDelayMs;
                                    break;
                                case 1:
                                    menuState = MENU_CODE_DELAY;
                                    tempAdjustValue = codeDelayMs;
                                    break;
                                case 2:
                                    menuState = MENU_START_INDEX;
                                    tempAdjustValue = startIndex;
                                    break;
                                case 3:
                                    menuState = MENU_MODE;
                                    tempAdjustValue = (int)mode;
                                    break;
                                case 4:
                                    menuState = MENU_RESET;
                                    break;
                            }
                        }
                        break;

                    case MENU_CHAR_DELAY:
                        // Confirm new value
                        charDelayMs = tempAdjustValue;
                        menuState = MENU_MAIN;
                        break;

                    case MENU_CODE_DELAY:
                        codeDelayMs = tempAdjustValue;
                        menuState = MENU_MAIN;
                        break;

                    case MENU_START_INDEX:
                        startIndex = tempAdjustValue;
                        menuState = MENU_MAIN;
                        break;

                    case MENU_MODE:
                        mode = (OperationMode)tempAdjustValue;
                        menuState = MENU_MAIN;
                        break;

                    case MENU_RESET:
                        // Confirm reset
                        resetToDefaults();
                        menuState = MENU_MAIN;
                        break;
                }
            }
        }
    }

    // Navigation button (GP2)
    if (nav == LOW && lastNavState == HIGH) {
        if (inMenu) {
            switch (menuState) {
                case MENU_MAIN:
                    // Cycle through menu items
                    menuSelectedItem++;
                    if (menuSelectedItem > 5) menuSelectedItem = 0;
                    break;

                case MENU_CHAR_DELAY:
                case MENU_CODE_DELAY:
                    // Increase by 10 ms, loop back to minimum (10) after 1000
                    tempAdjustValue += 10;
                    if (tempAdjustValue > 1000) tempAdjustValue = 10;
                    break;

                case MENU_START_INDEX:
                    // Increase by 100, loop back to 0 after 9900
                    tempAdjustValue += 100;
                    if (tempAdjustValue > 9900) tempAdjustValue = 0;
                    break;

                case MENU_MODE:
                    // Toggle between sequential and random
                    tempAdjustValue = (tempAdjustValue == SEQUENTIAL) ? RANDOM : SEQUENTIAL;
                    break;

                case MENU_RESET:
                    // Cancel and go back to main menu
                    menuState = MENU_MAIN;
                    break;
            }
        }
    }

    lastNavState = nav;
    lastActState = act;
}

// =====================================================================
void startTyping() {
    running = true;
    sendingCode = false;
    lastCodeTime = millis();   // start after codeDelayMs from now
    // Ensure current combo is set correctly for sequential mode
    if (mode == SEQUENTIAL) {
        currentIndex = startIndex;
        generateNextCode();
    } else {
        generateNextCode();
    }
    attempts = 0;
    digitalWrite(LED, HIGH);
}

void stopTyping() {
    running = false;
    sendingCode = false;
    digitalWrite(LED, LOW);
}

void enterMenu() {
    stopTyping();   // pause typing when entering menu
    inMenu = true;
    menuState = MENU_MAIN;
    menuSelectedItem = 0;
}

void exitMenu() {
    inMenu = false;
    // Do not auto‑start; user must press action button again
}

void applySettings() {
    // Called after confirming any value; no extra action needed here
    // because the variable assignments are done in handleButtons()
}

void resetToDefaults() {
    charDelayMs = 150;
    codeDelayMs = 500;
    startIndex = 0;
    mode = SEQUENTIAL;
    currentIndex = startIndex;
    generateNextCode();
}

// =====================================================================
void generateNextCode() {
    if (mode == SEQUENTIAL) {
        snprintf(currentCombo, sizeof(currentCombo), "%04d", currentIndex);
        currentIndex = (currentIndex + 1) % 10000;   // wrap around after 9999
    } else {
        int rnd = random(0, 10000);
        snprintf(currentCombo, sizeof(currentCombo), "%04d", rnd);
    }
}
