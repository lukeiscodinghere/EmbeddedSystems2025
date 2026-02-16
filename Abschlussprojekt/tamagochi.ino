/*=========================================================================================/
/        |                         _______________________                  |              /
/=============================== |== Tamagochi - Wesen ==| ================================/
/                                 \_____________________/                                  /
/                                                                                          /
/   Mit Zuständen die lernend sind                                                         /
/   ~ Arduino Uno ~                                                                        /
-------------------------------------------------------------------------------------------#
/   Zustände: Normal, Angry, Singend, Game, Schlafen                                       /
/   Befehle/Funktionen: S1 - startet Game Modus (pacman abklatsch)                         /
/                       S4 : Beendet Game modus und geht zurück in Normal (=Pet-Modus)     /
/                       S2, S7, S10, S5 : Bewegen im Game Modus                            /
/                       Knopf : Versetzt Wesen in den Schalfmodus, merkt sich Mood         /
/==========================================================================================*/

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <Keypad.h>
#include <EEPROM.h>

// ---------- Pinanordnung - Hardware ----------
/*Bauteile: Arduino Kit Taster Matrix S1-S16 , Joystick , 1 Widerstand , 1 Passiv-Buzzer, 1 Knopf(für Sleep-Modus)*/
#define MATRIX_PIN 6
#define BUZZER_PIN 9
#define SLEEP_BTN_PIN 2

#define MW 16
#define MH 16

#define JOY_VRX_PIN A4
#define JOY_VRY_PIN A5
#define JOY_SW_PIN  8   // optional


// -------- Alle Structs --------
struct Pt8 { int8_t x, y; };

struct ToneStep { uint16_t freq; uint16_t dur; };

struct Melody {
  const ToneStep* seq = nullptr;
  uint8_t idx = 0;
  uint32_t until = 0;
} melody;

// ---------- EEPROM -----------
struct SaveData {
  uint8_t magic;       // 0xA7
  uint8_t sleeping;    // 0/1
  uint8_t mood;        // 0/1
  uint16_t stim;       // 0..500
  uint8_t checksum;
};

const int EEPROM_ADDR = 0;

uint8_t checksum8(const SaveData& d) {
  uint16_t sum = d.magic + d.sleeping + d.mood + (uint8_t)(d.stim & 0xFF) + (uint8_t)(d.stim >> 8) + 0x3D;
  return (uint8_t)(sum & 0xFF);
}

int joyCenterX = 512;
int joyCenterY = 512;

const int JOY_DEADZONE = 140;     // ggf. 60..140 umändern falls Joystick nicht geht
const int JOY_SAMPLE_N = 8;      // Mittelwert beim Kalibrieren
uint32_t nextJoyPollAt = 0;
const uint16_t JOY_POLL_MS = 60; // wie oft lesen


// ---------- Matrix ----------
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  MW, MH, MATRIX_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800
);

// ---------- Keypad 4x4 (S1..S16) ----------
/* Layout: S1  S2  S3  S4 / 
           S5  S6  S7  S8 / 
           S9  S10 S11 S12 / 
           S13 S14 S15 S16*/
const byte ROWS = 4;
const byte COLS = 4;

// Tasten-Belegung:
// S1 game start, S4 game exit, S2 up, S5 left, S7 right, S10 down, S13 sing

char keys[ROWS][COLS] = {
  {'A','8','X','D'},   // S1=start, S2=up, S4=exit
  {'4','X','6','X'},   // S5=left, S7=right
  {'X','2','X','X'},   // S10=down
  {'S','X','X','X'}    // S13=sing
};

// R1..R4
byte rowPins[ROWS] = {3, 4, 5, 7};
// C1..C4
byte colPins[COLS] = {A0, A1, A2, A3};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------- Mood - States ----------
enum Mood : uint8_t { MOOD_HAPPY = 0, MOOD_ANGRY = 1 };
enum Mode : uint8_t { MODE_PET = 0, MODE_GAME = 1 };
enum PetState : uint8_t { PET_NORMAL = 0, PET_SINGING = 1 };

Mood mood = MOOD_HAPPY;
Mode mode = MODE_PET;
PetState petState = PET_NORMAL;
bool sleeping = false;

// Angry timing
const uint32_t ANGRY_MS = 12000;
uint32_t angryUntil = 0;

// ---------- Stress / Stimulation ----------
uint16_t stim = 0;                  // 0..500
uint32_t lastStimUpdateAt = 0;
uint32_t lastInteractAt = 0;

