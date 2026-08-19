#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <FS.h>
#include <SD.h>
#include <ArduinoJson.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ==========================================
// PINS & HARDWARE CONFIGURATION (CYD)
// ==========================================
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define LCD_BACKLIGHT_PIN 21

// SD Card Pins (HSPI)
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5

SPIClass touchSpi = SPIClass(VSPI);
SPIClass sdSpi = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

bool sdCardPresent = false;
const char* SD_FILE_PATH = "/cubyy_session.json";

// Flaga zapobiegająca podwójnemu zapisowi z dwóch pakietów BLE
volatile bool solveAlreadySaved = false;

// ==========================================
// MATERIAL DESIGN 3 - PALETA KOLORÓW
// ==========================================
uint16_t M3_BG;
uint16_t M3_SURFACE_CONTAINER;
uint16_t M3_PRIMARY;
uint16_t M3_ON_PRIMARY;
uint16_t M3_TEXT_PRIMARY;
uint16_t M3_TEXT_MUTED;
uint16_t M3_STATE_RED;
uint16_t M3_STATE_GREEN;

void initColors() {
  M3_BG                = tft.color565(14, 16, 19);
  M3_SURFACE_CONTAINER = tft.color565(26, 28, 32);
  M3_PRIMARY           = tft.color565(170, 199, 255);
  M3_ON_PRIMARY        = tft.color565(0, 48, 98);
  M3_TEXT_PRIMARY      = tft.color565(240, 240, 245);
  M3_TEXT_MUTED        = tft.color565(140, 145, 155);
  M3_STATE_RED         = tft.color565(255, 85, 85);
  M3_STATE_GREEN       = tft.color565(80, 220, 100);
}

// ==========================================
// BLE CONFIGURATION FOR GAN TIMER
// ==========================================
static BLEUUID SERVICE_UUID_128("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID SERVICE_UUID_16("fff0");
static BLEUUID STATE_CHAR_UUID("0000fff5-0000-1000-8000-00805f9b34fb");

enum AppState {
  STATE_DISCONNECTED,
  STATE_SCANNING,
  STATE_CONNECTING,
  STATE_CONNECTED
};

volatile AppState currentAppState = STATE_DISCONNECTED;
BLEClient* pClient = nullptr;

struct ScannedDevice {
  String address;
  String name;
  int rssi;
  BLEAdvertisedDevice rawDevice;
};

#define MAX_LIST_ITEMS 3
ScannedDevice scannedDevices[MAX_LIST_ITEMS];
int foundDevicesCount = 0;

// ==========================================
// SCRAMBLE GENERATOR (3x3)
// ==========================================
String currentScramble = "";
volatile bool scrambleNeedsUpdate = false;

String generateScramble(int length = 20) {
  const char faces[] = {'U', 'D', 'R', 'L', 'F', 'B'};
  const String modifiers[] = {"", "'", "2"};
  
  char scrambleFaces[30];
  String scrambleResult = "";
  int count = 0;

  while (count < length) {
    char newFace = faces[random(0, 6)];
    char last = (count > 0) ? scrambleFaces[count - 1] : '\0';
    char prevToLast = (count > 1) ? scrambleFaces[count - 2] : '\0';

    if (newFace == last) continue;

    bool isOpposite = (newFace == 'U' && last == 'D') || (newFace == 'D' && last == 'U') ||
                       (newFace == 'R' && last == 'L') || (newFace == 'L' && last == 'R') ||
                       (newFace == 'F' && last == 'B') || (newFace == 'B' && last == 'F');

    if (last != '\0' && isOpposite && newFace == prevToLast) continue;

    String mod = modifiers[random(0, 3)];
    scrambleFaces[count] = newFace;
    
    if (count > 0) scrambleResult += " ";
    scrambleResult += String(newFace) + mod;
    
    count++;
  }

  return scrambleResult;
}

// ==========================================
// GAN TIMER STATES & DATA
// ==========================================
enum TimerHandStatus {
  HANDS_OFF,
  HANDS_BOTH_PREPARE,
  HANDS_READY,
  TIMER_RUNNING,
  TIMER_FINISHED
};

volatile TimerHandStatus currentTimerStatus = HANDS_OFF;
volatile uint32_t ganHardwareTimeMs = 0;
volatile uint32_t localStartMillis = 0;
volatile uint32_t localElapsedMillis = 0;
volatile bool isTimerRunning = false;
volatile bool newPacketReceived = false;

String formatTime(uint32_t ms) {
  uint32_t minutes = ms / 60000;
  uint32_t seconds = (ms % 60000) / 1000;
  uint32_t millisec = ms % 1000;

  char buffer[16];
  if (minutes > 0) {
    snprintf(buffer, sizeof(buffer), "%lu:%02lu.%03lu", minutes, seconds, millisec);
  } else {
    snprintf(buffer, sizeof(buffer), "%lu.%03lu", seconds, millisec);
  }
  return String(buffer);
}

// ==========================================
// CS TIMER SD STORAGE HANDLER
// ==========================================
void saveSolveToSD(uint32_t timeMs, String scramble) {
  if (!sdCardPresent || timeMs == 0) return;

  JsonDocument doc;

  if (SD.exists(SD_FILE_PATH)) {
    File file = SD.open(SD_FILE_PATH, FILE_READ);
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      if (error) {
        doc.clear();
      }
    }
  }

  if (!doc["session1"].is<JsonArray>()) {
    doc.clear();
    doc.createNestedArray("session1");
    
    JsonObject properties = doc.createNestedObject("properties");
    properties["sessionData"] = "{\"1\":{\"name\":1,\"opt\":{},\"rank\":1}}";
    properties["scrFlt"] = "[\"333\",null]";
  }

  JsonArray session1 = doc["session1"].as<JsonArray>();

  JsonArray newSolve = session1.add<JsonArray>();
  JsonArray timeInfo = newSolve.add<JsonArray>();
  timeInfo.add(0);      // 0 = brak kary (+2 / DNF)
  timeInfo.add(timeMs); // czas w ms

  newSolve.add(scramble);
  newSolve.add("");     // Komentarz (pusty)
  newSolve.add(1787130434); // Pseudo-timestamp Unix

  File file = SD.open(SD_FILE_PATH, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("SD: Zapisano jednorazowo ułożenie w formacie csTimer!");
  } else {
    Serial.println("SD: Błąd otwarcia pliku do zapisu.");
  }
}

