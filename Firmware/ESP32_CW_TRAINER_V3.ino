
/*
  ON2DK ESP32 CW TRAINER V3 - Morse Trainer Pro v3.3.0
  --------------------------------
  Eerste stabiele basisversie met:
  - ST7789 320x240 landscape
  - Hoofdmenu
  - Vrij oefenen
  - Koch-training
  - Instellingen
  - Cursistprofiel in ESP32 Preferences
  - Sessiescore en totaalscore
  - Leskeuze met zichtbare Koch-tekens
  - Na een geslaagde sessie keuze: volgende les / herhalen / kiezen
  - Niet-blokkerende morseweergave
  - Betrouwbare paddle-invoer zonder dubbele tekens

  Libraries:
  - Adafruit GFX Library
  - Adafruit ST7789
  - Adafruit BusIO
  - ESP32Encoder

  Definitieve ESP32 CW TRAINER V2 pinnen:
  TFT SCK         GPIO18
  TFT MOSI        GPIO23
  TFT DC          GPIO2
  TFT RESET       GPIO13
  TFT CS          GPIO5
  TFT BACKLIGHT   GPIO27

  Encoder CLK     GPIO16
  Encoder DT      GPIO17
  Encoder SW      GPIO26

  Paddle DIT      GPIO35
  Paddle DAH      GPIO32
  Straight key    GPIO4

  Audio/Buzzer    GPIO25
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP32Encoder.h>
#include <Preferences.h>

// -----------------------------------------------------------------------------
// CW TRAINER V2 display colours
// These aliases preserve the colour names used throughout the original trainer.
// -----------------------------------------------------------------------------
#define C_BG       ST77XX_BLACK
#define C_PANEL    ST77XX_BLACK
#define C_WHITE    ST77XX_WHITE
#define C_GREY     0x8410
#define C_DARKGREY 0x4208
#define C_ORANGE   0xFD20
#define C_GREEN    ST77XX_GREEN
#define C_RED      ST77XX_RED
#define C_AMBER    0xFBE0

#include <WiFi.h>
#include <WebServer.h>

// ============================================================
// HARDWARE
// ============================================================

#define TFT_SCK         18
#define TFT_MOSI        23
#define TFT_DC           2
#define TFT_RST         13
#define TFT_CS           5
#define TFT_BACKLIGHT   27

#define BUZZER_PIN      25
#define PADDLE_DIT      35
#define PADDLE_DAH      32
#define STRAIGHT_KEY     4

#define ENCODER_CLK     16
#define ENCODER_DT      17
#define ENCODER_SW      26

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
ESP32Encoder encoder;
Preferences prefs;
WebServer webServer(80);

// ============================================================
// KLEUREN
// ============================================================
#define C_PANEL       0x0000
#define C_BLACK       0x0000
#define C_BLUE        0x001F
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_CYAN        0x07FF
#define C_MAGENTA     0xF81F
#define C_YELLOW      0xFFE0
#define C_WHITE       0xFFFF

// color definitions TFT7789
const uint16_t  Display_Color_Black        = 0x0000;
const uint16_t  Display_Color_Blue         = 0x001F;
const uint16_t  Display_Color_Red          = 0xF800;
const uint16_t  Display_Color_Green        = 0x07E0;
const uint16_t  Display_Color_Cyan         = 0x07FF;
const uint16_t  Display_Color_Magenta      = 0xF81F;
const uint16_t  Display_Color_Yellow       = 0xFFE0;
const uint16_t  Display_Color_White        = 0xFFFF;

// ============================================================
// MORSE-DATABASE
// ============================================================

struct MorseEntry {
  char symbol;
  const char* code;
};

const MorseEntry morseTable[] = {
  {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
  {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
  {'I', ".."},{'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
  {'M', "--"},{'N', "-."}, {'O', "---"}, {'P', ".--."},
  {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
  {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
  {'Y', "-.--"}, {'Z', "--.."},
  {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
  {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
  {'9', "----."}, {'0', "-----"}, 
  {'.',".-.-.-"}, {',',"--..--"}, {'?',"..--.."}, {'/',"-..-."}, 
  {'=',"-...-"}, {'+',".-.-."}, {'-',"-....-"}, {'(',"-.--."},
  {')',"-.--.-"}, {'@',".--.-."},
 
};

const int MORSE_COUNT = sizeof(morseTable) / sizeof(morseTable[0]);

/*
  Eerste programmeerbare Koch-profiel.
  De eerste les bevat K en M. Elk volgend niveau voegt één teken toe.

  Deze reeks kan later via SD/webinterface gewijzigd worden zonder
  de trainingsengine opnieuw te schrijven.
*/
const char KOCH_ORDER[] =
 "KMRSUAPTOWNJEFLVGHDBXZQYC1234567890.,?/=+()@-";

const int KOCH_CHAR_COUNT = sizeof(KOCH_ORDER) - 1;

// ============================================================
// INSTELLINGEN EN PROFIEL
// ============================================================

struct TrainerSettings {
  int wpm = 18;
  int revealDelaySec = 2;
  int toneHz = 700;
  int sessionLength = 20;
  int promotionPercent = 90;
  int wordGapSec = 2;
  bool readyBeep = true;
};

struct StudentProfile {
  char name[16] = "Paul";
  int kochLevel = 1;        // niveau 1 = K en M
  uint32_t totalQuestions = 0;
  uint32_t totalCorrect = 0;
  uint32_t completedSessions = 0;
};

TrainerSettings settings;
StudentProfile student;

int dotMs = 1200 / 18;

// ============================================================
// APP-STATUS
// ============================================================

enum Screen {
  SCREEN_MAIN_MENU,
  SCREEN_SETTINGS,
  SCREEN_SPEED_WPM,
  SCREEN_STATISTICS,
  SCREEN_KOCH_SELECT,
  SCREEN_SESSION_MENU,
  SCREEN_FREE_KEYING,
  SCREEN_WEB_SELECT,
  SCREEN_WEB_STATUS,
  SCREEN_TRAINING
};

enum TrainingMode {
  MODE_FREE,
  MODE_KOCH
};

enum TrainingState {
  TRAIN_NEW_QUESTION,
  TRAIN_PREPARE_PLAYBACK,
  TRAIN_PLAYBACK,
  TRAIN_REVEAL_DELAY,
  TRAIN_READY_BEEP,
  TRAIN_WAIT_INPUT,
  TRAIN_CHECK_INPUT,
  TRAIN_SHOW_RESULT,
  TRAIN_SESSION_COMPLETE
};

Screen currentScreen = SCREEN_MAIN_MENU;
TrainingMode trainingMode = MODE_FREE;
TrainingState trainingState = TRAIN_NEW_QUESTION;

const char* mainMenuItems[] = {
  "Vrij oefenen",
  "Koch training",
  "Vrij seinen",
  "Statistieken",
  "Instellingen",
  "Webserver"
};
const int MAIN_MENU_COUNT = 6;

const char* settingItems[] = {
  "WPM",
  "Toonhoogte",
  "Toon vertraging",
  "Sessie lengte",
  "Promotiedrempel",
  "Startsignaal",
  "Terug"
};
const int SETTINGS_COUNT = 7;

int menuIndex = 0;
int settingsIndex = 0;
int selectedKochLesson = 1;       // les 1 bevat K en M
int sessionMenuIndex = 0;
bool editingSetting = false;
bool sessionPassed = false;

bool lastEncoderButton = HIGH;
unsigned long encoderPressedAt = 0;
bool longPressHandled = false;
unsigned long lastButtonEdgeAt = 0;

long lastEncoderRaw = 0;
int encoderPulseAccumulator = 0;
const int ENCODER_PULSES_PER_STEP = 2;  // attachHalfQuad: meestal 2 pulsen per klik
unsigned long lastEncoderStepAt = 0;
const unsigned long ENCODER_STEP_GUARD_MS = 8;

// ============================================================
// TRAININGSSTATUS
// ============================================================