const uint16_t STIM_DECAY_PER_SEC = 10;   // beruhigt pro Sekunde
const uint16_t STIM_THRESH_ANGRY  = 120;  // ab hier Angry
const uint16_t STIM_MAX           = 500;

// Extra: Sing-spam tracking
uint32_t lastSingAt = 0;
uint8_t  singSpamCount = 0;                 // zählt schnelle S13-Aufrufe
const uint32_t SING_SPAM_WINDOW_MS = 2000;  // innerhalb 2s zählt als Spam
const uint8_t  SING_SPAM_LIMIT     = 3;     // erst ab 3 schnellen Aufrufen wird direkt angry mode aktiviert

// ---------- Eyes ----------
struct Eye { int baseX, baseY; };
Eye leftEye  = { 4, 8 };
Eye rightEye = { 11, 8 };

int curDX = 0, curDY = 0;
int targetDX = 0, targetDY = 0;
uint32_t nextMoveAt = 0;

// blink
bool blinkClosed = false;
uint32_t blinkUntil = 0;
uint32_t nextBlinkAt = 0;

// Singing duration (nur Gesicht anzeigen)
uint32_t singingUntil = 0;

static inline int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// ---------- Tunes ----------
const ToneStep TUNE_HAPPY[] PROGMEM = {
  {523,120},{659,120},{784,180},{0,80},{784,140},{988,240},{0,0}
};

const ToneStep TUNE_ANGRY[] PROGMEM = {
  {196,120},{0,50},{196,120},{0,50},{165,180},{131,260},{0,0}
};

// Sleep tunes: rotieren 1 <-> 2
const ToneStep TUNE_SLEEP_1[] PROGMEM = {
  {392,400}, {392,400}, {440,600}, {392,600}, {0,200},
  {392,400}, {440,400}, {494,600}, {440,800}, {0,300},
  {330,800}, {294,900}, {0,400},
  {0,0}
};

const ToneStep TUNE_SLEEP_2[] PROGMEM = {
  {330,450}, {392,450}, {440,600}, {392,600}, {0,200},
  {330,450}, {392,450}, {440,800}, {0,250},
  {294,700}, {262,900}, {0,400},
  {0,0}
};

const ToneStep TUNE_WAKE[] PROGMEM = {
  {262,90},{330,90},{392,140},{0,0}
};

// Retro-style singing jingles
const ToneStep SING_JINGLE_1[] PROGMEM = {
  {988,120},{0,40},{988,120},{0,40},{784,120},{0,40},{880,160},{0,80},
  {784,120},{659,120},{784,180},{0,120},
  {0,0}
};

const ToneStep SING_JINGLE_2[] PROGMEM = {
  {523,90},{659,90},{784,120},{0,40},{784,120},{659,120},{523,180},{0,80},
  {587,120},{698,120},{880,220},{0,120},
  {0,0}
};

const ToneStep SING_JINGLE_3[] PROGMEM = {
  {440,120},{466,120},{494,120},{523,160},{0,60},
  {494,120},{466,120},{440,220},{0,80},
  {392,160},{440,260},{0,120},
  {0,0}
};

const ToneStep* const SING_POOL[] = { SING_JINGLE_1, SING_JINGLE_2, SING_JINGLE_3 };
const uint8_t SING_POOL_N = sizeof(SING_POOL) / sizeof(SING_POOL[0]);

// Sleep rotation index
uint8_t sleepTuneIndex = 0; // 0 -> sleep_1, 1 -> sleep_2

// ---------- EEPROM persist ----------
uint32_t nextEepromSaveAt = 0;
bool dirtySave = false;

void markDirtySave() { dirtySave = true; }

void loadState() {
  SaveData d;
  EEPROM.get(EEPROM_ADDR, d);
  if (d.magic != 0xA7) return;
  if (checksum8(d) != d.checksum) return;

  sleeping = d.sleeping != 0;
  mood = (d.mood == 1) ? MOOD_ANGRY : MOOD_HAPPY;
  stim = d.stim;
}

void saveStateMaybe() {
  uint32_t now = millis();
  if (!dirtySave) return;
  if (now < nextEepromSaveAt) return;

  SaveData d;
  d.magic = 0xA7;
  d.sleeping = sleeping ? 1 : 0;
  d.mood = (uint8_t)mood;
  d.stim = stim;
  d.checksum = checksum8(d);

  EEPROM.put(EEPROM_ADDR, d);
  dirtySave = false;
  nextEepromSaveAt = now + 5000;
}

