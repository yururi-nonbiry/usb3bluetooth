#include <Arduino.h>
#include <EspUsbHost.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// Pins for ESP32-S3 DevKit
#define PIN_LED 48
#define PIN_BUTTON 0

// Global state
Preferences preferences;
int currentSlot = 0;
uint16_t slotConnHandles[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
NimBLEAddress slotAddresses[4];

Adafruit_NeoPixel pixels(1, PIN_LED, NEO_GRB + NEO_KHZ800);
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* inputKeyboard = nullptr;
NimBLECharacteristic* inputMouse = nullptr;

// Release reports
const uint8_t keyboardRelease[8] = {0};
const uint8_t mouseRelease[5] = {0};

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

void sendReleaseAll(uint16_t handle) {
  if (handle != 0xFFFF) {
    if (inputKeyboard) inputKeyboard->notify(keyboardRelease, sizeof(keyboardRelease), handle);
    if (inputMouse) inputMouse->notify(mouseRelease, sizeof(mouseRelease), handle);
  }
}

void switchToSlot(int slot) {
  if (slot < 0 || slot >= 4) return;
  if (currentSlot == slot) return;
  
  // Send release to current slot before switching to prevent stuck keys
  sendReleaseAll(slotConnHandles[currentSlot]);
  
  currentSlot = slot;
  preferences.putInt("slot", currentSlot);
  Serial.printf("Switching to slot %d\n", currentSlot + 1);
  
  // Feedback blink
  pixels.setPixelColor(0, pixels.Color(150, 150, 150));
  pixels.show();
  delay(50);
}

// BLE Callbacks
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    NimBLEAddress addr = connInfo.getIdAddress();
    uint16_t handle = connInfo.getConnHandle();
    Serial.printf("Connected: %s (Handle: %d)\n", addr.toString().c_str(), handle);

    int targetSlot = -1;
    // 1. Check if this address is already assigned to a slot (Byte-wise comparison is safer for Identity Addresses)
    for (int i = 0; i < 4; i++) {
      if (slotAddresses[i] == addr) {
        targetSlot = i;
        break;
      }
    }

    // 2. If new address, assign to first available slot (no address stored)
    if (targetSlot == -1) {
      for (int i = 0; i < 4; i++) {
        if (slotAddresses[i].isNull()) {
          targetSlot = i;
          slotAddresses[i] = addr;
          char key[8]; sprintf(key, "addr%d", i);
          uint8_t saveBuf[7];
          saveBuf[0] = addr.getType();
          memcpy(&saveBuf[1], addr.getVal(), 6);
          preferences.putBytes(key, saveBuf, 7);
          Serial.printf("Assigned new device to slot %d\n", i + 1);
          break;
        }
      }
    }

    // 3. If all slots are full but this is a new connection, overwrite first disconnected slot
    if (targetSlot == -1) {
       for (int i = 0; i < 4; i++) {
         if (slotConnHandles[i] == 0xFFFF) {
           targetSlot = i;
           slotAddresses[i] = addr;
           char key[8]; sprintf(key, "addr%d", i);
           uint8_t saveBuf[7];
           saveBuf[0] = addr.getType();
           memcpy(&saveBuf[1], addr.getVal(), 6);
           preferences.putBytes(key, saveBuf, 7);
           break;
         }
       }
    }

    if (targetSlot != -1) {
      slotConnHandles[targetSlot] = handle;
    }
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    uint16_t handle = connInfo.getConnHandle();
    for (int i = 0; i < 4; i++) {
      if (slotConnHandles[i] == handle) {
        slotConnHandles[i] = 0xFFFF;
        Serial.printf("Disconnected from slot %d, reason: %d\n", i + 1, reason);
      }
    }
    NimBLEDevice::getAdvertising()->start();
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (connInfo.isBonded()) {
      uint16_t handle = connInfo.getConnHandle();
      NimBLEAddress addr = connInfo.getIdAddress();
      for (int i = 0; i < 4; i++) {
        if (slotConnHandles[i] == handle) {
          if (!slotAddresses[i].equals(addr)) {
            Serial.printf("Updating slot %d address to IA: %s\n", i + 1, addr.toString().c_str());
            slotAddresses[i] = addr;
            char key[8]; sprintf(key, "addr%d", i);
            uint8_t saveBuf[7];
            saveBuf[0] = addr.getType();
            memcpy(&saveBuf[1], addr.getVal(), 6);
            preferences.putBytes(key, saveBuf, 7);
          }
          break;
        }
      }
    }
  }
};

