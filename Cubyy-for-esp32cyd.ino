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
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33
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

// Flaga zapobiegająca podwójnemu zapisowi
volatile bool solveAlreadySaved = false;

// ==========================================
// MATERIAL DESIGN 3 - PALETA KOLORÓW
// ==========================================
uint16_t M3_BG;
uint16_t M3_SURFACE_CONTAINER;
uint16_t M3_SURFACE_CONTAINER_HIGH;
uint16_t M3_PRIMARY;
uint16_t M3_ON_PRIMARY;
uint16_t M3_TEXT_PRIMARY;
uint16_t M3_TEXT_MUTED;
uint16_t M3_STATE_RED;
uint16_t M3_STATE_GREEN;
uint16_t M3_CARD_BG;

void initColors() {
  M3_BG                     = tft.color565(14, 16, 19);
  M3_SURFACE_CONTAINER      = tft.color565(26, 28, 32);
  M3_SURFACE_CONTAINER_HIGH = tft.color565(40, 42, 48);
  M3_PRIMARY                = tft.color565(170, 199, 255);
  M3_ON_PRIMARY             = tft.color565(0, 48, 98);
  M3_TEXT_PRIMARY           = tft.color565(240, 240, 245);
  M3_TEXT_MUTED             = tft.color565(140, 145, 155);
  M3_STATE_RED              = tft.color565(255, 85, 85);
  M3_STATE_GREEN            = tft.color565(80, 220, 100);
  M3_CARD_BG                = tft.color565(210, 225, 255);
}

// ==========================================
// APP NAVIGATION & DIALOG STATES
// ==========================================
enum ActiveScreen {
  SCREEN_TIMER,
  SCREEN_SOLVES,
  SCREEN_STATISTICS,
  SCREEN_BLE_SCAN
};

ActiveScreen currentScreen = SCREEN_TIMER;
bool isSidebarOpen = false;
bool isDisconnectModalOpen = false;
bool isSolveDetailsOpen = false;
int selectedSolveIndex = -1;

// Paginacja dla ekranu ułożeń
int solvesPage = 0;

struct SolveData {
  uint32_t timeMs;
  String scramble;
};

#define MAX_SOLVES_LOADED 100
SolveData loadedSolves[MAX_SOLVES_LOADED];
int totalSolvesCount = 0;

// ==========================================
// BLE CONFIGURATION FOR GAN TIMER
// ==========================================
static BLEUUID SERVICE_UUID_128("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID SERVICE_UUID_16("fff0");
static BLEUUID STATE_CHAR_UUID("0000fff5-0000-1000-8000-00805f9b34fb");

enum AppBleState {
  BLE_DISCONNECTED,
  BLE_SCANNING,
  BLE_CONNECTING,
  BLE_CONNECTED
};

volatile AppBleState currentBleState = BLE_DISCONNECTED;
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
    char newFace = faces[esp_random() % 6];
    char last = (count > 0) ? scrambleFaces[count - 1] : '\0';
    char prevToLast = (count > 1) ? scrambleFaces[count - 2] : '\0';

    if (newFace == last) continue;

    bool isOpposite = (newFace == 'U' && last == 'D') || (newFace == 'D' && last == 'U') ||
                      (newFace == 'R' && last == 'L') || (newFace == 'L' && last == 'R') ||
                      (newFace == 'F' && last == 'B') || (newFace == 'B' && last == 'F');

    if (last != '\0' && isOpposite && newFace == prevToLast) continue;

    String mod = modifiers[esp_random() % 3];
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
// SD STORAGE HANDLERS (CS TIMER COMPATIBLE)
// ==========================================
void loadSolvesFromSD() {
  totalSolvesCount = 0;
  if (!sdCardPresent || !SD.exists(SD_FILE_PATH)) return;

  File file = SD.open(SD_FILE_PATH, FILE_READ);
  if (!file) return;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) return;

  JsonArray session1 = doc["session1"].as<JsonArray>();
  int count = 0;
  
  for (int i = session1.size() - 1; i >= 0 && count < MAX_SOLVES_LOADED; i--) {
    JsonArray solve = session1[i];
    loadedSolves[count].timeMs = solve[0][1].as<uint32_t>();
    loadedSolves[count].scramble = solve[1].as<String>();
    count++;
  }
  totalSolvesCount = count;
}

