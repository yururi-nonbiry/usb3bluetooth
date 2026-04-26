#include <Arduino.h>
#include <EspUsbHost.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// Pins for M5Stamp S3
#define PIN_LED 21
#define PIN_BUTTON 0

// Global state
Preferences preferences;
int currentSlot = 0;
uint16_t slotConnHandles[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

Adafruit_NeoPixel pixels(1, PIN_LED, NEO_GRB + NEO_KHZ800);
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* inputKeyboard = nullptr;
NimBLECharacteristic* inputMouse = nullptr;

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

// BLE Callbacks
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.printf("Connected: %s\n", connInfo.getAddress().toString().c_str());
    
    // Assign to an empty slot, prioritizing current slot if it's empty
    if (slotConnHandles[currentSlot] == 0xFFFF) {
        slotConnHandles[currentSlot] = connInfo.getConnHandle();
    } else {
        for (int i = 0; i < 4; i++) {
            if (slotConnHandles[i] == 0xFFFF) {
                slotConnHandles[i] = connInfo.getConnHandle();
                break;
            }
        }
    }
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("Disconnected, reason: %d\n", reason);
    uint16_t handle = connInfo.getConnHandle();
    for (int i = 0; i < 4; i++) {
        if (slotConnHandles[i] == handle) {
            slotConnHandles[i] = 0xFFFF;
        }
    }
    // Restart advertising to allow others to connect
    NimBLEDevice::getAdvertising()->start();
  }
};

class MyUsbHost : public EspUsbHost {
public:
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) override {
    uint16_t handle = slotConnHandles[currentSlot];
    if (handle != 0xFFFF && inputKeyboard) {
        uint8_t report[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
        inputKeyboard->notify(report, sizeof(report), handle);
        // Release key immediately for simplicity in this USB bridge
        uint8_t release[8] = {0};
        inputKeyboard->notify(release, sizeof(release), handle);
    }
  }

  void onMouseAction(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) override {
    uint16_t handle = slotConnHandles[currentSlot];
    if (handle != 0xFFFF && inputMouse) {
        uint8_t report[4] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel};
        inputMouse->notify(report, sizeof(report), handle);
    }
  }
};

MyUsbHost usbHost;

void updateLED() {
  static unsigned long lastMillis = 0;
  static bool toggle = false;
  
  uint16_t handle = slotConnHandles[currentSlot];
  bool connected = (handle != 0xFFFF);
  uint32_t color = getSlotColor(currentSlot);

  if (connected) {
    pixels.setPixelColor(0, color); 
  } else {
    if (millis() - lastMillis > 500) {
      lastMillis = millis();
      toggle = !toggle;
      pixels.setPixelColor(0, toggle ? color : 0);
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
      Serial.println("Long press: Clearing all bonds...");
      for(int i=0; i<10; i++) {
        pixels.setPixelColor(0, pixels.Color(100, 100, 100)); pixels.show(); delay(50);
        pixels.setPixelColor(0, 0); pixels.show(); delay(50);
      }
      NimBLEDevice::deleteAllBonds();
      ESP.restart();
    } else if (duration > 50) {
      currentSlot = (currentSlot + 1) % 4;
      preferences.putInt("slot", currentSlot);
      Serial.printf("Slot switched to %d\n", currentSlot + 1);
      // Small blink for feedback
      pixels.setPixelColor(0, pixels.Color(50, 50, 50)); pixels.show();
      delay(100);
    }
  }
  lastState = currentState;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  pixels.begin();
  pixels.setBrightness(40);
  
  preferences.begin("ble-adapter", false);
  currentSlot = preferences.getInt("slot", 0);

  NimBLEDevice::init("M5 Multi Keyboard");
  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  hid = new NimBLEHIDDevice(pServer);
  inputKeyboard = hid->inputReport(1); // Report ID 1: Keyboard
  inputMouse = hid->inputReport(2);    // Report ID 2: Mouse

  hid->manufacturer()->setValue("M5Stack");
  hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->hidInfo(0x00, 0x01);

  // USB HID Report Map for Keyboard + Mouse
  const uint8_t reportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0,
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xc0, 0xc0
  };
  hid->reportMap((uint8_t*)reportMap, sizeof(reportMap));
  hid->startServices();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->setScanResponse(false);
  pAdvertising->start();

  Serial.println("BLE Multi-HID Ready");
  usbHost.begin();
}

void loop() {
  usbHost.task();
  handleButton();
  updateLED();
}