// ---------- Melody helpers ----------
void playMelody(const ToneStep* seq) {
  melody.seq = seq;
  melody.idx = 0;
  melody.until = 0;
}

void stopMelody() {
  noTone(BUZZER_PIN);
  melody.seq = nullptr;
  melody.idx = 0;
  melody.until = 0;
}


const ToneStep* currentSleepTune() {
  return (sleepTuneIndex == 0) ? TUNE_SLEEP_1 : TUNE_SLEEP_2;
}

void rotateSleepTune() {
  sleepTuneIndex ^= 1;
  playMelody(currentSleepTune());
}

void updateMelody() {
  if (!melody.seq) return;

  uint32_t now = millis();
  if (melody.until && now < melody.until) return;

  ToneStep s;
  s.freq = pgm_read_word(&melody.seq[melody.idx].freq);
  s.dur  = pgm_read_word(&melody.seq[melody.idx].dur);

  if (s.dur == 0) {
    if (sleeping && (melody.seq == TUNE_SLEEP_1 || melody.seq == TUNE_SLEEP_2)) {
      rotateSleepTune();
      return;
    }
    stopMelody();
    return;
  }

  if (s.freq == 0) noTone(BUZZER_PIN);
  else tone(BUZZER_PIN, s.freq);

  melody.until = now + s.dur;
  melody.idx++;
}


// ---------- Drawing helpers ----------
void drawEye3x3(int cx, int cy, uint16_t col) {
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      matrix.drawPixel(cx + dx, cy + dy, col);
}

void drawClosedEye(int cx, int cy, uint16_t col) {
  matrix.drawPixel(cx - 1, cy, col);
  matrix.drawPixel(cx,     cy, col);
  matrix.drawPixel(cx + 1, cy, col);
}

void drawMouthSmall(int cx, int y, uint16_t col) {
  matrix.drawPixel(cx - 1, y, col);
  matrix.drawPixel(cx,     y, col);
  matrix.drawPixel(cx + 1, y, col);
}

// ---------- Stress logic ----------
void enterAngry() {
  if (sleeping) return;
  mood = MOOD_ANGRY;
  angryUntil = millis() + ANGRY_MS;
  playMelody(TUNE_ANGRY);
  markDirtySave();
}

void maybeEnterAngryFromStim() {
  if (sleeping) return;
  if (mood == MOOD_ANGRY) return;
  if (stim >= STIM_THRESH_ANGRY) enterAngry();
}

void maybeExitAngry() {
  if (mood != MOOD_ANGRY) return;
  if (millis() >= angryUntil) {
    mood = MOOD_HAPPY;
    markDirtySave();
  }
}

void addInteractionWeighted(uint16_t amount, bool canTriggerAngry) {
  uint32_t now = millis();
  if (now - lastInteractAt < 250) amount += 10;
  else if (now - lastInteractAt < 500) amount += 5;

  lastInteractAt = now;

  uint32_t tmp = (uint32_t)stim + amount;
  stim = (tmp > STIM_MAX) ? STIM_MAX : (uint16_t)tmp;

  markDirtySave();

  if (canTriggerAngry) {
    maybeEnterAngryFromStim();
  }
}

void updateStimDecay() {
  uint32_t now = millis();
  if (lastStimUpdateAt == 0) lastStimUpdateAt = now;

  uint32_t dt = now - lastStimUpdateAt;
  lastStimUpdateAt = now;

  uint32_t dec = (dt * STIM_DECAY_PER_SEC) / 1000;
  if (dec > 0) {
    stim = (stim > dec) ? (uint16_t)(stim - dec) : 0;
    markDirtySave();
  }

  if (now - lastSingAt > 3000) singSpamCount = 0;
}

// ---------- Pet behavior ----------
void scheduleBlink() { nextBlinkAt = millis() + random(2000, 6000); }

void updateBlink() {
  uint32_t now = millis();
  if (blinkClosed && now >= blinkUntil) {
    blinkClosed = false;
    scheduleBlink();
  } else if (!blinkClosed && now >= nextBlinkAt) {
    blinkClosed = true;
    blinkUntil = now + random(80, 150);
  }
}

void pickNewTarget() {
  if (mood == MOOD_HAPPY) {
    targetDX = random(-1, 2);
    targetDY = random(-1, 2);
    nextMoveAt = millis() + random(500, 1400);
  } else {
    targetDX = random(-2, 3);
    targetDY = random(-1, 2);
    nextMoveAt = millis() + random(140, 450);
  }
}