void saveSolveToSD(uint32_t timeMs, String scramble) {
  if (!sdCardPresent || timeMs == 0) return;

  JsonDocument doc;
  if (SD.exists(SD_FILE_PATH)) {
    File file = SD.open(SD_FILE_PATH, FILE_READ);
    if (file) {
      deserializeJson(doc, file);
      file.close();
    }
  }

  if (!doc["session1"].is<JsonArray>()) {
    doc.clear();
    doc.createNestedArray("session1");
  }

  JsonArray session1 = doc["session1"].as<JsonArray>();
  JsonArray newSolve = session1.add<JsonArray>();
  JsonArray timeInfo = newSolve.add<JsonArray>();
  timeInfo.add(0);
  timeInfo.add(timeMs);
  newSolve.add(scramble);
  newSolve.add("");
  newSolve.add(1787130434);

  File file = SD.open(SD_FILE_PATH, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void deleteSolveFromSD(int displayIndex) {
  if (!sdCardPresent || !SD.exists(SD_FILE_PATH)) return;

  JsonDocument doc;
  File file = SD.open(SD_FILE_PATH, FILE_READ);
  if (file) {
    deserializeJson(doc, file);
    file.close();
  }

  JsonArray session1 = doc["session1"].as<JsonArray>();
  int targetIndex = session1.size() - 1 - displayIndex;

  if (targetIndex >= 0 && targetIndex < session1.size()) {
    session1.remove(targetIndex);
    File outFile = SD.open(SD_FILE_PATH, FILE_WRITE);
    if (outFile) {
      serializeJson(doc, outFile);
      outFile.close();
    }
  }
  loadSolvesFromSD();
}

// Declarations
void drawMainScreen();
void drawSidebar();
void drawDisconnectModal();

// ==========================================
// CALCULATIONS FOR STATISTICS
// ==========================================
uint32_t computeAoN(int n, int startIdx) {
  if (totalSolvesCount < startIdx + n) return 0;
  
  uint32_t temp[100];
  for (int i = 0; i < n; i++) {
    temp[i] = loadedSolves[startIdx + i].timeMs;
  }

  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (temp[i] > temp[j]) {
        uint32_t t = temp[i];
        temp[i] = temp[j];
        temp[j] = t;
      }
    }
  }

  int trim = 1;
  if (n == 50) trim = 3;       // 5% z 50 = 2.5 -> 3
  else if (n == 100) trim = 5; // 5% z 100 = 5

  uint64_t sum = 0;
  for (int i = trim; i < n - trim; i++) {
    sum += temp[i];
  }
  return (uint32_t)(sum / (n - 2 * trim));
}

uint32_t getBestAoN(int n) {
  if (totalSolvesCount < n) return 0;
  uint32_t best = 0xFFFFFFFF;
  for (int i = 0; i <= totalSolvesCount - n; i++) {
    uint32_t val = computeAoN(n, i);
    if (val > 0 && val < best) {
      best = val;
    }
  }
  return (best == 0xFFFFFFFF) ? 0 : best;
}

// ==========================================
// VECTOR DRAWING HELPER FUNCTIONS
// ==========================================
void drawBluetoothIcon(int x, int y, uint16_t color) {
  tft.drawLine(x, y - 7, x, y + 7, color);
  tft.drawLine(x - 4, y - 4, x + 4, y + 3, color);
  tft.drawLine(x + 4, y + 3, x, y + 7, color);
  tft.drawLine(x - 4, y + 4, x + 4, y - 3, color);
  tft.drawLine(x + 4, y - 3, x, y - 7, color);
}

void drawThreeDotsIcon(int x, int y, uint16_t color) {
  tft.fillCircle(x, y - 8, 2, color);
  tft.fillCircle(x, y, 2, color);
  tft.fillCircle(x, y + 8, 2, color);
}