int currentMorseIndex = 0;
String userMorse = "";

int sessionQuestions = 0;
int sessionCorrect = 0;

unsigned long trainingTimer = 0;
unsigned long lastPaddleActivity = 0;
bool lastAnswerCorrect = false;
String decodedText = "";
bool wordSpaceInserted = true;
bool wordGapSavePending = false;
unsigned long wordGapChangedAt = 0;

int webModeIndex = 0;
bool webRunning = false;
bool webFallbackAP = false;
bool webRoutesConfigured = false;
bool webConnectedShown = false;
unsigned long wifiConnectStartedAt = 0;
String wifiSSID = "";
String wifiPassword = "";
const char* LOCAL_AP_SSID = "ON5MB-Morse";
const char* LOCAL_AP_PASSWORD = "ON5MB2026";

// ============================================================
// MORSE-AFSPELER
// ============================================================

const char* playbackCode = nullptr;
int playbackPosition = 0;

enum PlaybackPhase {
  PLAY_IDLE,
  PLAY_TONE,
  PLAY_GAP
};

PlaybackPhase playbackPhase = PLAY_IDLE;
unsigned long playbackTimer = 0;

// ============================================================
// PADDLE-KEYER
// ============================================================

enum KeyerPhase {
  KEYER_IDLE,
  KEYER_TONE,
  KEYER_GAP
};

KeyerPhase keyerPhase = KEYER_IDLE;

// Straight-key state. The straight key is decoded by measuring how long
// the contact remains closed: shorter than 2 dot lengths = dit,
// 2 dot lengths or longer = dah.
bool straightKeyDown = false;
unsigned long straightKeyPressedAt = 0;
char keyerElement = '\0';
unsigned long keyerTimer = 0;

/*
  Belangrijk:
  - één korte tik geeft exact één element;
  - vasthouden herhaalt;
  - pas na loslaten kan een nieuwe losse tik starten;
  - geen paddle-memory die onbedoeld een tweede element veroorzaakt.

  Iambic A/B voegen we later als afzonderlijke instelbare keyermodule toe.
*/

// ============================================================
// DISPLAY-HULPFUNCTIES
// ============================================================

void centeredText(const String& text, int y, int size, uint16_t color,
  uint16_t background = C_PANEL) {
  int16_t x1, y1;
  uint16_t w, h;

  tft.setTextSize(size);
  tft.setTextColor(color, background);
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((tft.width() - w) / 2, y);
  tft.print(text);
}

void drawTopBar(const String& title) {
  tft.fillRect(0, 0, 320, 36, C_PANEL);
  tft.drawFastHLine(0, 36, 320, C_ORANGE);

  tft.setTextSize(2);
  tft.setTextColor(C_ORANGE, C_PANEL);
  tft.setCursor(8, 9);
  tft.print(title);
}

void drawFooter(const String& text) {
  tft.fillRect(0, 218, 320, 22, C_PANEL);
  tft.drawFastHLine(0, 217, 320, C_DARKGREY);
  centeredText(text, 224, 1, C_GREY, C_PANEL);
}

void clearContent() {
  tft.fillRect(0, 37, 320, 180, C_BG);
}

void drawMenuLine(int y, const String& text, bool selected) {
  uint16_t bg = selected ? C_ORANGE : C_BG;
  uint16_t fg = selected ? C_BG : C_WHITE;

  tft.fillRoundRect(22, y, 276, 32, 5, bg);
  tft.setTextSize(2);
  tft.setTextColor(fg, bg);
  tft.setCursor(36, y + 8);
  tft.print(selected ? "> " : "  ");
  tft.print(text);
}

void drawMainMenuLine(int index, bool selected) {
  int y = 40 + index * 29;
  uint16_t bg = selected ? C_ORANGE : C_BG;
  uint16_t fg = selected ? C_BG : C_WHITE;

  tft.fillRoundRect(22, y, 276, 26, 4, bg);
  tft.setTextSize(2);
  tft.setTextColor(fg, bg);
  tft.setCursor(36, y + 5);
  tft.print(selected ? "> " : "  ");
  tft.print(mainMenuItems[index]);
}

void drawSplash() {
  tft.drawRect(0, 0, 320, 240, C_ORANGE);
  tft.drawRect(4, 4, 312, 232, C_DARKGREY);

  centeredText("ON5MB", 112, 4, C_ORANGE);
  centeredText("V1.0", 178, 2, C_AMBER);

  delay(1000);
}

// ============================================================
// SCHERMEN
// ============================================================

void redrawMainMenuSelection(int oldIndex, int newIndex) {
  if (oldIndex >= 0 && oldIndex < MAIN_MENU_COUNT) {
    drawMainMenuLine(oldIndex, false);
  }
  if (newIndex >= 0 && newIndex < MAIN_MENU_COUNT) {
    drawMainMenuLine(newIndex, true);
  }
}

void drawSettingLine(int index, bool selected) {
  int y = 40 + index * 25;
  uint8_t bg = selected ? C_ORANGE : C_BG;
  uint8_t fg = selected ? C_BG : C_WHITE;

  // Wis alleen deze ene regel, niet het volledige scherm.
  tft.fillRoundRect(12, y, 296, 22, 4, bg);
  tft.setTextSize(1);
  tft.setTextColor(fg, bg);
  tft.setCursor(22, y + 7);
  tft.print(selected ? "> " : "  ");
  tft.print(settingItems[index]);

  if (index < SETTINGS_COUNT - 1) {
    // Waardegebied altijd volledig overschrijven.
    tft.fillRect(220, y + 2, 82, 18, bg);
    tft.setTextColor(fg, bg);
    tft.setCursor(224, y + 7);

    switch (index) {
      case 0:
        tft.print(settings.wpm);
        tft.print(" WPM");
        break;
      case 1:
        tft.print(settings.toneHz);
        tft.print(" Hz");
        break;
      case 2:
        tft.print(settings.revealDelaySec);
        tft.print(" s");
        break;
      case 3:
        tft.print(settings.sessionLength);
        break;
      case 4:
        tft.print(settings.promotionPercent);
        tft.print("%");
        break;
      case 5:
        tft.print(settings.readyBeep ? "Aan" : "Uit");
        break;
    }
  }
}

void redrawSettingsSelection(int oldIndex, int newIndex) {
  if (oldIndex >= 0 && oldIndex < SETTINGS_COUNT) {
    drawSettingLine(oldIndex, false);
  }
  if (newIndex >= 0 && newIndex < SETTINGS_COUNT) {
    drawSettingLine(newIndex, true);
  }
}

const char* sessionMenuLabel(int index) {
  static const char* labelsPassed[] = {
    "Volgende les",
    "Dezelfde les",
    "Andere les kiezen",
    "Hoofdmenu"
  };
  static const char* labelsFailed[] = {
    "Dezelfde les",
    "Andere les kiezen",
    "Hoofdmenu"
  };

  bool hasNext = sessionPassed && selectedKochLesson < KOCH_CHAR_COUNT - 1;
  return hasNext ? labelsPassed[index] : labelsFailed[index];
}

void redrawSessionMenuSelection(int oldIndex, int newIndex) {
  if (oldIndex >= 0) {
    drawMenuLine(88 + oldIndex * 31, sessionMenuLabel(oldIndex), false);
  }
  if (newIndex >= 0) {
    drawMenuLine(88 + newIndex * 31, sessionMenuLabel(newIndex), true);
  }
}

void redrawKochLessonContent() {
  // Alleen het veranderlijke middengebied vernieuwen.
  tft.fillRect(0, 39, 320, 178, C_BG);

  centeredText("LES " + String(selectedKochLesson), 51, 4, C_ORANGE);

  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  tft.setCursor(18, 105);
  tft.print("Beschikbare tekens:");

  String chars = kochCharactersForLesson(selectedKochLesson);
  int split = chars.length() > 32 ? 32 : chars.length();
  if (split < chars.length()) {
    while (split > 0 && chars[split] != ' ') split--;
  }

  centeredText(chars.substring(0, split), 128, 2, C_WHITE);
  if (split < chars.length()) {
    centeredText(chars.substring(split + 1), 156, 2, C_WHITE);
  }

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BG);
  tft.setCursor(18, 193);
  tft.print("Hoogste bereikte les: ");
  tft.print(student.kochLevel);
}