void stepMotion() {
  int step = (mood == MOOD_ANGRY) ? 2 : 1;
  while (step--) {
    if (curDX < targetDX) curDX++;
    else if (curDX > targetDX) curDX--;
    if (curDY < targetDY) curDY++;
    else if (curDY > targetDY) curDY--;
  }
}

void setSleeping(bool on) {
  sleeping = on;
  curDX = curDY = targetDX = targetDY = 0;

  petState = PET_NORMAL;
  singingUntil = 0;

  blinkClosed = false;
  blinkUntil = 0;
  nextBlinkAt = 0;

  singSpamCount = 0;
  lastSingAt = 0;

  stopMelody();
  if (sleeping) playMelody(currentSleepTune());
  else playMelody(TUNE_WAKE);

  markDirtySave();
}

// ---------- Singing (S13) ----------
void startSinging() {
  if (mode != MODE_PET || sleeping) return;
  if (mood == MOOD_ANGRY) return;

  uint32_t now = millis();

  if (now - lastSingAt <= SING_SPAM_WINDOW_MS) {
    if (singSpamCount < 255) singSpamCount++;
  } else {
    singSpamCount = 0;
  }
  lastSingAt = now;

  addInteractionWeighted(10, false);
  if (singSpamCount >= SING_SPAM_LIMIT) {
    addInteractionWeighted(70, true);
  }
  if (mood == MOOD_ANGRY) return;

  petState = PET_SINGING;
  singingUntil = now + 2500;

  stopMelody();
  uint8_t pick = (uint8_t)random(0, SING_POOL_N);
  playMelody(SING_POOL[pick]);
}

// ---------- Render Pet ----------
void renderPet() {
  matrix.fillScreen(0);

  if (sleeping) {
    uint16_t col = matrix.Color(40, 40, 40);
    drawClosedEye(leftEye.baseX,  leftEye.baseY,  col);
    drawClosedEye(rightEye.baseX, rightEye.baseY, col);
    matrix.show();
    return;
  }

  if (petState == PET_SINGING) {
    uint16_t purple = matrix.Color(140, 0, 180);
    drawEye3x3(leftEye.baseX,  leftEye.baseY,  purple);
    drawEye3x3(rightEye.baseX, rightEye.baseY, purple);
    drawMouthSmall(MW / 2, 12, purple);
    matrix.show();
    return;
  }

  uint16_t col = (mood == MOOD_HAPPY)
    ? matrix.Color(160,160,160)
    : matrix.Color(180,0,0);

  int lx = clampi(leftEye.baseX  + curDX, 1, MW - 2);
  int ly = clampi(leftEye.baseY  + curDY, 1, MH - 2);
  int rx = clampi(rightEye.baseX + curDX, 1, MW - 2);
  int ry = clampi(rightEye.baseY + curDY, 1, MH - 2);

  if (blinkClosed) {
    drawClosedEye(lx, ly, col);
    drawClosedEye(rx, ry, col);
  } else {
    drawEye3x3(lx, ly, col);
    drawEye3x3(rx, ry, col);
  }

  int bar = map(stim, 0, STIM_MAX, 0, MW);
  uint16_t barCol = matrix.Color(20, 20, 20);
  for (int x = 0; x < bar; x++) matrix.drawPixel(x, 0, barCol);

  matrix.show();
}




// ======================================================================
// ======================= GAME MODE: PACMAN ==============================
// ======================================================================

Pt8 pac = { 1, 1 };
Pt8 ghost1 = { 12, 12 };
Pt8 ghost2 = { 12, 1 };

bool ghost2Active = false;

int8_t pacDX = 1, pacDY = 0;