void drawTimerIcon(int x, int y, uint16_t color) {
  tft.drawCircle(x, y + 2, 6, color);
  tft.drawLine(x, y - 6, x, y - 4, color);
  tft.drawLine(x, y + 2, x + 2, y, color);
}

void drawListIcon(int x, int y, uint16_t color) {
  tft.drawRect(x - 6, y - 6, 12, 12, color);
  tft.drawLine(x - 3, y - 2, x + 3, y - 2, color);
  tft.drawLine(x - 3, y + 2, x + 3, y + 2, color);
}

void drawStatsIcon(int x, int y, uint16_t color) {
  tft.fillRect(x - 6, y + 2, 3, 6, color);
  tft.fillRect(x - 1, y - 2, 3, 10, color);
  tft.fillRect(x + 4, y - 6, 3, 14, color);
}

void drawTrashIcon(int x, int y, uint16_t color) {
  tft.drawRect(x - 5, y - 3, 10, 10, color);
  tft.drawLine(x - 7, y - 5, x + 7, y - 5, color);
  tft.drawLine(x - 2, y - 7, x + 2, y - 7, color);
}

void drawCloseIcon(int x, int y, uint16_t color) {
  tft.drawLine(x - 5, y - 3, x, y + 3, color);
  tft.drawLine(x, y + 3, x + 5, y - 3, color);
}

// ==========================================
// BLE CALLBACKS
// ==========================================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) override {}
  void onDisconnect(BLEClient* pclient) override {
    currentBleState = BLE_DISCONNECTED;
    isTimerRunning = false;
  }
};

// ==========================================
// UI DRAWING FUNCTIONS
// ==========================================
void drawHeader() {
  tft.fillCircle(25, 26, 20, M3_SURFACE_CONTAINER);
  drawThreeDotsIcon(25, 26, M3_TEXT_PRIMARY);

  if (currentScreen == SCREEN_TIMER) {
    tft.fillRoundRect(55, 6, 255, 40, 10, M3_SURFACE_CONTAINER);
    tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);

    if (currentScramble.length() > 34) {
      tft.drawString(currentScramble.substring(0, 34), 182, 18);
      tft.drawString(currentScramble.substring(34), 182, 32);
    } else {
      tft.drawString(currentScramble, 182, 26);
    }

    uint16_t btBg = (currentBleState == BLE_CONNECTED) ? M3_PRIMARY : M3_SURFACE_CONTAINER;
    uint16_t btFg = (currentBleState == BLE_CONNECTED) ? M3_ON_PRIMARY : M3_TEXT_PRIMARY;
    
    tft.fillCircle(285, 66, 15, btBg);
    drawBluetoothIcon(285, 66, btFg);
  }
}

void drawLiveTimerUI() {
  if (currentScreen != SCREEN_TIMER) return;

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

  tft.fillRect(10, 85, 300, 65, M3_BG);
  tft.setTextColor(timeColor, M3_BG);
  tft.setTextSize(4);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(formatTime(displayTime), 160, 115);

  tft.fillRect(10, 155, 300, 25, M3_BG);
  tft.setTextColor(M3_TEXT_MUTED, M3_BG);
  tft.setTextSize(1);
  tft.drawString(statusText, 160, 165);
}