void drawMainMenu() {
  currentScreen = SCREEN_MAIN_MENU;
  tft.fillScreen(C_BG);
  drawTopBar("CW STUDIO ON5MB");

  for (int i = 0; i < MAIN_MENU_COUNT; i++) {
    drawMainMenuLine(i, i == menuIndex);
  }

  drawFooter("Draai = kiezen | Druk = openen");
}

void drawFreeKeying() {
  currentScreen = SCREEN_FREE_KEYING;
  tft.fillScreen(C_BG);
  drawTopBar("VRIJ SEINEN");
  centeredText("SEIN EEN TEKEN", 48, 2, C_AMBER);
  centeredText(userMorse.length() ? userMorse : "...", 84, 4,
               userMorse.length() ? C_CYAN : C_DARKGREY);
  tft.drawFastHLine(20, 132, 280, C_DARKGREY);
  centeredText(decodedText.length() ? decodedText : "Uitvoer verschijnt hier",
               151, decodedText.length() ? 3 : 1,
               decodedText.length() ? C_GREEN : C_GREY);
  centeredText("Woordpauze: " + String(settings.wordGapSec) + " s",
               198, 1, C_AMBER);
  drawFooter("Draai = pauze | Druk = wissen | Lang = terug");
}

void redrawFreeKeyingInput() {
  tft.fillRect(0, 78, 320, 48, C_BG);
  centeredText(userMorse.length() ? userMorse : "...", 84, 4,
               userMorse.length() ? C_CYAN : C_DARKGREY);
}

void redrawDecodedText() {
  tft.fillRect(0, 140, 320, 70, C_BG);
  centeredText(decodedText.length() ? decodedText : "Uitvoer verschijnt hier",
               151, decodedText.length() ? 3 : 1,
               decodedText.length() ? C_GREEN : C_GREY);
  centeredText("Woordpauze: " + String(settings.wordGapSec) + " s",
               198, 1, C_AMBER);
}

void redrawWordGapSetting() {
  tft.fillRect(0, 192, 320, 20, C_BG);
  centeredText("Woordpauze: " + String(settings.wordGapSec) + " s",
               198, 1, C_AMBER);
}

void drawWebSelect() {
  currentScreen = SCREEN_WEB_SELECT;
  tft.fillScreen(C_BG);
  drawTopBar("WEBSERVER");
  centeredText("KIES VERBINDING", 48, 2, C_AMBER);
  drawMenuLine(82, "Eigen netwerk", webModeIndex == 0);
  drawMenuLine(124, "Thuis-wifi", webModeIndex == 1);
  drawFooter("Draai = kiezen | Druk = starten");
}

void redrawWebSelect() {
  drawMenuLine(82, "Eigen netwerk", webModeIndex == 0);
  drawMenuLine(124, "Thuis-wifi", webModeIndex == 1);
}

void drawWebStatus() {
  currentScreen = SCREEN_WEB_STATUS;
  tft.fillScreen(C_BG);
  drawTopBar("WEBSERVER ACTIEF");

  if (WiFi.getMode() == WIFI_AP || webFallbackAP) {
    centeredText("Eigen wifi-netwerk", 52, 2, C_AMBER);
    centeredText(LOCAL_AP_SSID, 84, 2, C_WHITE);
    centeredText("Wachtwoord: " + String(LOCAL_AP_PASSWORD), 113, 1, C_GREY);
    centeredText("http://192.168.4.1", 143, 2, C_GREEN);
  } else if (WiFi.status() == WL_CONNECTED) {
    centeredText("Verbonden met thuis-wifi", 52, 2, C_GREEN);
    centeredText(wifiSSID, 84, 2, C_WHITE);
    centeredText("http://" + WiFi.localIP().toString(), 126, 2, C_GREEN);
  } else {
    centeredText("Verbinden met thuis-wifi...", 66, 2, C_AMBER);
    centeredText(wifiSSID, 105, 2, C_WHITE);
    centeredText("Maximaal 15 seconden", 150, 1, C_GREY);
  }

  drawFooter("Druk = stoppen | Lang = hoofdmenu");
}

void drawSettings() {
  currentScreen = SCREEN_SETTINGS;
  tft.fillScreen(C_BG);
  drawTopBar("INSTELLINGEN");

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    drawSettingLine(i, i == settingsIndex);
  }

  drawFooter(editingSetting
    ? "Draai = wijzigen | Druk = bevestigen"
    : "Draai = kiezen | Druk = openen");
}

void drawStatistics() {
  currentScreen = SCREEN_STATISTICS;
  tft.fillScreen(C_BG);
  drawTopBar("STATISTIEKEN");

  tft.setTextSize(2);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.setCursor(18, 50);
  tft.print(student.name);

  uint32_t accuracy = student.totalQuestions == 0
    ? 0
    : (student.totalCorrect * 100UL) / student.totalQuestions;

  tft.setTextSize(2);
  tft.setTextColor(C_WHITE, C_BG);

  tft.setCursor(18, 85);
  tft.print("Koch niveau:");
  tft.setCursor(230, 85);
  tft.print(student.kochLevel);

  tft.setCursor(18, 115);
  tft.print("Vragen:");
  tft.setCursor(230, 115);
  tft.print(student.totalQuestions);

  tft.setCursor(18, 145);
  tft.print("Juist:");
  tft.setCursor(230, 145);
  tft.print(student.totalCorrect);

  tft.setCursor(18, 175);
  tft.print("Score:");
  tft.setCursor(230, 175);
  tft.setTextColor(accuracy >= 90 ? C_GREEN : C_AMBER, C_BG);
  tft.print(accuracy);
  tft.print("%");

  drawFooter("Druk = terug");
}

void drawTrainingHeader() {
  tft.fillRect(0, 0, 320, 38, C_PANEL);
  tft.drawFastHLine(0, 38, 320, C_ORANGE);

  tft.setTextSize(1);
  tft.setTextColor(C_ORANGE, C_PANEL);
  tft.setCursor(8, 7);
  tft.print(trainingMode == MODE_KOCH ? "KOCH TRAINING" : "VRIJ OEFENEN");

  tft.setTextColor(C_WHITE, C_PANEL);
  tft.setCursor(8, 23);
  tft.print("WPM ");
  tft.print(settings.wpm);

  if (trainingMode == MODE_KOCH) {
    tft.setCursor(82, 23);
    tft.print("LES ");
    tft.print(selectedKochLesson);
  }

  tft.setCursor(164, 23);
  tft.print("SCORE ");
  tft.print(sessionCorrect);
  tft.print("/");
  tft.print(sessionQuestions);

  tft.setCursor(265, 23);
  tft.print("/");
  tft.print(settings.sessionLength);
}

void drawListening() {
  tft.fillScreen(C_BG);
  drawTrainingHeader();

  centeredText("LUISTER", 65, 2, C_AMBER);
  centeredText("?", 97, 7, C_ORANGE);

  if (trainingMode == MODE_KOCH) {
    String available = "Tekens: ";
    int count = min(selectedKochLesson + 1, KOCH_CHAR_COUNT);

    for (int i = 0; i < count && i < 12; i++) {
      available += KOCH_ORDER[i];
      available += " ";
    }

    centeredText(available, 198, 1, C_GREY);
  }

  drawFooter("Lang drukken = sessie stoppen");
}