void drawMainUI();

// ==========================================
// BLE CALLBACKS
// ==========================================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) override {
    Serial.println("BLE: Connected");
  }

  void onDisconnect(BLEClient* pclient) override {
    Serial.println("BLE: Disconnected!");
    currentAppState = STATE_DISCONNECTED;
    isTimerRunning = false;
  }
};

// ==========================================
// UI DRAWING FUNCTIONS
// ==========================================
void drawScrambleHeader() {
  tft.fillRect(0, 0, 320, 50, M3_BG);
  
  if (currentAppState == STATE_CONNECTED) {
    tft.fillRoundRect(10, 6, 300, 40, 10, M3_SURFACE_CONTAINER);
    tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    
    if (currentScramble.length() > 38) {
      tft.drawString(currentScramble.substring(0, 38), 160, 18);
      tft.drawString(currentScramble.substring(38), 160, 32);
    } else {
      tft.drawString(currentScramble, 160, 26);
    }
  } else {
    tft.setTextColor(M3_PRIMARY, M3_BG);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Cubyy BLE", 160, 25);
  }
}

void drawMainUI() {
  tft.fillScreen(M3_BG);
  drawScrambleHeader();

  if (currentAppState != STATE_CONNECTED) {
    // Przycisk skanowania
    tft.fillRoundRect(40, 175, 240, 42, 21, M3_PRIMARY);
    tft.setTextColor(M3_ON_PRIMARY, M3_PRIMARY);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    
    if (currentAppState == STATE_SCANNING) {
      tft.drawString("SCANNING...", 160, 196);
    } else {
      tft.drawString("SEARCH TIMER", 160, 196);
    }

    // Lista urządzeń
    for (int i = 0; i < MAX_LIST_ITEMS; i++) {
      int y = 52 + (i * 38);
      if (i < foundDevicesCount) {
        tft.fillRoundRect(15, y, 290, 34, 10, M3_SURFACE_CONTAINER);
        tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
        tft.setTextSize(1);
        tft.setTextDatum(TL_DATUM);
        
        String nameStr = scannedDevices[i].name.length() > 0 ? scannedDevices[i].name : "GAN Smart Timer";
        tft.drawString(nameStr + " (" + String(scannedDevices[i].rssi) + "dBm)", 25, y + 5);
        tft.setTextColor(M3_PRIMARY, M3_SURFACE_CONTAINER);
        tft.drawString(scannedDevices[i].address, 25, y + 19);
      }
    }
  } else {
    // Przycisk rozłączenia
    tft.fillRoundRect(100, 180, 120, 32, 16, M3_SURFACE_CONTAINER);
    tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("DISCONNECT", 160, 196);
  }

  // Komunikat braku karty SD na dole ekranu
  if (!sdCardPresent) {
    tft.setTextColor(M3_STATE_RED, M3_BG);
    tft.setTextSize(1);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("No SD card detected - solves won't be saved", 160, 236);
  }
}