void drawSolvesScreen() {
  tft.fillRect(0, 52, 320, 188, M3_BG);

  if (totalSolvesCount == 0) {
    tft.setTextColor(M3_TEXT_MUTED, M3_BG);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No solves on SD card", 160, 130);
    return;
  }

  int solvesPerPage = 6;
  int totalPages = (totalSolvesCount + solvesPerPage - 1) / solvesPerPage;

  if (solvesPage >= totalPages) solvesPage = totalPages - 1;
  if (solvesPage < 0) solvesPage = 0;

  int startIndex = solvesPage * solvesPerPage;
  int endIndex = min(startIndex + solvesPerPage, totalSolvesCount);

  int cols = 3;
  int tileWidth = 94;
  int tileHeight = 45;
  int gapX = 8;
  int gapY = 8;
  int startX = 10;
  int startY = 60;

  for (int i = startIndex; i < endIndex; i++) {
    int localIdx = i - startIndex;
    int col = localIdx % cols;
    int row = localIdx / cols;

    int x = startX + col * (tileWidth + gapX);
    int y = startY + row * (tileHeight + gapY);

    tft.fillRoundRect(x, y, tileWidth, tileHeight, 10, M3_CARD_BG);
    tft.setTextColor(tft.color565(20, 40, 80), M3_CARD_BG);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(formatTime(loadedSolves[i].timeMs), x + (tileWidth / 2), y + (tileHeight / 2));
  }

  // --- PASEK NAWIGACJI STROŃ ---
  int navY = 188;
  
  // Przycisk W LEWO (<)
  uint16_t prevBtnBg = (solvesPage > 0) ? M3_SURFACE_CONTAINER : M3_SURFACE_CONTAINER_HIGH;
  uint16_t prevBtnFg = (solvesPage > 0) ? M3_PRIMARY : M3_TEXT_MUTED;
  tft.fillRoundRect(60, navY, 40, 32, 8, prevBtnBg);
  tft.setTextColor(prevBtnFg, prevBtnBg);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("<", 80, navY + 16);

  // Informacja o stronie (np. "1 / 4")
  tft.setTextColor(M3_TEXT_PRIMARY, M3_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  String pageStr = String(solvesPage + 1) + " / " + String(totalPages);
  tft.drawString(pageStr, 160, navY + 16);

  // Przycisk W PRAWO (>)
  uint16_t nextBtnBg = (solvesPage < totalPages - 1) ? M3_SURFACE_CONTAINER : M3_SURFACE_CONTAINER_HIGH;
  uint16_t nextBtnFg = (solvesPage < totalPages - 1) ? M3_PRIMARY : M3_TEXT_MUTED;
  tft.fillRoundRect(220, navY, 40, 32, 8, nextBtnBg);
  tft.setTextColor(nextBtnFg, nextBtnBg);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(">", 240, navY + 16);
}

void drawStatisticsScreen() {
  tft.fillRect(0, 52, 320, 188, M3_BG);
  loadSolvesFromSD();

  if (totalSolvesCount == 0) {
    tft.setTextColor(M3_TEXT_MUTED, M3_BG);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No data on SD card", 160, 130);
    return;
  }

  // 1. Liczenie Najlepszego i Najgorszego ułożenia
  uint32_t bestSingle = 0xFFFFFFFF;
  uint32_t worstSingle = 0;
  for (int i = 0; i < totalSolvesCount; i++) {
    if (loadedSolves[i].timeMs < bestSingle) bestSingle = loadedSolves[i].timeMs;
    if (loadedSolves[i].timeMs > worstSingle) worstSingle = loadedSolves[i].timeMs;
  }

  // 2. Liczenie Średnich (Aktualne i Najlepsze)
  uint32_t curAo5 = computeAoN(5, 0);
  uint32_t bestAo5 = getBestAoN(5);

  uint32_t curAo12 = computeAoN(12, 0);
  uint32_t bestAo12 = getBestAoN(12);

  uint32_t curAo50 = computeAoN(50, 0);
  uint32_t bestAo50 = getBestAoN(50);

  uint32_t curAo100 = computeAoN(100, 0);
  uint32_t bestAo100 = getBestAoN(100);

  // --- PODSUMOWANIE GÓRNE ---
  tft.fillRoundRect(10, 56, 96, 30, 8, M3_SURFACE_CONTAINER);
  tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("SOLVES", 16, 60);
  tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
  tft.drawString(String(totalSolvesCount), 16, 71);

  tft.fillRoundRect(112, 56, 96, 30, 8, M3_SURFACE_CONTAINER);
  tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
  tft.drawString("BEST", 118, 60);
  tft.setTextColor(M3_STATE_GREEN, M3_SURFACE_CONTAINER);
  tft.drawString(formatTime(bestSingle), 118, 71);

  tft.fillRoundRect(214, 56, 96, 30, 8, M3_SURFACE_CONTAINER);
  tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
  tft.drawString("WORST", 220, 60);
  tft.setTextColor(M3_STATE_RED, M3_SURFACE_CONTAINER);
  tft.drawString(formatTime(worstSingle), 220, 71);

  // --- TABELA ŚREDNICH ---
  auto drawAvgCard = [](int x, int y, int w, int h, const char* label, uint32_t cur, uint32_t best) {
    tft.fillRoundRect(x, y, w, h, 8, M3_SURFACE_CONTAINER);
    tft.setTextColor(M3_PRIMARY, M3_SURFACE_CONTAINER);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(label, x + 6, y + 4);

    tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
    String curStr = (cur > 0) ? formatTime(cur) : "--";
    tft.drawString("Cur: " + curStr, x + 6, y + 18);

    tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
    String bestStr = (best > 0) ? formatTime(best) : "--";
    tft.drawString("B: " + bestStr, x + 76, y + 18);
  };

  drawAvgCard(10, 92, 146, 32, "Ao5", curAo5, bestAo5);
  drawAvgCard(164, 92, 146, 32, "Ao12", curAo12, bestAo12);
  drawAvgCard(10, 128, 146, 32, "Ao50", curAo50, bestAo50);
  drawAvgCard(164, 128, 146, 32, "Ao100", curAo100, bestAo100);

  // --- HISTOGRAM ROZKŁADU CZASÓW (POPRAWIONY) ---
  int chartX = 10;
  int chartY = 164;
  int chartW = 300;
  int chartH = 72;

  tft.fillRoundRect(chartX, chartY, chartW, chartH, 8, M3_SURFACE_CONTAINER);
  tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("DISTRIBUTION (s)", chartX + 8, chartY + 3);

  const int NUM_BUCKETS = 7;
  int buckets[NUM_BUCKETS] = {0};

  if (totalSolvesCount > 0) {
    uint32_t minSec = bestSingle / 1000;
    uint32_t maxSec = worstSingle / 1000;
    uint32_t rangeSec = (maxSec >= minSec) ? (maxSec - minSec + 1) : 1;

    uint32_t bucketSizeSec = (rangeSec + NUM_BUCKETS - 1) / NUM_BUCKETS;
    if (bucketSizeSec == 0) bucketSizeSec = 1;

    for (int i = 0; i < totalSolvesCount; i++) {
      uint32_t solveSec = loadedSolves[i].timeMs / 1000;
      int bIdx = (solveSec - minSec) / bucketSizeSec;
      if (bIdx >= NUM_BUCKETS) bIdx = NUM_BUCKETS - 1;
      if (bIdx < 0) bIdx = 0;
      buckets[bIdx]++;
    }

    int maxCount = 0;
    for (int b = 0; b < NUM_BUCKETS; b++) {
      if (buckets[b] > maxCount) maxCount = buckets[b];
    }

    int barWidth = 28;
    int barGap = 11;
    int startBarX = chartX + 16;
    int maxBarHeight = 28;
    int baseBarY = chartY + 54;

    for (int b = 0; b < NUM_BUCKETS; b++) {
      int bx = startBarX + b * (barWidth + barGap);
      int h = (maxCount > 0) ? (buckets[b] * maxBarHeight / maxCount) : 0;
      if (buckets[b] > 0 && h < 3) h = 3;

      if (h > 0) {
        uint16_t barColor = (buckets[b] == maxCount) ? M3_PRIMARY : M3_SURFACE_CONTAINER_HIGH;
        tft.fillRoundRect(bx, baseBarY - h, barWidth, h, 3, barColor);
      }

      if (buckets[b] > 0) {
        tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
        tft.setTextDatum(BC_DATUM);
        tft.drawString(String(buckets[b]), bx + (barWidth / 2), baseBarY - h - 1);
      }

      uint32_t bStartSec = minSec + (b * bucketSizeSec);
      String rangeLabel = String(bStartSec) + "s";

      tft.setTextColor(M3_TEXT_MUTED, M3_SURFACE_CONTAINER);
      tft.setTextDatum(TC_DATUM);
      tft.drawString(rangeLabel, bx + (barWidth / 2), baseBarY + 3);
    }
  }
}

void drawSolveDetailsModal(int index) {
  if (index < 0 || index >= totalSolvesCount) return;

  uint16_t modalBg = tft.color565(45, 52, 64);
  tft.fillRoundRect(10, 10, 300, 220, 16, modalBg);

  tft.fillRoundRect(220, 20, 32, 32, 8, tft.color565(65, 75, 92));
  drawTrashIcon(236, 36, M3_TEXT_PRIMARY);

  tft.fillRoundRect(258, 20, 32, 32, 8, M3_PRIMARY);
  drawCloseIcon(274, 36, M3_ON_PRIMARY);

  tft.setTextColor(M3_TEXT_PRIMARY, modalBg);
  tft.setTextSize(4);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(formatTime(loadedSolves[index].timeMs), 160, 95);

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(200, 210, 225), modalBg);
  String scr = loadedSolves[index].scramble;
  
  if (scr.length() > 32) {
    tft.drawString(scr.substring(0, 32), 160, 145);
    tft.drawString(scr.substring(32), 160, 160);
  } else {
    tft.drawString(scr, 160, 150);
  }
}