void drawAnswerPrompt() {
  tft.fillScreen(C_BG);
  drawTrainingHeader();

  centeredText("SEIN HET TEKEN", 48, 2, C_AMBER);

  String symbolText = String(morseTable[currentMorseIndex].symbol);
  centeredText(symbolText, 78, 7, C_ORANGE);
  centeredText(String(morseTable[currentMorseIndex].code), 151, 3, C_WHITE);

  tft.drawFastHLine(25, 191, 270, C_DARKGREY);

  if (userMorse.length() == 0) {
    centeredText("Paddle: DIT / DAH", 201, 1, C_GREY);
  } else {
    centeredText(userMorse, 196, 3, C_CYAN);
  }
}

void drawPaddleInput() {
  tft.fillRect(0, 194, 320, 23, C_BG);

  if (userMorse.length() == 0) {
    centeredText("Paddle: DIT / DAH", 201, 1, C_GREY);
  } else {
    centeredText(userMorse, 196, 3, C_CYAN);
  }
}

void drawTrainingResult(bool correct) {
  tft.fillRect(0, 185, 320, 32, C_BG);
  centeredText(correct ? "GOED!" : "FOUT!",
               190, 3, correct ? C_GREEN : C_RED);
}

String kochCharactersForLesson(int lesson) {
  int count = constrain(lesson + 1, 2, KOCH_CHAR_COUNT);
  String result = "";
  for (int i = 0; i < count; i++) {
    result += KOCH_ORDER[i];
    if (i < count - 1) result += " ";
  }
  return result;
}

void drawKochSelect() {
  currentScreen = SCREEN_KOCH_SELECT;
  tft.fillScreen(C_BG);
  drawTopBar("KIES KOCH-LES");

  centeredText("LES " + String(selectedKochLesson), 51, 4, C_ORANGE);

  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  tft.setCursor(18, 105);
  tft.print("Beschikbare tekens:");

  String chars = kochCharactersForLesson(selectedKochLesson);
  int split = chars.length() > 32 ? 32 : chars.length();
  if (split < chars.length()) {
    while (split > 0 && chars[split] != ' ') split--;
  }

  centeredText(chars.substring(0, split), 128, 2, C_WHITE);
  if (split < chars.length()) {
    centeredText(chars.substring(split + 1), 156, 2, C_WHITE);
  }

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BG);
  tft.setCursor(18, 193);
  tft.print("Hoogste bereikte les: ");
  tft.print(student.kochLevel);

  drawFooter("Draai = les kiezen | Druk = starten");
}

void drawSessionMenu() {
  currentScreen = SCREEN_SESSION_MENU;
  tft.fillScreen(C_BG);
  drawTopBar("SESSIE KLAAR");

  int percent = sessionQuestions == 0 ? 0 : (sessionCorrect * 100) / sessionQuestions;
  centeredText(String(percent) + "%", 43, 4,
               sessionPassed ? C_GREEN : C_AMBER);

  int count = sessionPassed && selectedKochLesson < KOCH_CHAR_COUNT - 1 ? 4 : 3;
  for (int i = 0; i < count; i++) {
    drawMenuLine(88 + i * 31, sessionMenuLabel(i), i == sessionMenuIndex);
  }

  drawFooter("Draai = kiezen | Druk = bevestigen");
}

void drawSessionComplete() {
  tft.fillScreen(C_BG);
  drawTopBar("SESSIE KLAAR");

  int percent = sessionQuestions == 0
    ? 0
    : (sessionCorrect * 100) / sessionQuestions;

  centeredText(String(percent) + "%", 62, 6,
               percent >= settings.promotionPercent ? C_GREEN : C_AMBER);
  centeredText(String(sessionCorrect) + " van " + String(sessionQuestions) + " juist",
               132, 2, C_WHITE);
  drawFooter("Druk = hoofdmenu");
}

// ============================================================
// OPSLAG
// ============================================================

void loadData() {
  prefs.begin("cwtrainer", false);

  settings.wpm = constrain(prefs.getInt("wpm", 18), 5, 40);
  settings.toneHz = constrain(prefs.getInt("tone", 700), 300, 1200);
  settings.revealDelaySec = constrain(prefs.getInt("delay", 2), 0, 10);
  settings.sessionLength = constrain(prefs.getInt("sesslen", 20), 5, 100);
  settings.promotionPercent = constrain(prefs.getInt("promote", 90), 70, 100);
  settings.wordGapSec = constrain(prefs.getInt("wordgap", 2), 1, 10);
  settings.readyBeep = prefs.getBool("readybeep", true);
  wifiSSID = prefs.getString("wifi_ssid", "");
  wifiPassword = prefs.getString("wifi_pass", "");

  student.kochLevel =
    constrain(prefs.getInt("koch", 1), 1, KOCH_CHAR_COUNT - 1);
  selectedKochLesson = constrain(prefs.getInt("selected", student.kochLevel), 1, KOCH_CHAR_COUNT - 1);
  student.totalQuestions = prefs.getULong("totalq", 0);
  student.totalCorrect = prefs.getULong("totalok", 0);
  student.completedSessions = prefs.getULong("sessions", 0);

  String savedName = prefs.getString("name", "Bernard");
  savedName.toCharArray(student.name, sizeof(student.name));

  dotMs = 1200 / settings.wpm;
}

void saveSettings() {
  prefs.putInt("wpm", settings.wpm);
  prefs.putInt("tone", settings.toneHz);
  prefs.putInt("delay", settings.revealDelaySec);
  prefs.putInt("sesslen", settings.sessionLength);
  prefs.putInt("promote", settings.promotionPercent);
  prefs.putInt("wordgap", settings.wordGapSec);
  prefs.putBool("readybeep", settings.readyBeep);
}

void saveStudent() {
  prefs.putInt("koch", student.kochLevel);
  prefs.putInt("selected", selectedKochLesson);
  prefs.putULong("totalq", student.totalQuestions);
  prefs.putULong("totalok", student.totalCorrect);
  prefs.putULong("sessions", student.completedSessions);
  prefs.putString("name", student.name);
}

String htmlEscape(const String& value) {
  String result = value;
  result.replace("&", "&amp;");
  result.replace("\"", "&quot;");
  result.replace("<", "&lt;");
  result.replace(">", "&gt;");
  return result;
}