void pollJoystickForGame() {
  static bool joyArmed = true; // darf erst wieder setzen, wenn zurück in Center
  uint32_t now = millis();
  if (now < nextJoyPollAt) return;
  nextJoyPollAt = now + JOY_POLL_MS;

  int rawX = analogRead(JOY_VRX_PIN);
  int rawY = analogRead(JOY_VRY_PIN);

  int x = rawX - joyCenterX;
  int y = rawY - joyCenterY;

  // Deadzone
  bool inCenter = (abs(x) < JOY_DEADZONE) && (abs(y) < JOY_DEADZONE);
  if (inCenter) {
    joyArmed = true;
    return;
  }

  // optional: Exit per Stick-Button
  if (digitalRead(JOY_SW_PIN) == LOW) {
    mode = MODE_PET;
    markDirtySave();
    return;
  }

  // wenn noch nicht "bewaffnet", ignoriere (verhindert Dauerüberschreiben)
  if (!joyArmed) return;

  // Richtung setzen (dominante Achse)
  if (abs(x) > abs(y)) {
    if (x > 0) { pacDX =  1; pacDY = 0; }
    else       { pacDX = -1; pacDY = 0; }
  } else {
    if (y > 0) { pacDX = 0; pacDY =  1; }
    else       { pacDX = 0; pacDY = -1; }
  }

  joyArmed = false;
}


bool pellets[MH][MW];   // 16x16 = 256 bytes
int8_t pelletX = -1;
int8_t pelletY = -1;

uint16_t gameScore = 0;
const uint8_t WIN_SCORE = 10;

// --- Portal Paare (Top-Left-Koordinaten) ---
const Pt8 P1A = { 0, 5 };
const Pt8 P1B = { 14, 5 };
const Pt8 P2A = { 0, 10 };
const Pt8 P2B = { 14, 10 };


uint32_t nextGameTickAt = 0;
const uint16_t PAC_TICK_MS = 140;      // pac speed
const uint16_t GHOST_TICK_MS = 360;    // ghosts speed
uint32_t nextGhostTickAt = 0;




void clearPellets() {
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++)
      pellets[y][x] = false;
  
  pelletX = -1;
  pelletY = -1;
}

bool isOccupiedStart(int x, int y) {
  if (x == pac.x && y == pac.y) return true;
  if (x == ghost1.x && y == ghost1.y) return true;
  if (x == ghost2.x && y == ghost2.y) return true;
  return false;
}

void spawnSinglePellet() {
  // NICHT clearPellets();  -> nur die eine Position
  if (pelletX >= 0 && pelletY >= 0) {
    pellets[pelletY][pelletX] = false;
  }
  pelletX = -1;
  pelletY = -1;

  uint16_t tries = 0;
  const uint16_t TRY_LIMIT = 2000;

  while (tries++ < TRY_LIMIT) {
    int x = random(0, MW - 1); // 0..14
    int y = random(0, MH - 1); // 0..14

    if (!isPassable2x2(x, y)) continue;
    if (blocksOverlap2x2(x, y, pac.x, pac.y)) continue;
    if (blocksOverlap2x2(x, y, ghost1.x, ghost1.y)) continue;
    if (ghost2Active && blocksOverlap2x2(x, y, ghost2.x, ghost2.y)) continue;

    pellets[y][x] = true;
    pelletX = x;
    pelletY = y;
    return;
  }
}


void gameResetPacman() {
  pac = { 1, 1 };
  ghost1 = { 12, 12 };
  ghost2 = { 12, 1 };

  ghost2Active = false;

  //Falls koolidiert Pos mit Wall
  if (blockHitsWall2x2(pac.x, pac.y))    pac = {2,2};
  if (blockHitsWall2x2(ghost1.x,ghost1.y)) ghost1 = {12,12};
  if (blockHitsWall2x2(ghost2.x,ghost2.y)) ghost2 = {12,2};

  pacDX = 1; pacDY = 0;
  gameScore = 0;

  spawnSinglePellet(); // Anzahl Punkte
  nextGameTickAt = millis() + PAC_TICK_MS;
  nextGhostTickAt = millis() + GHOST_TICK_MS;
}

// ---------- Spielbrettmatrix - Aufbau (1 = wall, 0 = path) ----------
// 16 Zeilen, je 16 Spalten
const uint8_t MAZE[MH][MW] PROGMEM = {
  // 0 1 2 3 4 5 6 7 8 9 A B C D E F
  {1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1}, // 0  
  {1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1}, // 1
  {1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1}, // 2
  {1,1,1,1,0,0,1,1,1,1,0,0,1,0,0,1}, // 3
  {1,0,0,0,0,0,0,0,1,1,0,0,1,0,0,1}, // 4
  {0,0,0,0,0,0,0,0,1,1,0,0,1,0,0,0}, // 5  PORTAL #1 (links+rechts offen)
  {0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0}, // 6  
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}, // 7
  {1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1}, // 8
  {1,0,0,1,1,0,0,1,1,0,0,0,1,1,1,1}, // 9
  {0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0}, // A  PORTAL #2 (links+rechts offen)
  {0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0}, // B 
  {1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1}, // C
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}, // D
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}, // E
  {1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1}  // F  PORTAL #3 oben unten offen
};