void drawMainScreen() {
  tft.fillScreen(M3_BG);
  drawHeader();

  if (currentScreen == SCREEN_TIMER) {
    drawLiveTimerUI();
  } else if (currentScreen == SCREEN_SOLVES) {
    loadSolvesFromSD();
    drawSolvesScreen();
  } else if (currentScreen == SCREEN_STATISTICS) {
    drawStatisticsScreen();
  } else if (currentScreen == SCREEN_BLE_SCAN) {
    tft.fillRoundRect(15, 185, 90, 40, 12, M3_SURFACE_CONTAINER);
    tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CANCEL", 60, 205);

    tft.fillRoundRect(115, 185, 190, 40, 12, M3_PRIMARY);
    tft.setTextColor(M3_ON_PRIMARY, M3_PRIMARY);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);

    if (currentBleState == BLE_SCANNING) {
      tft.drawString("SCANNING...", 210, 205);
    } else {
      tft.drawString("SEARCH", 210, 205);
    }

    for (int i = 0; i < MAX_LIST_ITEMS; i++) {
      int y = 55 + (i * 38);
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
  }

  if (isSidebarOpen) {
    drawSidebar();
  } else if (isDisconnectModalOpen) {
    drawDisconnectModal();
  } else if (isSolveDetailsOpen) {
    drawSolveDetailsModal(selectedSolveIndex);
  }
}