void updateLiveTimerUI() {
  if (currentAppState != STATE_CONNECTED) return;

  uint16_t timeColor = M3_TEXT_PRIMARY;
  String statusText = "";

  switch (currentTimerStatus) {
    case HANDS_OFF:
      timeColor = M3_TEXT_PRIMARY;
      statusText = "IDLE";
      break;

    case HANDS_BOTH_PREPARE:
      timeColor = M3_STATE_RED;
      statusText = "HOLDING...";
      break;

    case HANDS_READY:
      timeColor = M3_STATE_GREEN;
      statusText = "READY!";
      break;

    case TIMER_RUNNING:
      timeColor = M3_TEXT_PRIMARY;
      statusText = "SOLVING...";
      break;

    case TIMER_FINISHED:
      timeColor = M3_TEXT_PRIMARY;
      statusText = "FINISHED";
      break;
  }

  uint32_t displayTime = isTimerRunning ? (millis() - localStartMillis) : ganHardwareTimeMs;
  
  tft.fillRect(10, 80, 300, 65, M3_BG);
  tft.setTextColor(timeColor, M3_BG);
  tft.setTextSize(4);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(formatTime(displayTime), 160, 110);

  tft.fillRect(10, 150, 300, 20, M3_BG);
  tft.setTextColor(M3_TEXT_MUTED, M3_BG);
  tft.setTextSize(1);
  tft.drawString(statusText, 160, 160);
}

// ==========================================
// GAN TIMER BLE DECODER
// ==========================================
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  if (length < 4 || pData[0] != 0xFE) return;

  uint8_t stateCode = pData[3];

  switch (stateCode) {
    case 6: // HANDS_ON / PREPARE
      currentTimerStatus = HANDS_BOTH_PREPARE;
      solveAlreadySaved = false; // Reset flagi na starcie przygotowania
      break;

    case 1: // READY
      currentTimerStatus = HANDS_READY;
      solveAlreadySaved = false; // Reset flagi gdy timer gotowy
      break;

    case 2: // HANDS_OFF
      if (isTimerRunning) {
        isTimerRunning = false;
        localElapsedMillis = millis() - localStartMillis;
        currentTimerStatus = TIMER_FINISHED;
      } else if (currentTimerStatus != TIMER_FINISHED) {
        currentTimerStatus = HANDS_OFF;
      }
      break;

    case 3: // RUNNING
      if (!isTimerRunning) {
        isTimerRunning = true;
        localStartMillis = millis();
        ganHardwareTimeMs = 0;
        solveAlreadySaved = false; // Reset flagi podczas biegu
      }
      currentTimerStatus = TIMER_RUNNING;
      break;

    case 4: // STOPPED
    case 7: // FINISHED
      if (isTimerRunning) {
        isTimerRunning = false;
        localElapsedMillis = millis() - localStartMillis;
      }
      currentTimerStatus = TIMER_FINISHED;

      if (length >= 8) {
        uint32_t minutes = pData[4];
        uint32_t seconds = pData[5];
        uint32_t millisec = pData[6] | (pData[7] << 8);
        ganHardwareTimeMs = (minutes * 60000) + (seconds * 1000) + millisec;
      } else if (ganHardwareTimeMs == 0) {
        ganHardwareTimeMs = localElapsedMillis;
      }

      // --- JEDNORAZOWY ZAPIS I GENEROWANIE NOWEGO SCRAMBLE ---
      if (!solveAlreadySaved) {
        solveAlreadySaved = true; // Zablokowanie kolejnych pakietów powiadomień
        saveSolveToSD(ganHardwareTimeMs, currentScramble);
        currentScramble = generateScramble();
        scrambleNeedsUpdate = true;
      }
      break;

    case 5: // RESET / IDLE
      isTimerRunning = false;
      currentTimerStatus = HANDS_OFF;
      ganHardwareTimeMs = 0;
      localElapsedMillis = 0;
      solveAlreadySaved = false;
      break;
  }

  newPacketReceived = true;
}