class MyUsbHost : public EspUsbHost {
public:
  // Using onKeyboard for transparent bridge (supports long press and multiple keys)
  void onKeyboard(hid_keyboard_report_t report, hid_keyboard_report_t last_report) override {
    // Intercept switching keys within the report
    for (int i = 0; i < 6; i++) {
      uint8_t key = report.keycode[i];
      if (key >= 0x68 && key <= 0x6B) { // F13 - F16
        switchToSlot(key - 0x68);
        return; 
      }
    }
    
    // Alt + F1-F4
    bool isAlt = (report.modifier & 0x04) || (report.modifier & 0x40);
    if (isAlt) {
      for (int i = 0; i < 6; i++) {
        uint8_t key = report.keycode[i];
        if (key >= 0x3A && key <= 0x3D) { // F1 - F4
          switchToSlot(key - 0x3A);
          return;
        }
      }
    }

    uint16_t handle = slotConnHandles[currentSlot];
    if (handle != 0xFFFF && inputKeyboard) {
      inputKeyboard->notify((uint8_t*)&report, sizeof(hid_keyboard_report_t), handle);
    }
  }

  // Using onMouse to support horizontal scroll (Pan)
  void onMouse(hid_mouse_report_t report, uint8_t last_buttons) override {
    Serial.println("Mouse moved");
    uint16_t handle = slotConnHandles[currentSlot];
    if (handle != 0xFFFF && inputMouse) {
      // 5 bytes: Buttons, X, Y, Wheel, Pan
      uint8_t buffer[5] = {report.buttons, (uint8_t)report.x, (uint8_t)report.y, (uint8_t)report.wheel, 0};
      inputMouse->notify(buffer, sizeof(buffer), handle);
    }
  }

  // Handle raw HID data if onMouse/onKeyboard didn't catch it (e.g. non-boot protocol)
  void onReceive(const usb_transfer_t *transfer) override {
    uint8_t ep_num = (transfer->bEndpointAddress & 0x0F);
    auto& ep_data = endpoint_data_list[ep_num];
    
    // Only process if it's a HID interface and not handled by boot protocol callbacks
    if (ep_data.bInterfaceClass == 0x03 && ep_data.bInterfaceProtocol == 0) {
      uint16_t handle = slotConnHandles[currentSlot];
      if (handle == 0xFFFF || !inputMouse) return;

      // Keyball44 trackball often sends data that looks like a mouse report
      // If length is 4-8 bytes, we attempt to pass it through
      if (transfer->actual_num_bytes >= 3 && transfer->actual_num_bytes <= 8) {
        Serial.printf("Mouse moved (Raw, Len: %d)\n", transfer->actual_num_bytes);
        // We use a 5-byte buffer to match our BLE descriptor
        uint8_t buffer[5] = {0};
        // Simple heuristic: Buttons are usually byte 0 (or byte 1 if ID present)
        // This is a "best effort" bridge for non-boot mice
        memcpy(buffer, transfer->data_buffer, (transfer->actual_num_bytes > 5) ? 5 : transfer->actual_num_bytes);
        inputMouse->notify(buffer, sizeof(buffer), handle);
      }
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
      Serial.println("Long press: Clearing all bonds and slot mappings...");
      NimBLEDevice::deleteAllBonds();
      for (int i = 0; i < 4; i++) {
        char key[8]; sprintf(key, "addr%d", i);
        preferences.remove(key);
      }
      ESP.restart();
    } else if (duration > 50) {
      switchToSlot((currentSlot + 1) % 4);
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
  // Load stored addresses for slots (7 bytes: type + address)
  for (int i = 0; i < 4; i++) {
    char key[8]; sprintf(key, "addr%d", i);
    uint8_t buf[7];
    size_t len = preferences.getBytes(key, buf, 7);
    if (len == 7) {
      slotAddresses[i] = NimBLEAddress(&buf[1], buf[0]);
    } else if (len == 6) {
      // Compatibility with old 6-byte format
      slotAddresses[i] = NimBLEAddress(buf, 0); 
    } else {
      slotAddresses[i] = NimBLEAddress("\0\0\0\0\0\0", 0);
    }
  }

  NimBLEDevice::init("ESP32-S3 Keyboard");
  // Set security for better compatibility with Windows/iOS
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  hid = new NimBLEHIDDevice(pServer);
  inputKeyboard = hid->getInputReport(1); // Report ID 1: Keyboard
  inputMouse = hid->getInputReport(2);    // Report ID 2: Mouse

  hid->setManufacturer("Espressif");
  // Set PnP ID to Apple (0x05ac) for better compatibility with some hosts
  hid->setPnp(0x02, 0x05ac, 0x820a, 0x0210);
  hid->setHidInfo(0x00, 0x01);

  // Updated Report Map for Keyboard + Mouse (with Pan)
  const uint8_t reportMap[] = {
    // Keyboard
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0,
    // Mouse (Buttons, X, Y, Wheel, Pan)
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0x05, 0x0c, 0x0a, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95,
    0x01, 0x81, 0x06, 0xc0, 0xc0
  };
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));

  pServer->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setAppearance(0x03C1); // HID Keyboard (Combo)
  pAdvertising->addServiceUUID(hid->getHidService()->getUUID());
  pAdvertising->enableScanResponse(false);
  pAdvertising->start();

  Serial.println("BLE Multi-HID Bridge Ready");
  usbHost.begin();
}

void loop() {
  usbHost.task();
  handleButton();
  updateLED();
}