void drawSidebar() {
  int sidebarWidth = 140;
  tft.fillRoundRect(0, 0, sidebarWidth, 240, 16, M3_SURFACE_CONTAINER);

  uint16_t bg1 = (currentScreen == SCREEN_TIMER) ? M3_SURFACE_CONTAINER_HIGH : M3_SURFACE_CONTAINER;
  tft.fillRoundRect(8, 12, 124, 38, 10, bg1);
  drawTimerIcon(24, 31, M3_PRIMARY);
  tft.setTextColor(M3_TEXT_PRIMARY, bg1);
  tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Timer", 42, 31);

  uint16_t bg2 = (currentScreen == SCREEN_SOLVES) ? M3_SURFACE_CONTAINER_HIGH : M3_SURFACE_CONTAINER;
  tft.fillRoundRect(8, 56, 124, 38, 10, bg2);
  drawListIcon(24, 75, M3_PRIMARY);
  tft.setTextColor(M3_TEXT_PRIMARY, bg2);
  tft.drawString("Solves", 42, 75);

  uint16_t bg3 = (currentScreen == SCREEN_STATISTICS) ? M3_SURFACE_CONTAINER_HIGH : M3_SURFACE_CONTAINER;
  tft.fillRoundRect(8, 100, 124, 38, 10, bg3);
  drawStatsIcon(24, 119, M3_PRIMARY);
  tft.setTextColor(M3_TEXT_PRIMARY, bg3);
  tft.drawString("Statistics", 42, 119);
}