String buildWebPage(const String& notice = "") {
  uint32_t accuracy = student.totalQuestions == 0
    ? 0
    : (student.totalCorrect * 100UL) / student.totalQuestions;

  String page;
  page.reserve(5500);
  page += F("<!doctype html><html lang='nl'><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>ON5MB Morse Trainer</title><style>");
  page += F("body{font-family:Arial;background:#111;color:#eee;max-width:720px;margin:auto;padding:18px}");
  page += F("h1,h2{color:#ff9d00}.card{background:#222;padding:16px;margin:14px 0;border-radius:10px}");
  page += F("label{display:block;margin-top:10px}input,select{width:100%;box-sizing:border-box;padding:9px}");
  page += F("button{margin-top:15px;padding:11px 18px;background:#ff9d00;border:0;border-radius:6px}");
  page += F(".ok{color:#57e389}.small{color:#aaa;font-size:.9em}</style></head><body>");
  page += F("<h1>ON5MB Morse Trainer</h1>");
  if (notice.length()) {
    page += "<p class='ok'>" + htmlEscape(notice) + "</p>";
  }

  page += F("<div class='card'><h2>Statistieken</h2>");
  page += "Cursist: " + htmlEscape(String(student.name)) + "<br>";
  page += "Koch-les: " + String(student.kochLevel) + "<br>";
  page += "Vragen: " + String(student.totalQuestions) + "<br>";
  page += "Juist: " + String(student.totalCorrect) + "<br>";
  page += "Score: " + String(accuracy) + "%<br>";
  page += "Sessies: " + String(student.completedSessions);
  page += F("</div>");

  String webKochChars = String(KOCH_ORDER).substring(
    0, min(student.kochLevel + 1, KOCH_CHAR_COUNT));
  page += F("<div class='card'><h2>Train via deze website</h2>");
  page += F("<label>Training<select id='trainMode'><option value='free'>Vrij oefenen A-Z</option>");
  page += "<option value='koch'>Koch-les " + String(student.kochLevel) +
          " (" + htmlEscape(webKochChars) + ")</option></select></label>";
  page += F("<button type='button' id='startTrain'>Start training</button>");
  page += F("<div id='trainer' style='display:none;margin-top:16px'>");
  page += F("<p id='progress'>Vraag 0</p><p id='trainStatus' class='ok'>Luister...</p>");
  page += F("<label>Welk teken hoorde je?<input id='answer' maxlength='1' autocomplete='off' autocapitalize='characters'></label>");
  page += F("<button type='button' id='answerBtn'>Controleer</button> ");
  page += F("<button type='button' id='replayBtn'>Opnieuw afspelen</button></div></div>");

  page += F("<script>");
  page += F("const morse={A:'.-',B:'-...',C:'-.-.',D:'-..',E:'.',F:'..-.',G:'--.',H:'....',I:'..',J:'.---',K:'-.-',L:'.-..',M:'--',N:'-.',O:'---',P:'.--.',Q:'--.-',R:'.-.',S:'...',T:'-',U:'..-',V:'...-',W:'.--',X:'-..-',Y:'-.--',Z:'--..','1':'.----','2':'..---','3':'...--','4':'....-','5':'.....','6':'-....','7':'--...','8':'---..','9':'----.','0':'-----','.':'.-.-.-',',':'--..--','?':'..--..','/':'-..-.'};");
  page += "const WPM=" + String(settings.wpm) +
          ",TONE=" + String(settings.toneHz) +
          ",LENGTH=" + String(settings.sessionLength) +
          ",KOCH='" + webKochChars + "';";
  page += F("let ctx,target='',questions=0,correct=0,active=false,chars='ABCDEFGHIJKLMNOPQRSTUVWXYZ';");
  page += F("function playChar(c){if(!ctx)ctx=new(window.AudioContext||window.webkitAudioContext)();ctx.resume();let t=ctx.currentTime+.08,dot=1.2/WPM;for(const e of morse[c]){let d=e==='.'?dot:dot*3,o=ctx.createOscillator(),g=ctx.createGain();o.frequency.value=TONE;g.gain.value=.16;o.connect(g);g.connect(ctx.destination);o.start(t);o.stop(t+d);t+=d+dot;}}");
  page += F("function nextQuestion(){if(questions>=LENGTH){finishSession();return;}target=chars[Math.floor(Math.random()*chars.length)];document.getElementById('answer').value='';document.getElementById('progress').textContent='Vraag '+(questions+1)+' van '+LENGTH+' | Score '+correct+'/'+questions;document.getElementById('trainStatus').textContent='Luister...';playChar(target);setTimeout(()=>document.getElementById('answer').focus(),250);}");
  page += F("function checkAnswer(){if(!active)return;let a=document.getElementById('answer').value.trim().toUpperCase();if(!a)return;questions++;if(a===target){correct++;document.getElementById('trainStatus').textContent='GOED! '+target+' = '+morse[target];}else{document.getElementById('trainStatus').textContent='FOUT: het was '+target+' = '+morse[target];}setTimeout(nextQuestion,850);}");
  page += F("async function finishSession(){active=false;let pct=questions?Math.round(correct*100/questions):0;document.getElementById('trainStatus').textContent='Sessie klaar: '+correct+'/'+questions+' ('+pct+'%)';let b=new URLSearchParams({correct:String(correct),questions:String(questions),mode:document.getElementById('trainMode').value});await fetch('/web-result',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});}");
  page += F("document.getElementById('startTrain').onclick=()=>{questions=0;correct=0;active=true;chars=document.getElementById('trainMode').value==='koch'?KOCH:'ABCDEFGHIJKLMNOPQRSTUVWXYZ';document.getElementById('trainer').style.display='block';nextQuestion();};");
  page += F("document.getElementById('answerBtn').onclick=checkAnswer;document.getElementById('replayBtn').onclick=()=>active&&playChar(target);document.getElementById('answer').onkeydown=e=>{if(e.key==='Enter')checkAnswer();};");
  page += F("</script>");

  page += F("<form class='card' method='post' action='/save'><h2>Trainer</h2>");
  page += "<label>Naam<input name='name' maxlength='15' value=\"" +
          htmlEscape(String(student.name)) + "\"></label>";
  page += "<label>WPM<input type='number' name='wpm' min='5' max='40' value='" +
          String(settings.wpm) + "'></label>";
  page += "<label>Toonhoogte (Hz)<input type='number' name='tone' min='300' max='1200' value='" +
          String(settings.toneHz) + "'></label>";
  page += "<label>Toonvertraging (s)<input type='number' name='delay' min='0' max='10' value='" +
          String(settings.revealDelaySec) + "'></label>";
  page += "<label>Sessielengte<input type='number' name='sesslen' min='5' max='100' value='" +
          String(settings.sessionLength) + "'></label>";
  page += "<label>Promotiedrempel (%)<input type='number' name='promote' min='70' max='100' value='" +
          String(settings.promotionPercent) + "'></label>";
  page += "<label>Woordpauze (s)<input type='number' name='wordgap' min='1' max='10' value='" +
          String(settings.wordGapSec) + "'></label>";
  page += "<label>Koch-les<input type='number' name='koch' min='1' max='" +
          String(KOCH_CHAR_COUNT - 1) + "' value='" + String(student.kochLevel) + "'></label>";

  page += F("<h2>Thuis-wifi</h2>");
  page += "<label>Wifi-naam (SSID)<input name='ssid' maxlength='32' value=\"" +
          htmlEscape(wifiSSID) + "\"></label>";
  page += F("<label>Nieuw wachtwoord<input type='password' name='pass' maxlength='63'></label>");
  page += F("<p class='small'>Laat het wachtwoord leeg om het bestaande wachtwoord te behouden.</p>");
  page += F("<button type='submit'>Alles lokaal opslaan</button></form>");

  page += F("<form class='card' method='post' action='/reset' onsubmit=\"return confirm('Statistieken wissen?')\">");
  page += F("<h2>Statistieken wissen</h2><button type='submit'>Reset statistieken</button></form>");
  page += F("<p class='small'>Alle gegevens blijven uitsluitend in deze ESP32 opgeslagen.</p>");
  page += F("</body></html>");
  return page;
}

void sendWebPage(const String& notice = "") {
  webServer.send(200, "text/html; charset=utf-8", buildWebPage(notice));
}

void configureWebRoutes() {
  if (webRoutesConfigured) return;

  webServer.on("/", HTTP_GET, []() {
    sendWebPage();
  });

  webServer.on("/save", HTTP_POST, []() {
    if (webServer.hasArg("name")) {
      String newName = webServer.arg("name");
      newName.trim();
      if (newName.length()) newName.toCharArray(student.name, sizeof(student.name));
    }
    if (webServer.hasArg("wpm")) settings.wpm = constrain(webServer.arg("wpm").toInt(), 5, 40);
    if (webServer.hasArg("tone")) settings.toneHz = constrain(webServer.arg("tone").toInt(), 300, 1200);
    if (webServer.hasArg("delay")) settings.revealDelaySec = constrain(webServer.arg("delay").toInt(), 0, 10);
    if (webServer.hasArg("sesslen")) settings.sessionLength = constrain(webServer.arg("sesslen").toInt(), 5, 100);
    if (webServer.hasArg("promote")) settings.promotionPercent = constrain(webServer.arg("promote").toInt(), 70, 100);
    if (webServer.hasArg("wordgap")) settings.wordGapSec = constrain(webServer.arg("wordgap").toInt(), 1, 10);
    if (webServer.hasArg("koch")) {
      student.kochLevel = constrain(webServer.arg("koch").toInt(), 1, KOCH_CHAR_COUNT - 1);
      selectedKochLesson = student.kochLevel;
    }
    if (webServer.hasArg("ssid")) {
      wifiSSID = webServer.arg("ssid");
      wifiSSID.trim();
      prefs.putString("wifi_ssid", wifiSSID);
    }
    if (webServer.hasArg("pass") && webServer.arg("pass").length()) {
      wifiPassword = webServer.arg("pass");
      prefs.putString("wifi_pass", wifiPassword);
    }
    dotMs = 1200 / settings.wpm;
    saveSettings();
    saveStudent();
    sendWebPage("Instellingen opgeslagen.");
  });

  webServer.on("/web-result", HTTP_POST, []() {
    uint32_t questions = constrain(webServer.arg("questions").toInt(), 0, 100);
    uint32_t correct = constrain(webServer.arg("correct").toInt(), 0, (int)questions);

    student.totalQuestions += questions;
    student.totalCorrect += correct;
    student.completedSessions++;

    int percent = questions == 0 ? 0 : (correct * 100UL) / questions;
    if (webServer.arg("mode") == "koch" &&
        percent >= settings.promotionPercent &&
        student.kochLevel < KOCH_CHAR_COUNT - 1) {
      student.kochLevel++;
      selectedKochLesson = student.kochLevel;
    }

    saveStudent();
    String response = "{\"saved\":true,\"percent\":" + String(percent) +
                      ",\"kochLevel\":" + String(student.kochLevel) + "}";
    webServer.send(200, "application/json", response);
  });

  webServer.on("/reset", HTTP_POST, []() {
    student.totalQuestions = 0;
    student.totalCorrect = 0;
    student.completedSessions = 0;
    saveStudent();
    sendWebPage("Statistieken gewist.");
  });

  webServer.onNotFound([]() {
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
  });

  webRoutesConfigured = true;
}