static inline bool isWall(int x, int y) {
  if (x < 0 || x >= MW || y < 0 || y >= MH) return true;
  return pgm_read_byte(&MAZE[y][x]) != 0;
}

static inline void drawBlock2x2(int x, int y, uint16_t col) {
  // keine clamps hier, weil teleport + wall-check
  matrix.drawPixel(x,   y,   col);
  matrix.drawPixel(x+1, y,   col);
  matrix.drawPixel(x,   y+1, col);
  matrix.drawPixel(x+1, y+1, col);
}

static inline bool blockHitsWall2x2(int x, int y) {
  return isWall(x, y) || isWall(x+1, y) || isWall(x, y+1) || isWall(x+1, y+1);
}

static inline bool isPassable2x2(int x, int y) {
  // Ein 2x2-Block darf komplett NICHT in Walls sein
  return !blockHitsWall2x2(x, y);
}


static inline bool blocksOverlap2x2(int ax, int ay, int bx, int by) {
  return (ax < bx+2) && (ax+2 > bx) && (ay < by+2) && (ay+2 > by);
}


static inline void wrap2x2(Pt8 &p) {
  // erlaubte Top-Left Koordinate: 0..MW-2 / 0..MH-2
  if (p.x < 0)      p.x = MW - 2;
  if (p.x > MW - 2) p.x = 0;
  if (p.y < 0)      p.y = MH - 2;
  if (p.y > MH - 2) p.y = 0;
}

static inline void drawGhost3of4(int x, int y, uint16_t col) {
  // L-Form: (x,y) (x+1,y) (x,y+1)  -> fehlend ist (x+1,y+1)
  matrix.drawPixel(x,   y,   col);
  matrix.drawPixel(x+1, y,   col);
  matrix.drawPixel(x,   y+1, col);
}


bool hitGhost() {
  if (blocksOverlap2x2(pac.x, pac.y, ghost1.x, ghost1.y)) return true;
  if (ghost2Active && blocksOverlap2x2(pac.x, pac.y, ghost2.x, ghost2.y)) return true;
  return false;
}


void renderPacman() {
  matrix.fillScreen(0);

  // Walls: blau
  uint16_t wallCol = matrix.Color(0, 0, 180);

  // Walls zeichnen
  for (int y = 0; y < MH; y++) {
    for (int x = 0; x < MW; x++) {
      if (isWall(x, y)) {
        matrix.drawPixel(x, y, wallCol);
      }
    }
  }

  // Single Pellet: hellgrün als 2x2 - die Punkte zum Sammeln
  if (pelletX >= 0 && pelletY >= 0) {
    uint16_t pelletCol = matrix.Color(0, 200, 0);
    drawBlock2x2(pelletX, pelletY, pelletCol);
  }

  // Pacman "Player" 2x2 gelb
  drawBlock2x2(pac.x, pac.y, matrix.Color(180, 160, 0));

  // Ghosts 2x2 rot, türkis
  drawGhost3of4(ghost1.x, ghost1.y, matrix.Color(180, 0, 0));
  if (ghost2Active) {
    drawGhost3of4(ghost2.x, ghost2.y, matrix.Color(0, 180, 180));
  }

  // mini score bar bottom
  int sb = (gameScore > 16) ? 16 : gameScore;
  for (int x = 0; x < sb; x++) matrix.drawPixel(x, MH - 1, matrix.Color(0, 30, 0));

  matrix.show();
}


// Zahlen score nach dem Game
void showScoreBig(uint16_t s, uint16_t ms) {
  uint8_t shown = (s > 99) ? (uint8_t)(s % 100) : (uint8_t)s;

  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(2);
  matrix.setTextColor(matrix.Color(120, 120, 120));

  char buf[4];
  if (shown < 10) {
    buf[0] = '0' + shown;
    buf[1] = '\0';
    // zentrieren
    matrix.setCursor(5, 1);
  } else {
    buf[0] = '0' + (shown / 10);
    buf[1] = '0' + (shown % 10);
    buf[2] = '\0';
    matrix.setCursor(1, 1);
  }

  matrix.print(buf);
  matrix.show();

  uint32_t endAt = millis() + ms;
  while (millis() < endAt) {
    updateMelody();
  }

  // zurück auf defaults
  matrix.setTextSize(1);
}