void drawDisconnectModal() {
  tft.fillRoundRect(30, 60, 260, 120, 16, M3_SURFACE_CONTAINER_HIGH);
  tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER_HIGH);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Disconnect BLE Timer?", 160, 85);

  tft.fillRoundRect(45, 120, 100, 36, 10, M3_SURFACE_CONTAINER);
  tft.setTextColor(M3_TEXT_PRIMARY, M3_SURFACE_CONTAINER);
  tft.drawString("Cancel", 95, 138);

  tft.fillRoundRect(175, 120, 100, 36, 10, M3_STATE_RED);
  tft.setTextColor(M3_TEXT_PRIMARY, M3_STATE_RED);
  tft.drawString("Disconnect", 225, 138);
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
    case 6:
      currentTimerStatus = HANDS_BOTH_PREPARE;
      solveAlreadySaved = false;
      break;

    case 1:
      currentTimerStatus = HANDS_READY;
      solveAlreadySaved = false;
      break;

    case 2:
      if (isTimerRunning) {
        isTimerRunning = false;
        localElapsedMillis = millis() - localStartMillis;
        currentTimerStatus = TIMER_FINISHED;
      } else if (currentTimerStatus != TIMER_FINISHED) {
        currentTimerStatus = HANDS_OFF;
      }
      break;

    case 3:
      if (!isTimerRunning) {
        isTimerRunning = true;
        localStartMillis = millis();
        ganHardwareTimeMs = 0;
        solveAlreadySaved = false;
      }
      currentTimerStatus = TIMER_RUNNING;
      break;

    case 4:
    case 7:
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

      if (!solveAlreadySaved) {
        solveAlreadySaved = true;
        saveSolveToSD(ganHardwareTimeMs, currentScramble);
        currentScramble = generateScramble();
        scrambleNeedsUpdate = true;
      }
      break;

    case 5:
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
  currentBleState = BLE_SCANNING;
  foundDevicesCount = 0;
  drawMainScreen();

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  BLEScanResults* foundDevices = pBLEScan->start(5, false);

  if (foundDevices != nullptr) {
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
  }

  pBLEScan->clearResults();
  currentBleState = BLE_DISCONNECTED;
  drawMainScreen();
}

bool connectToSelectedDevice(int index) {
  if (index >= foundDevicesCount) return false;

  currentBleState = BLE_CONNECTING;
  drawMainScreen();

  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
  }

  bool connected = pClient->connect(&scannedDevices[index].rawDevice);

  if (!connected) {
    currentBleState = BLE_DISCONNECTED;
    delay(1000);
    drawMainScreen();
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

  currentBleState = BLE_CONNECTED;
  currentScreen = SCREEN_TIMER;
  drawMainScreen();
  return true;
}

void disconnectDevice() {
  if (pClient && pClient->isConnected()) {
    pClient->disconnect();
  }
  currentBleState = BLE_DISCONNECTED;
  isTimerRunning = false;
  ganHardwareTimeMs = 0;
  localElapsedMillis = 0;
  solveAlreadySaved = false;
  drawMainScreen();
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

  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSpi) && SD.cardType() != CARD_NONE) {
    sdCardPresent = true;
  } else {
    sdCardPresent = false;
  }

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  BLEDevice::init("CYD_Gan_Timer");
  currentScramble = generateScramble();
  
  drawMainScreen();
}