void startLocalAccessPoint() {
  if (webRunning) webServer.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(LOCAL_AP_SSID, LOCAL_AP_PASSWORD);
  webFallbackAP = true;
  webConnectedShown = true;
  webServer.begin();
  webRunning = true;
  drawWebStatus();
}

void startSelectedWebServer() {
  configureWebRoutes();
  webFallbackAP = false;
  webConnectedShown = false;

  if (webModeIndex == 0 || wifiSSID.length() == 0) {
    startLocalAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  wifiConnectStartedAt = millis();
  webServer.begin();
  webRunning = true;
  drawWebStatus();
}

void stopWebServer() {
  if (!webRunning) return;
  webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  webRunning = false;
  webFallbackAP = false;
  webConnectedShown = false;
}

void updateWebServer() {
  if (!webRunning) return;
  webServer.handleClient();

  if (currentScreen == SCREEN_WEB_STATUS &&
      WiFi.getMode() == WIFI_STA) {
    if (WiFi.status() == WL_CONNECTED && !webConnectedShown) {
      webConnectedShown = true;
      drawWebStatus();
    } else if (millis() - wifiConnectStartedAt >= 15000UL) {
      startLocalAccessPoint();
    }
  }
}

// ============================================================
// AUDIO
// ============================================================

void toneOn() {
  tone(BUZZER_PIN, settings.toneHz);
}

void toneOff() {
  noTone(BUZZER_PIN);
}

void resultSound(bool correct) {
  if (correct) {
    tone(BUZZER_PIN, 1000, 80);
  } else {
    tone(BUZZER_PIN, 300, 180);
  }
}

// ============================================================
// MORSE-ZOEKFUNCTIES
// ============================================================

int findMorseIndex(char symbol) {
  for (int i = 0; i < MORSE_COUNT; i++) {
    if (morseTable[i].symbol == symbol) {
      return i;
    }
  }
  return 0;
}

char decodeMorse(const String& code) {
  for (int i = 0; i < MORSE_COUNT; i++) {
    if (code == morseTable[i].code) {
      return morseTable[i].symbol;
    }
  }
  return '#';
}

int chooseQuestionIndex() {
  if (trainingMode == MODE_FREE) {
    // Vrij oefenen: voorlopig alle letters A-Z.
    return random(0, 26);
  }

  // Niveau 1 bevat twee tekens: K en M.
  int availableCount = min(selectedKochLesson + 1, KOCH_CHAR_COUNT);
  char symbol = KOCH_ORDER[random(0, availableCount)];
  return findMorseIndex(symbol);
}

// ============================================================
// MORSE-AFSPELER
// ============================================================

void startPlayback(const char* code) {
  playbackCode = code;
  playbackPosition = 0;
  playbackPhase = PLAY_IDLE;
  playbackTimer = millis();
}

bool updatePlayback() {
  if (playbackCode == nullptr) {
    return true;
  }

  unsigned long now = millis();

  if (playbackPhase == PLAY_IDLE) {
    if (playbackCode[playbackPosition] == '\0') {
      playbackCode = nullptr;
      toneOff();
      return true;
    }

    toneOn();
    playbackTimer = now;
    playbackPhase = PLAY_TONE;
    return false;
  }

  if (playbackPhase == PLAY_TONE) {
    unsigned long duration =
      playbackCode[playbackPosition] == '.' ? dotMs : dotMs * 3UL;

    if (now - playbackTimer >= duration) {
      toneOff();
      playbackTimer = now;
      playbackPhase = PLAY_GAP;
    }

    return false;
  }

  if (playbackPhase == PLAY_GAP) {
    if (now - playbackTimer >= (unsigned long)dotMs) {
      playbackPosition++;
      playbackPhase = PLAY_IDLE;
    }
  }

  return false;
}

// ============================================================
// PADDLE
// ============================================================

void resetKeyer() {
  keyerPhase = KEYER_IDLE;
  keyerElement = '\0';
  straightKeyDown = false;
  toneOff();
}

void appendPaddleElement(char element) {
  if (userMorse.length() < 8) {
    userMorse += element;
  }

  lastPaddleActivity = millis();
  wordSpaceInserted = false;
  if (currentScreen == SCREEN_FREE_KEYING) {
    redrawFreeKeyingInput();
  } else {
    drawPaddleInput();
  }
}

void updateKeyer() {
  unsigned long now = millis();

  bool dit = digitalRead(PADDLE_DIT) == LOW;
  bool dah = digitalRead(PADDLE_DAH) == LOW;

  switch (keyerPhase) {
    case KEYER_IDLE:
      if (dit && !dah) {
        keyerElement = '.';
        toneOn();
        keyerTimer = now;
        keyerPhase = KEYER_TONE;
      } else if (dah && !dit) {
        keyerElement = '-';
        toneOn();
        keyerTimer = now;
        keyerPhase = KEYER_TONE;
      }
      break;

    case KEYER_TONE: {
      unsigned long duration =
        keyerElement == '.' ? dotMs : dotMs * 3UL;

      if (now - keyerTimer >= duration) {
        toneOff();
        appendPaddleElement(keyerElement);
        keyerTimer = now;
        keyerPhase = KEYER_GAP;
      }
      break;
    }

    case KEYER_GAP:
      if (now - keyerTimer >= (unsigned long)dotMs) {
        bool samePaddleStillHeld =
          (keyerElement == '.' && dit && !dah) ||
          (keyerElement == '-' && dah && !dit);

        if (samePaddleStillHeld) {
          toneOn();
          keyerTimer = now;
          keyerPhase = KEYER_TONE;
        } else {
          // Meteen klaar voor het volgende element. Dit herstelt de
          // directe respons van de eerdere werkende paddleversie.
          keyerPhase = KEYER_IDLE;
        }
      }
      break;
  }
}


void updateStraightKey() {
  unsigned long now = millis();
  bool pressed = digitalRead(STRAIGHT_KEY) == LOW;

  // Do not start a straight-key element while the paddle keyer is
  // already generating an element.
  if (pressed && !straightKeyDown && keyerPhase == KEYER_IDLE) {
    straightKeyDown = true;
    straightKeyPressedAt = now;
    toneOn();
    return;
  }

  if (!pressed && straightKeyDown) {
    toneOff();

    unsigned long held = now - straightKeyPressedAt;
    char element = (held < (unsigned long)dotMs * 2UL) ? '.' : '-';

    appendPaddleElement(element);
    straightKeyDown = false;
  }
}

void updateManualKeyInput() {
  updateStraightKey();

  // Paddle remains active when the straight key is not being held.
  if (!straightKeyDown) {
    updateKeyer();
  }
}

void startFreeKeying() {
  userMorse = "";
  decodedText = "";
  resetKeyer();
  lastPaddleActivity = millis();
  wordSpaceInserted = true;
  drawFreeKeying();
}

void updateFreeKeying() {
  unsigned long now = millis();
  updateManualKeyInput();

  if (wordGapSavePending && now - wordGapChangedAt >= 800) {
    saveSettings();
    wordGapSavePending = false;
  }

  // Een standaard letterpauze duurt drie punten, gerekend vanaf het
  // einde van het laatste element. De ondergrens houdt de herkenning
  // bij hoge WPM nog praktisch en voorkomt voortijdig afbreken.
  unsigned long characterPause =
    max(180UL, (unsigned long)dotMs * 3UL);
  unsigned long requestedWordPause =
    (unsigned long)settings.wordGapSec * 1000UL;
  unsigned long wordPause =
    max(requestedWordPause, characterPause + 250UL);
  if (userMorse.length() > 0 &&
      keyerPhase == KEYER_IDLE &&
      now - lastPaddleActivity >= characterPause) {
    char decoded = decodeMorse(userMorse);
    if (decodedText.length() >= 10) {
      decodedText.remove(0, 1);
    }
    decodedText += decoded;
    userMorse = "";
    redrawFreeKeyingInput();
    redrawDecodedText();
    return;
  }

  // Langere stilte na een teken betekent: nieuw woord.
  if (userMorse.length() == 0 &&
      decodedText.length() > 0 &&
      !wordSpaceInserted &&
      keyerPhase == KEYER_IDLE &&
      now - lastPaddleActivity >= wordPause) {
    if (decodedText.length() >= 10) {
      decodedText.remove(0, 1);
    }
    decodedText += ' ';
    wordSpaceInserted = true;
    redrawDecodedText();
  }
}

// ============================================================
// ENCODER
// ============================================================

void resetEncoderTracking() {
  encoder.setCount(0);
  lastEncoderRaw = 0;
  encoderPulseAccumulator = 0;
}

void setEncoderForCurrentScreen() {
  // De hardwareteller blijft relatief. Menu- en instellingswaarden
  // worden niet meer rechtstreeks in de quadratuurteller geschreven.
  resetEncoderTracking();
}

void stopTrainingToMenu() {
  toneOff();
  playbackCode = nullptr;
  resetKeyer();
  saveStudent();

  menuIndex = trainingMode == MODE_FREE ? 0 : 1;
  drawMainMenu();
  setEncoderForCurrentScreen();
}

void startTraining(TrainingMode mode) {
  trainingMode = mode;
  currentScreen = SCREEN_TRAINING;
  trainingState = TRAIN_NEW_QUESTION;

  sessionQuestions = 0;
  sessionCorrect = 0;
  userMorse = "";

  toneOff();
  playbackCode = nullptr;
  resetKeyer();
}

void handleShortPress() {
  if (currentScreen == SCREEN_MAIN_MENU) {
    switch (menuIndex) {
      case 0:
        startTraining(MODE_FREE);
        break;

      case 1:
        selectedKochLesson = constrain(selectedKochLesson, 1, KOCH_CHAR_COUNT - 1);
        drawKochSelect();
        setEncoderForCurrentScreen();
        break;

      case 2:
        startFreeKeying();
        break;

      case 3:
        drawStatistics();
        break;

      case 4:
        settingsIndex = 0;
        editingSetting = false;
        drawSettings();
        setEncoderForCurrentScreen();
        break;

      case 5:
        webModeIndex = 0;
        drawWebSelect();
        setEncoderForCurrentScreen();
        break;
    }
  } else if (currentScreen == SCREEN_WEB_SELECT) {
    startSelectedWebServer();
    setEncoderForCurrentScreen();
  } else if (currentScreen == SCREEN_WEB_STATUS) {
    stopWebServer();
    drawWebSelect();
    setEncoderForCurrentScreen();
  } else if (currentScreen == SCREEN_KOCH_SELECT) {
    saveStudent();
    startTraining(MODE_KOCH);
  } else if (currentScreen == SCREEN_SESSION_MENU) {
    bool hasNext = sessionPassed && selectedKochLesson < KOCH_CHAR_COUNT - 1;

    if (hasNext) {
      if (sessionMenuIndex == 0) {
        selectedKochLesson++;
        if (selectedKochLesson > student.kochLevel) student.kochLevel = selectedKochLesson;
        saveStudent();
        startTraining(MODE_KOCH);
      } else if (sessionMenuIndex == 1) {
        startTraining(MODE_KOCH);
      } else if (sessionMenuIndex == 2) {
        drawKochSelect();
        setEncoderForCurrentScreen();
      } else {
        drawMainMenu();
        setEncoderForCurrentScreen();
      }
    } else {
      if (sessionMenuIndex == 0) {
        startTraining(MODE_KOCH);
      } else if (sessionMenuIndex == 1) {
        drawKochSelect();
        setEncoderForCurrentScreen();
      } else {
        drawMainMenu();
        setEncoderForCurrentScreen();
      }
    }
  } else if (currentScreen == SCREEN_FREE_KEYING) {
    userMorse = "";
    decodedText = "";
    wordSpaceInserted = true;
    resetKeyer();
    drawFreeKeying();
  } else if (currentScreen == SCREEN_STATISTICS) {
    drawMainMenu();
    setEncoderForCurrentScreen();
  } else if (currentScreen == SCREEN_SETTINGS) {
    if (editingSetting) {
      editingSetting = false;
      saveSettings();
      drawSettings();
      setEncoderForCurrentScreen();
    } else if (settingsIndex == SETTINGS_COUNT - 1) {
      drawMainMenu();
      setEncoderForCurrentScreen();
    } else {
      editingSetting = true;

      resetEncoderTracking();
      drawSettings();
    }
  } else if (currentScreen == SCREEN_TRAINING &&
             trainingState == TRAIN_SESSION_COMPLETE) {
    drawMainMenu();
    setEncoderForCurrentScreen();
  }
}

void applyEncoderStep(int direction) {
  if (direction == 0) return;

  if (currentScreen == SCREEN_MAIN_MENU) {
    int newIndex = constrain(menuIndex + direction, 0, MAIN_MENU_COUNT - 1);
    if (newIndex != menuIndex) {
      int oldIndex = menuIndex;
      menuIndex = newIndex;
      redrawMainMenuSelection(oldIndex, menuIndex);
    }
    return;
  }

  if (currentScreen == SCREEN_FREE_KEYING) {
    int newWordGap = constrain(settings.wordGapSec + direction, 1, 10);
    if (newWordGap != settings.wordGapSec) {
      settings.wordGapSec = newWordGap;
      wordGapChangedAt = millis();
      wordGapSavePending = true;
      redrawWordGapSetting();
    }
    return;
  }

  if (currentScreen == SCREEN_WEB_SELECT) {
    int newMode = constrain(webModeIndex + direction, 0, 1);
    if (newMode != webModeIndex) {
      webModeIndex = newMode;
      redrawWebSelect();
    }
    return;
  }

  if (currentScreen == SCREEN_KOCH_SELECT) {
    int newLesson = constrain(selectedKochLesson + direction, 1, KOCH_CHAR_COUNT - 1);
    if (newLesson != selectedKochLesson) {
      selectedKochLesson = newLesson;
      redrawKochLessonContent();
    }
    return;
  }

  if (currentScreen == SCREEN_SESSION_MENU) {
    int maxIndex = (sessionPassed && selectedKochLesson < KOCH_CHAR_COUNT - 1) ? 3 : 2;
    int newIndex = constrain(sessionMenuIndex + direction, 0, maxIndex);
    if (newIndex != sessionMenuIndex) {
      int oldIndex = sessionMenuIndex;
      sessionMenuIndex = newIndex;
      redrawSessionMenuSelection(oldIndex, sessionMenuIndex);
    }
    return;
  }

  if (currentScreen != SCREEN_SETTINGS) return;

  if (!editingSetting) {
    int newIndex = constrain(settingsIndex + direction, 0, SETTINGS_COUNT - 1);
    if (newIndex != settingsIndex) {
      int oldIndex = settingsIndex;
      settingsIndex = newIndex;
      redrawSettingsSelection(oldIndex, settingsIndex);
    }
    return;
  }

  switch (settingsIndex) {
    case 0:
      settings.wpm = constrain(settings.wpm + direction, 5, 40);
      dotMs = 1200 / settings.wpm;
      break;

    case 1:
      settings.toneHz = constrain(settings.toneHz + direction * 10, 300, 1200);
      break;

    case 2:
      settings.revealDelaySec = constrain(settings.revealDelaySec + direction, 0, 10);
      break;

    case 3:
      settings.sessionLength = constrain(settings.sessionLength + direction, 5, 100);
      break;

    case 4:
      settings.promotionPercent = constrain(settings.promotionPercent + direction, 70, 100);
      break;

    case 5:
      settings.readyBeep = !settings.readyBeep;
      break;
  }

  drawSettingLine(settingsIndex, true);
}

void handleEncoderRotation() {
  long raw = encoder.getCount();
  long delta = raw - lastEncoderRaw;

  if (delta == 0) return;
  lastEncoderRaw = raw;

  // Contactdender kan zeer snel een tegengestelde puls veroorzaken.
  // We verzamelen eerst een volledige mechanische klik.
  encoderPulseAccumulator += (int)delta;

  unsigned long now = millis();

  while (abs(encoderPulseAccumulator) >= ENCODER_PULSES_PER_STEP) {
    int direction = encoderPulseAccumulator > 0 ? 1 : -1;

    if (now - lastEncoderStepAt >= ENCODER_STEP_GUARD_MS) {
      applyEncoderStep(direction);
      lastEncoderStepAt = now;
    }

    encoderPulseAccumulator -= direction * ENCODER_PULSES_PER_STEP;
  }
}

void updateEncoderButton() {
  bool button = digitalRead(ENCODER_SW);
  unsigned long now = millis();

  if (button != lastEncoderButton && now - lastButtonEdgeAt >= 25) {
    lastButtonEdgeAt = now;

    if (button == LOW) {
      encoderPressedAt = now;
      longPressHandled = false;
    } else {
      if (!longPressHandled) {
        handleShortPress();
      }
    }

    lastEncoderButton = button;
  }

  if (button == LOW &&
      !longPressHandled &&
      now - encoderPressedAt >= 900) {
    longPressHandled = true;

    if (currentScreen == SCREEN_TRAINING) {
      stopTrainingToMenu();
    } else if (currentScreen != SCREEN_MAIN_MENU) {
      if (currentScreen == SCREEN_WEB_STATUS) {
        stopWebServer();
      }
      if (currentScreen == SCREEN_FREE_KEYING && wordGapSavePending) {
        saveSettings();
        wordGapSavePending = false;
      }
      editingSetting = false;
      drawMainMenu();
      setEncoderForCurrentScreen();
    }
  }
}

// ============================================================
// TRAININGSENGINE
// ============================================================

void finishSession() {
  student.completedSessions++;

  int percent = sessionQuestions == 0
    ? 0
    : (sessionCorrect * 100) / sessionQuestions;

  sessionPassed = percent >= settings.promotionPercent;

  // Een les wordt pas als bereikt bewaard wanneer ze succesvol voltooid is.
  if (trainingMode == MODE_KOCH && sessionPassed &&
      selectedKochLesson > student.kochLevel) {
    student.kochLevel = selectedKochLesson;
  }

  saveStudent();
  trainingState = TRAIN_SESSION_COMPLETE;

  if (trainingMode == MODE_KOCH) {
    sessionMenuIndex = 0;
    drawSessionMenu();
    setEncoderForCurrentScreen();
  } else {
    drawSessionComplete();
  }
}

void updateTraining() {
  unsigned long now = millis();

  switch (trainingState) {
    case TRAIN_NEW_QUESTION:
      if (sessionQuestions >= settings.sessionLength) {
        finishSession();
        break;
      }

      currentMorseIndex = chooseQuestionIndex();
      userMorse = "";
      resetKeyer();

      drawListening();
      trainingTimer = now;
      trainingState = TRAIN_PREPARE_PLAYBACK;
      break;

    case TRAIN_PREPARE_PLAYBACK:
      if (now - trainingTimer >= 500) {
        startPlayback(morseTable[currentMorseIndex].code);
        trainingState = TRAIN_PLAYBACK;
      }
      break;

    case TRAIN_PLAYBACK:
      if (updatePlayback()) {
        trainingTimer = now;
        trainingState = TRAIN_REVEAL_DELAY;
      }
      break;

    case TRAIN_REVEAL_DELAY:
      if (now - trainingTimer >=
          (unsigned long)settings.revealDelaySec * 1000UL) {
        userMorse = "";
        drawAnswerPrompt();

        if (settings.readyBeep) {
          // Kort startsignaal, duidelijk hoger dan de morsetoon.
          tone(BUZZER_PIN, 1100);
          trainingTimer = now;
          trainingState = TRAIN_READY_BEEP;
        } else {
          lastPaddleActivity = now;
          trainingState = TRAIN_WAIT_INPUT;
        }
      }
      break;

    case TRAIN_READY_BEEP:
      // Paddle wordt pas actief nadat de piep volledig afgelopen is.
      if (now - trainingTimer >= 100) {
        toneOff();
        resetKeyer();
        lastPaddleActivity = now;
        trainingState = TRAIN_WAIT_INPUT;
      }
      break;

    case TRAIN_WAIT_INPUT:
      updateManualKeyInput();

      if (userMorse.length() > 0 &&
          keyerPhase == KEYER_IDLE &&
          now - lastPaddleActivity >= 1100) {
        trainingState = TRAIN_CHECK_INPUT;
      }
      break;

    case TRAIN_CHECK_INPUT:
      lastAnswerCorrect =
        userMorse == String(morseTable[currentMorseIndex].code);

      sessionQuestions++;
      student.totalQuestions++;

      if (lastAnswerCorrect) {
        sessionCorrect++;
        student.totalCorrect++;
      }

      drawTrainingHeader();
      drawTrainingResult(lastAnswerCorrect);
      resultSound(lastAnswerCorrect);

      trainingTimer = now;
      trainingState = TRAIN_SHOW_RESULT;
      break;

    case TRAIN_SHOW_RESULT:
      if (now - trainingTimer >= 1300) {
        trainingState = TRAIN_NEW_QUESTION;
      }
      break;

    case TRAIN_SESSION_COMPLETE:
      // Wachten op korte druk om terug te keren.
      break;
  }
}

// ============================================================
// SETUP EN LOOP
// ============================================================

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);

  // GPIO35 is input-only and has no internal pull-up on the ESP32.
  // The TRAINER V2 PCB must therefore provide the external bias for DIT.
  pinMode(PADDLE_DIT, INPUT);
  pinMode(PADDLE_DAH, INPUT_PULLUP);
  pinMode(STRAIGHT_KEY, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  loadData();

  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachHalfQuad(ENCODER_CLK, ENCODER_DT);
  resetEncoderTracking();

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);
  tft.setRotation(1);
  tft.setTextWrap(false);

  randomSeed((uint32_t)micros());

  drawSplash();
  drawMainMenu();
}

void loop() {
  updateEncoderButton();

  if (webRunning) {
    updateWebServer();
  }

  if (currentScreen == SCREEN_FREE_KEYING) {
    updateFreeKeying();
    handleEncoderRotation();
  } else if (currentScreen != SCREEN_TRAINING) {
    handleEncoderRotation();
  } else {
    updateTraining();
  }
}