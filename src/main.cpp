#include <Arduino.h>
#include <BleKeyboard.h>
#include <BleMouse.h>
#include <EspUsbHost.h>
#include <Adafruit_NeoPixel.h>

// Pins for M5Stamp S3
#define PIN_LED 21
#define PIN_BUTTON 0

// BLE Device Names
BleKeyboard bleKeyboard("M5Stamp S3 Keyboard", "M5Stack", 100);
BleMouse bleMouse("M5Stamp S3 Mouse", "M5Stack", 100);
Adafruit_NeoPixel pixels(1, PIN_LED, NEO_GRB + NEO_KHZ800);

class MyUsbHost : public EspUsbHost {
public:
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) override {
    if (bleKeyboard.isConnected()) {
      if (ascii != 0) {
        bleKeyboard.write(ascii);
      } else {
        // Handle special keys if needed (EspUsbHost handles most via ascii)
        // For raw keycode mapping, we could use bleKeyboard.press(keycode)
      }
    }
  }

  void onMouseAction(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) override {
    if (bleMouse.isConnected()) {
      bleMouse.move(x, y, wheel);
      if (buttons & 0x01) bleMouse.press(MOUSE_LEFT); else bleMouse.release(MOUSE_LEFT);
      if (buttons & 0x02) bleMouse.press(MOUSE_RIGHT); else bleMouse.release(MOUSE_RIGHT);
      if (buttons & 0x04) bleMouse.press(MOUSE_MIDDLE); else bleMouse.release(MOUSE_MIDDLE);
    }
  }

  void onDeviceConnect(uint8_t address, bool isHub) override {
    Serial.printf("Device connected: address=%d, isHub=%s\n", address, isHub ? "true" : "false");
  }

  void onDeviceDisconnect(uint8_t address) override {
    Serial.printf("Device disconnected: address=%d\n", address);
  }
};

MyUsbHost usbHost;

void updateLED() {
  static unsigned long lastMillis = 0;
  static bool toggle = false;
  
  if (bleKeyboard.isConnected()) {
    pixels.setPixelColor(0, pixels.Color(0, 50, 0)); // Green: Connected
  } else {
    if (millis() - lastMillis > 500) {
      lastMillis = millis();
      toggle = !toggle;
      if (toggle) {
        pixels.setPixelColor(0, pixels.Color(0, 0, 50)); // Blue: Pairing
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
    }
  }
  pixels.show();
}

void setup() {
  Serial.begin(115200);
  
  pixels.begin();
  pixels.setBrightness(20);
  
  Serial.println("Starting BLE Keyboard/Mouse...");
  bleKeyboard.begin();
  bleMouse.begin();
  
  Serial.println("Starting USB Host...");
  usbHost.begin();
  
  // M5Stamp S3 internal USB uses GPIO 19(D-) and 20(D+)
  // EspUsbHost uses these by default on S3
}

void loop() {
  usbHost.task();
  updateLED();
}