void gameOverPacman() {
  // "fail" beep
  tone(BUZZER_PIN, 196, 120);
  delay(140);
  tone(BUZZER_PIN, 131, 180);

  showScoreBig(gameScore, 1800); // ~1.8s anzeigen

  // zurück in Pet
  mode = MODE_PET;
  markDirtySave();
}

void moveGhostToward(Pt8 &g) {
  int8_t candDX[4] = { 1, -1, 0, 0 };
  int8_t candDY[4] = { 0, 0, 1, -1 };

  // Ziel: näher an Pac
  int bestI = -1;
  int bestScore = 9999;

  // 20% random move
  bool doRandom = (random(0,100) < 20);

  if (doRandom) {
    for (int tries = 0; tries < 8; tries++) {
      int i = random(0,4);
      Pt8 cand = { (int8_t)(g.x + candDX[i]), (int8_t)(g.y + candDY[i]) };
      
      wrap2x2(cand);
      if (cand.x == P1A.x && cand.y == P1A.y) cand = P1B;
      else if (cand.x == P1B.x && cand.y == P1B.y) cand = P1A;
      else if (cand.x == P2A.x && cand.y == P2A.y) cand = P2B;
      else if (cand.x == P2B.x && cand.y == P2B.y) cand = P2A;
      wrap2x2(cand);

      if (isPassable2x2(cand.x, cand.y)) {
          g = cand;
        return;
      }

    }
    return; // stecken geblieben
  }

  for (int i = 0; i < 4; i++) {
    Pt8 cand = { (int8_t)(g.x + candDX[i]), (int8_t)(g.y + candDY[i]) };
    wrap2x2(cand);
    if (cand.x == P1A.x && cand.y == P1A.y) cand = P1B;
else if (cand.x == P1B.x && cand.y == P1B.y) cand = P1A;
else if (cand.x == P2A.x && cand.y == P2A.y) cand = P2B;
else if (cand.x == P2B.x && cand.y == P2B.y) cand = P2A;
wrap2x2(cand);

    if (!isPassable2x2(cand.x, cand.y)) continue;

    int d = abs((int)pac.x - cand.x) + abs((int)pac.y - cand.y);

    if (d < bestScore) {
      bestScore = d;
      bestI = i;
    }
  }

  if (bestI >= 0) {
    Pt8 cand = { (int8_t)(g.x + candDX[bestI]), (int8_t)(g.y + candDY[bestI]) };
    wrap2x2(cand);
    if (cand.x == P1A.x && cand.y == P1A.y) cand = P1B;
else if (cand.x == P1B.x && cand.y == P1B.y) cand = P1A;
else if (cand.x == P2A.x && cand.y == P2A.y) cand = P2B;
else if (cand.x == P2B.x && cand.y == P2B.y) cand = P2A;
wrap2x2(cand);

    if (isPassable2x2(cand.x, cand.y)) g = cand;
  }

}


void tickPacman() {
  uint32_t now = millis();

  // pac move
  if (now >= nextGameTickAt) {
    nextGameTickAt = now + PAC_TICK_MS;

    Pt8 cand = { (int8_t)(pac.x + pacDX), (int8_t)(pac.y + pacDY) };
    wrap2x2(cand);

    if (cand.x == P1A.x && cand.y == P1A.y) cand = P1B;
    else if (cand.x == P1B.x && cand.y == P1B.y) cand = P1A;
    else if (cand.x == P2A.x && cand.y == P2A.y) cand = P2B;
    else if (cand.x == P2B.x && cand.y == P2B.y) cand = P2A;
    wrap2x2(cand);

    // nur bewegen, wenn 2x2 frei
    if (isPassable2x2(cand.x, cand.y)) {
      pac = cand;
    }

    // Single Pellet fressen
    bool ate = false;
    if (pelletX >= 0 && pelletY >= 0) {
      if (blocksOverlap2x2(pac.x, pac.y, pelletX, pelletY)) {
        pellets[pelletY][pelletX] = false;   // optional
        pelletX = -1;
        pelletY = -1;
        ate = true;
      }
    }

    if (ate) {
      gameScore++;

      // KEIN stim/stress im Game
      tone(BUZZER_PIN, 880, 25);

      // Ghost2 ab 5 Punkten erst hinzufügen
      if (!ghost2Active && gameScore >= 5) {
        ghost2Active = true;
      }

      // Win bei 10
      if (gameScore >= WIN_SCORE) {
        tone(BUZZER_PIN, 988, 120);
        delay(140);
        tone(BUZZER_PIN, 1318, 180);

        showScoreBig(gameScore, 1800);
        mode = MODE_PET;
        markDirtySave();
        return;
      }

      // nächsten Punkt spawnen
      spawnSinglePellet();
    } else {
      // falls Score irgendwo anders geändert würde
      if (!ghost2Active && gameScore >= 5) {
        ghost2Active = true;
      }
    }

    if (hitGhost()) {
      gameOverPacman();
      return;
    }
  }

  // ghost move
  if (now >= nextGhostTickAt) {
    nextGhostTickAt = now + GHOST_TICK_MS;

    moveGhostToward(ghost1);
    if (hitGhost()) { gameOverPacman(); return; }

    if (ghost2Active) {
      moveGhostToward(ghost2);
      if (hitGhost()) { gameOverPacman(); return; }
    }
  }

  renderPacman();
}



