#include <Arduino.h>
#include <BleKeyboard.h>
#include <BleMouse.h>
#include <EspUsbHost.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <NimBLEDevice.h>

// Pins for M5Stamp S3
#define PIN_LED 21
#define PIN_BUTTON 0

// Global state
Preferences preferences;
int currentSlot = 0;
bool isConnected = false;

// BLE Device Objects (Pointers to allow dynamic naming)
BleKeyboard* bleKeyboard = nullptr;
BleMouse* bleMouse = nullptr;
Adafruit_NeoPixel pixels(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// Slot Colors: 1:Red, 2:Blue, 3:Yellow, 4:Green
uint32_t getSlotColor(int slot) {
  switch (slot) {
    case 0: return pixels.Color(100, 0, 0);   // Red
    case 1: return pixels.Color(0, 0, 100);   // Blue
    case 2: return pixels.Color(100, 100, 0); // Yellow
    case 3: return pixels.Color(0, 100, 0);   // Green
    default: return pixels.Color(50, 50, 50);
  }
}

class MyUsbHost : public EspUsbHost {
public:
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) override {
    if (bleKeyboard && bleKeyboard->isConnected()) {
      if (ascii != 0) {
        bleKeyboard->write(ascii);
      }
    }
  }

  void onMouseAction(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) override {
    if (bleMouse && bleMouse->isConnected()) {
      bleMouse->move(x, y, wheel);
      if (buttons & 0x01) bleMouse->press(MOUSE_LEFT); else bleMouse->release(MOUSE_LEFT);
      if (buttons & 0x02) bleMouse->press(MOUSE_RIGHT); else bleMouse->release(MOUSE_RIGHT);
      if (buttons & 0x04) bleMouse->press(MOUSE_MIDDLE); else bleMouse->release(MOUSE_MIDDLE);
    }
  }
};

MyUsbHost usbHost;

void updateLED() {
  static unsigned long lastMillis = 0;
  static bool toggle = false;
  
  bool connected = (bleKeyboard && bleKeyboard->isConnected());
  uint32_t color = getSlotColor(currentSlot);

  if (connected) {
    pixels.setPixelColor(0, color); // Solid color when connected
  } else {
    // Blinking when pairing/not connected
    if (millis() - lastMillis > 500) {
      lastMillis = millis();
      toggle = !toggle;
      if (toggle) {
        pixels.setPixelColor(0, color);
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
    }
  }
  pixels.show();
}

void handleButton() {
  static unsigned long pressStart = 0;
  static bool lastState = HIGH;
  bool currentState = digitalRead(PIN_BUTTON);

  if (lastState == HIGH && currentState == LOW) {
    pressStart = millis();
  } else if (lastState == LOW && currentState == HIGH) {
    unsigned long duration = millis() - pressStart;
    if (duration > 2000) {
      // Long press: Pairing mode (Clear bonds for current slot)
      Serial.println("Long press: Clearing bonds and entering pairing mode...");
      // Rapid blink to acknowledge
      for(int i=0; i<10; i++) {
        pixels.setPixelColor(0, getSlotColor(currentSlot)); pixels.show(); delay(50);
        pixels.setPixelColor(0, 0); pixels.show(); delay(50);
      }
      NimBLEDevice::deleteAllBonds(); // Note: This clears ALL bonds. 
      // For slot-specific clearing, it would be more complex, 
      // but usually users want a fresh start.
      ESP.restart();
    } else if (duration > 50) {
      // Short press: Switch device slot
      currentSlot = (currentSlot + 1) % 4;
      preferences.putInt("slot", currentSlot);
      Serial.printf("Short press: Switching to slot %d\n", currentSlot + 1);
      
      // Feedback blink
      pixels.setPixelColor(0, getSlotColor(currentSlot)); pixels.show();
      delay(200);
      ESP.restart();
    }
  }
  lastState = currentState;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  pixels.begin();
  pixels.setBrightness(40);
  
  // Load slot from preferences
  preferences.begin("ble-adapter", false);
  currentSlot = preferences.getInt("slot", 0);
  
  // Set a unique MAC address for each slot so they appear as separate devices
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  mac[5] = (mac[5] & 0xFC) | currentSlot; // Modify last 2 bits
  esp_base_mac_addr_set(mac);

  String name = "M5 Keyboard Slot " + String(currentSlot + 1);
  Serial.println("Starting BLE " + name);
  
  bleKeyboard = new BleKeyboard(name.c_str(), "M5Stack", 100);
  bleMouse = new BleMouse(name.c_str(), "M5Stack", 100);
  
  bleKeyboard->begin();
  bleMouse->begin();
  
  Serial.println("Starting USB Host...");
  usbHost.begin();
}

void loop() {
  usbHost.task();
  handleButton();
  updateLED();
}