// ==========================================
// SCAN & CONNECTION LOGIC
// ==========================================
void startBLEScan() {
  currentAppState = STATE_SCANNING;
  foundDevicesCount = 0;
  drawMainUI();

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  BLEScanResults* foundDevices = pBLEScan->start(5, false);

  for (int i = 0; i < foundDevices->getCount(); i++) {
    BLEAdvertisedDevice dev = foundDevices->getDevice(i);
    String devName = dev.getName().c_str();
    String devMac = dev.getAddress().toString().c_str();

    String nameUpper = devName;
    nameUpper.toUpperCase();

    if (nameUpper.indexOf("GAN") != -1) {
      if (foundDevicesCount < MAX_LIST_ITEMS) {
        scannedDevices[foundDevicesCount].address = devMac;
        scannedDevices[foundDevicesCount].name = devName;
        scannedDevices[foundDevicesCount].rssi = dev.getRSSI();
        scannedDevices[foundDevicesCount].rawDevice = dev;
        foundDevicesCount++;
      }
    }
  }

  pBLEScan->clearResults();
  currentAppState = STATE_DISCONNECTED;
  drawMainUI();
}

bool connectToSelectedDevice(int index) {
  if (index >= foundDevicesCount) return false;

  currentAppState = STATE_CONNECTING;
  drawMainUI();

  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
  }

  bool connected = pClient->connect(&scannedDevices[index].rawDevice);

  if (!connected) {
    currentAppState = STATE_DISCONNECTED;
    delay(1000);
    drawMainUI();
    return false;
  }

  BLERemoteService* pService = pClient->getService(SERVICE_UUID_128);
  if (!pService) pService = pClient->getService(SERVICE_UUID_16);

  if (pService) {
    BLERemoteCharacteristic* pChar = pService->getCharacteristic(STATE_CHAR_UUID);
    if (pChar && pChar->canNotify()) {
      pChar->registerForNotify(notifyCallback);

      BLERemoteDescriptor* p2902 = pChar->getDescriptor(BLEUUID((uint16_t)0x2902));
      if (p2902 != nullptr) {
        uint8_t notifyOn[] = {0x01, 0x00};
        p2902->writeValue(notifyOn, 2, true);
      }
    }
  }

  currentScramble = generateScramble();
  currentAppState = STATE_CONNECTED;
  drawMainUI();
  updateLiveTimerUI();
  return true;
}

void disconnectDevice() {
  if (pClient && pClient->isConnected()) {
    pClient->disconnect();
  }
  currentAppState = STATE_DISCONNECTED;
  isTimerRunning = false;
  ganHardwareTimeMs = 0;
  localElapsedMillis = 0;
  solveAlreadySaved = false;
  drawMainUI();
}

// ==========================================
// SETUP & MAIN LOOP
// ==========================================
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);

  initColors();

  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  // Inicjalizacja SD Card (HSPI)
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSpi) && SD.cardType() != CARD_NONE) {
    sdCardPresent = true;
    Serial.println("SD: Wykryto i zamontowano kartę.");
  } else {
    sdCardPresent = false;
    Serial.println("SD: Brak karty SD.");
  }

  // Inicjalizacja Touch (VSPI)
  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  randomSeed(analogRead(34) + micros());

  BLEDevice::init("CYD_Gan_Timer");
  drawMainUI();
}

void loop() {
  static AppState lastState = STATE_DISCONNECTED;

  if (lastState == STATE_CONNECTED && currentAppState == STATE_DISCONNECTED) {
    drawMainUI();
  }
  lastState = currentAppState;

  if (scrambleNeedsUpdate) {
    drawScrambleHeader();
    scrambleNeedsUpdate = false;
  }

  if (currentAppState == STATE_CONNECTED) {
    if (isTimerRunning || newPacketReceived) {
      if (isTimerRunning) {
        localElapsedMillis = millis() - localStartMillis;
      }
      updateLiveTimerUI();
      newPacketReceived = false;
    }
  }

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 240, 3800, 0, 240);

    if (currentAppState == STATE_DISCONNECTED) {
      if (x >= 40 && x <= 280 && y >= 175 && y <= 217) {
        startBLEScan();
        delay(300);
      }
      for (int i = 0; i < foundDevicesCount; i++) {
        int itemY = 52 + (i * 38);
        if (x >= 15 && x <= 305 && y >= itemY && y <= (itemY + 34)) {
          connectToSelectedDevice(i);
          delay(300);
          break;
        }
      }
    } else if (currentAppState == STATE_CONNECTED) {
      if (x >= 100 && x <= 220 && y >= 180 && y <= 212) {
        disconnectDevice();
        delay(300);
      }
    }
  }

  delay(10);
}