void loop() {
  if (scrambleNeedsUpdate) {
    drawHeader();
    scrambleNeedsUpdate = false;
  }

  if (currentBleState == BLE_CONNECTED && currentScreen == SCREEN_TIMER) {
    if (isTimerRunning || newPacketReceived) {
      if (isTimerRunning) {
        localElapsedMillis = millis() - localStartMillis;
      }
      drawLiveTimerUI();
      newPacketReceived = false;
    }
  }

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 240, 3800, 0, 240);

    if (isSolveDetailsOpen) {
      if (x >= 220 && x <= 252 && y >= 20 && y <= 52) {
        deleteSolveFromSD(selectedSolveIndex);
        isSolveDetailsOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
      if (x >= 258 && x <= 290 && y >= 20 && y <= 52) {
        isSolveDetailsOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
    }
    else if (isDisconnectModalOpen) {
      if (x >= 45 && x <= 145 && y >= 120 && y <= 156) {
        isDisconnectModalOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
      if (x >= 175 && x <= 275 && y >= 120 && y <= 156) {
        isDisconnectModalOpen = false;
        disconnectDevice();
        delay(250);
        return;
      }
    } 
    else if (isSidebarOpen) {
      if (x >= 8 && x <= 132 && y >= 12 && y <= 50) {
        currentScreen = SCREEN_TIMER;
        isSidebarOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
      if (x >= 8 && x <= 132 && y >= 56 && y <= 94) {
        currentScreen = SCREEN_SOLVES;
        solvesPage = 0;
        isSidebarOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
      if (x >= 8 && x <= 132 && y >= 100 && y <= 138) {
        currentScreen = SCREEN_STATISTICS;
        isSidebarOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
      if (x > 140) {
        isSidebarOpen = false;
        drawMainScreen();
        delay(250);
        return;
      }
    } 
    else {
      if (x >= 5 && x <= 45 && y >= 6 && y <= 46) {
        isSidebarOpen = true;
        drawSidebar();
        delay(250);
        return;
      }

      if (currentScreen == SCREEN_TIMER && x >= 265 && x <= 305 && y >= 46 && y <= 86) {
        if (currentBleState == BLE_CONNECTED) {
          isDisconnectModalOpen = true;
          drawDisconnectModal();
        } else {
          currentScreen = SCREEN_BLE_SCAN;
          drawMainScreen();
        }
        delay(250);
        return;
      }

      if (currentScreen == SCREEN_SOLVES) {
        int solvesPerPage = 6;
        int totalPages = (totalSolvesCount + solvesPerPage - 1) / solvesPerPage;

        if (x >= 50 && x <= 110 && y >= 180 && y <= 225) {
          if (solvesPage > 0) {
            solvesPage--;
            drawSolvesScreen();
          }
          delay(250);
          return;
        }

        if (x >= 210 && x <= 270 && y >= 180 && y <= 225) {
          if (solvesPage < totalPages - 1) {
            solvesPage++;
            drawSolvesScreen();
          }
          delay(250);
          return;
        }

        int cols = 3;
        int tileWidth = 94;
        int tileHeight = 45;
        int gapX = 8;
        int gapY = 8;
        int startX = 10;
        int startY = 60;

        int startIndex = solvesPage * solvesPerPage;
        int endIndex = min(startIndex + solvesPerPage, totalSolvesCount);

        for (int i = startIndex; i < endIndex; i++) {
          int localIdx = i - startIndex;
          int col = localIdx % cols;
          int row = localIdx / cols;
          int tx = startX + col * (tileWidth + gapX);
          int ty = startY + row * (tileHeight + gapY);

          if (x >= tx && x <= (tx + tileWidth) && y >= ty && y <= (ty + tileHeight)) {
            selectedSolveIndex = i;
            isSolveDetailsOpen = true;
            drawSolveDetailsModal(selectedSolveIndex);
            delay(250);
            return;
          }
        }
      }

      if (currentScreen == SCREEN_BLE_SCAN) {
        if (x >= 15 && x <= 105 && y >= 185 && y <= 225) {
          currentScreen = SCREEN_TIMER;
          drawMainScreen();
          delay(250);
          return;
        }

        if (x >= 115 && x <= 305 && y >= 185 && y <= 225) {
          startBLEScan();
          delay(250);
          return;
        }

        for (int i = 0; i < foundDevicesCount; i++) {
          int itemY = 55 + (i * 38);
          if (x >= 15 && x <= 305 && y >= itemY && y <= (itemY + 34)) {
            connectToSelectedDevice(i);
            delay(250);
            break;
          }
        }
      }
    }
  }

  delay(10);
}