void setPacDirFromKey(char key) {
  if (key == '8') { pacDX = 0; pacDY = -1; }
  if (key == '2') { pacDX = 0; pacDY =  1; }
  if (key == '4') { pacDX = -1; pacDY = 0; } // LEFT
  if (key == '6') { pacDX =  1; pacDY = 0; } // RIGHT
}

// ---------- Inputs ----------
void handleSleepButton() {
  static bool last = HIGH;
  bool now = digitalRead(SLEEP_BTN_PIN);
  if (last == HIGH && now == LOW) {
    setSleeping(!sleeping);
  }
  last = now;
}

void handleKeypad() {
  char key = keypad.getKey();
  if (!key || key == 'X') return;

  // PET mode
  if (mode == MODE_PET) {
    if (sleeping) return;

    if (key == 'S') {          // S13 sing
      startSinging();
      return;
    }

    if (key == 'A') {          // S1 start game
      addInteractionWeighted(10, true);
      mode = MODE_GAME;

        long sx=0, sy=0;
        for (int i=0;i<JOY_SAMPLE_N;i++){ 
          sx+=analogRead(JOY_VRX_PIN); 
          sy+=analogRead(JOY_VRY_PIN); 
          delay(5); 
        }
          joyCenterX = sx / JOY_SAMPLE_N;
        joyCenterY = sy / JOY_SAMPLE_N;
      gameResetPacman();
      renderPacman();
      return;
    }

    addInteractionWeighted(6, true);
    return;
  }

  // GAME mode
  if (mode == MODE_GAME) {
    // exit game
    if (key == 'D') { // S4
      mode = MODE_PET;
      markDirtySave();
      return;
    }

    // steer pacman
    if (key == '8' || key == '2' || key == '6' || key == '4') {
      //addInteractionWeighted(2, true);
      setPacDirFromKey(key);
    }
  }
}

// ---------- Setup / Loop ----------
void setup() {
  randomSeed(analogRead(A4) ^ analogRead(A5) ^ micros());

  pinMode(SLEEP_BTN_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  matrix.begin();
  matrix.setBrightness(25);

  loadState();

  if (sleeping) {
    playMelody(currentSleepTune());
  }

  scheduleBlink();
  pickNewTarget();

  pinMode(JOY_SW_PIN, INPUT_PULLUP);

// Joystick Center kalibrieren (bei Ruhe nicht bewegen)
long sx = 0, sy = 0;
for (int i = 0; i < JOY_SAMPLE_N; i++) {
  sx += analogRead(JOY_VRX_PIN);
  sy += analogRead(JOY_VRY_PIN);
  delay(5);
}
joyCenterX = sx / JOY_SAMPLE_N;
joyCenterY = sy / JOY_SAMPLE_N;

}

void loop() {
  handleSleepButton();
  handleKeypad();

  updateMelody();
  updateStimDecay();

  if (mode == MODE_GAME) {
    // Pacman game tick
    pollJoystickForGame();
    tickPacman();
    saveStateMaybe();
    delay(10);
    return;
  }

  // Pet mode
  if (mode == MODE_PET) {
    if (!sleeping) {
      if (petState == PET_SINGING && millis() >= singingUntil) {
        petState = PET_NORMAL;
      }

      maybeExitAngry();

      if (petState == PET_NORMAL) {
        updateBlink();
        if (millis() >= nextMoveAt) pickNewTarget();
        stepMotion();
      }
    }
    renderPet();
  }

  saveStateMaybe();
  delay(30);

