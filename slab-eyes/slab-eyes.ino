// Slab eyes — landscape Arduino eyes for Waveshare ESP32-C6-LCD-1.47
// (Amazon B0DK5J6LX3). LCD-only slab — no touch, no IMU.
//
// Port of robot-face's egg eyes / looks / blink+wink machines, locked
// landscape 320×172 the whole time. Life is autonomous: look-arounds,
// winks, fidgets, drowsy → asleep. BOOT pokes it; hold BOOT cycles looks.
//
// Serial 115200: mood <name> | look <n|next|prev|list> | gaze x y |
//                blink | wink | poke | pet | sleep | wake | status | help
//
// Backlight stays ≤ 50% (Waveshare heat-spot warning); dims further asleep.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <math.h>

#include "c6_slab_pins.h"

#define BOOT_LONG_MS 700UL

static const int FB_W = 320;
static const int FB_H = 172;

static const uint8_t BG[3] = {12, 13, 18};
static const float EYE_L_X = 86;
static const float EYE_R_X = 234;
static const float EYE_Y = 86;
static const float EYE_A = 58;
static const float EYE_B = 64;
static const float PUPIL_R = 22;
static const float LOOK_TRAVEL_X = 0.50f;
static const float LOOK_TRAVEL_Y = 0.42f;
static const float PAIR_SHIFT_X = 6;
static const float PAIR_SHIFT_Y = 3;
static const float BLUSH_DX = 28;
static const float BLUSH_Y = 148;
static const float BLUSH_R = 16;

enum PupilStyle : uint8_t { ST_NONE = 0, ST_ROUND, ST_DOT, ST_SLIT, ST_STAR, ST_HEART, ST_RING };

struct Look {
  const char *name;
  uint8_t sclera[3];
  uint8_t pupil[3];
  bool hasPupil;
  uint8_t pupil2[3];
  bool hasPupil2;
  bool shine;
  PupilStyle style;
  uint8_t blush[3];
};

// Eye colour palettes ("looks")
static const Look LOOKS[] = {
  {"cloud",      {242, 236, 226}, {22, 24, 30},    true,  {0, 0, 0},     false, true,  ST_ROUND, {232, 96, 110}},
  {"periwinkle", {168, 196, 240}, {0, 0, 0},       false, {0, 0, 0},     false, false, ST_NONE,  {225, 110, 150}},
  {"glowstick",  {200, 255, 30},  {10, 10, 16},    true,  {0, 0, 0},     false, false, ST_SLIT,  {255, 120, 80}},
  {"matcha",     {207, 230, 196}, {30, 40, 90},    true,  {190, 80, 20}, true,  false, ST_DOT,   {240, 120, 120}},
  {"blueberry",  {36, 48, 255},   {214, 246, 255}, true,  {0, 0, 0},     false, false, ST_ROUND, {200, 90, 255}},
  {"bubblegum",  {255, 60, 225},  {60, 225, 90},   true,  {0, 0, 0},     false, false, ST_DOT,   {255, 140, 200}},
  {"lagoon",     {28, 120, 190},  {220, 255, 60},  true,  {0, 0, 0},     false, false, ST_SLIT,  {120, 200, 255}},
  {"peach",      {255, 176, 140}, {70, 40, 40},    true,  {0, 0, 0},     false, true,  ST_ROUND, {255, 110, 120}},
  {"ghost",      {250, 250, 255}, {120, 140, 255}, true,  {0, 0, 0},     false, false, ST_STAR,  {190, 200, 255}},
};
static const int LOOK_COUNT = sizeof(LOOKS) / sizeof(LOOKS[0]);
static const uint8_t COL_SHINE[3] = {255, 255, 255};
static const uint8_t COL_HEART[3] = {232, 96, 110};
static const uint8_t COL_TEAR[3] = {160, 210, 255};

enum Mood : uint8_t {
  MOOD_IDLE = 0, MOOD_HAPPY, MOOD_SLEEPY, MOOD_CURIOUS, MOOD_SURPRISED, MOOD_LOVE,
  MOOD_DIZZY, MOOD_ANNOYED, MOOD_SAD, MOOD_EXCITED, MOOD_ASLEEP, MOOD_COUNT
};
static const char *MOOD_NAMES[MOOD_COUNT] = {
  "idle", "happy", "sleepy", "curious", "surprised", "love", "dizzy", "annoyed", "sad", "excited", "asleep"};

enum GestureType : uint8_t {
  G_NONE = 0, G_GLANCE2, G_THINK, G_BROW, G_WIGGLE, G_STARE, G_SIGH, G_STRETCH, G_EYEROLL
};
enum Awake : uint8_t { AWAKE = 0, DROWSY, ASLEEP };

static const uint32_t DROWSY_AFTER_MS = 75000;
static const uint32_t DROWSY_LEN_MS = 12000;
static const uint32_t LONELY_AFTER_MS = 45000;

struct Shape {
  float sx = 1, sy = 1, top = 0, ang = 0, curve = 0, bot = 0, pupil = 1;
};

static Arduino_DataBus *bus = new Arduino_ESP32SPI(
    C6_LCD_DC_PIN, C6_LCD_CS_PIN, C6_LCD_SCLK_PIN, C6_LCD_MOSI_PIN, GFX_NOT_DEFINED);
static Arduino_GFX *gfx = new Arduino_ST7789(
    bus, C6_LCD_RST_PIN, 0 /* rotation; setRotation(1) in setup */, true /* IPS */,
    C6_LCD_W, C6_LCD_H, 34, 0, 34, 0);

static Adafruit_NeoPixel rgb(1, C6_RGB_PIN, NEO_GRB + NEO_KHZ800);
static Preferences prefs;
static uint16_t *fb = nullptr;

static Shape shapeL, shapeR;
static float lookX = 0, lookY = 0, velX = 0, velY = 0;
static float offX = 0, offY = 0, offVelY = 0;
static float blinkL = 0, blinkR = 0, openPop = 0;
static float blush = 0, swirl = 0, heartbeat = 0, breath = 0;
static float tear = 0;
static int tearSide = 1;
static float drowsy = 0;
static float backlight = 1;

static int lookIndex = 0;
static int pendingLook = -1;
static Mood mood = MOOD_IDLE;
static uint32_t moodUntil = 0;
static bool moodHold = false;
static uint32_t moodEnteredAt = 0;
static bool pokeHappyPending = false;

static float targetLookX = 0, targetLookY = 0;
static bool glancing = false;
static uint32_t glanceUntil = 0;
static uint32_t nextGlanceAt = 0;
static uint32_t nextAutoMoodAt = 0;
static uint32_t nextFidgetAt = 0;
static uint32_t nextMicroAt = 0;
static float microX = 0, microY = 0;

static uint32_t nextBlinkAt = 0;
static uint32_t blinkStart = 0;
static uint32_t blinkClose = 75, blinkHold = 35, blinkOpen = 160;
static bool blinking = false;
static bool blinkAgain = false;
static uint8_t winkEye = 0;
static uint32_t winkStart = 0;
static uint32_t winkDur = 0;
static uint32_t nextWinkAt = 0;

static GestureType gesture = G_NONE;
static uint32_t gestureStart = 0, gestureDur = 0;
static int gestureSide = 1;
static float gAx = 0, gAy = 0, gBx = 0, gBy = 0;

static uint32_t lastInteraction = 0;
static Awake awake = AWAKE;
static uint32_t drowsySince = 0;
static int petCount = 0;
static uint32_t lastPetAt = 0;

static PupilStyle frameStyle = ST_ROUND;
static float frameHeartPulse = 1;
static float frameOffX = 0, frameOffY = 0;
static float frameBlinkL = 0, frameBlinkR = 0;
static uint8_t blushCol[3] = {0, 0, 0};

static bool g_bootDown = false;
static bool g_bootLong = false;
static uint32_t g_bootDownAt = 0;
static String g_line;
static int dirtyTop = 0, dirtyBot = FB_H - 1;
static int prevTop = 0, prevBot = FB_H - 1;
static uint32_t frames = 0;
static uint32_t nextStatsAt = 0;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float easeOut(float t) { t = clampf(t, 0, 1); return 1.0f - (1.0f - t) * (1.0f - t); }
static float easeIn(float t) { t = clampf(t, 0, 1); return t * t; }
static float randf(float a, float b) { return a + (b - a) * (random(0, 10000) / 10000.0f); }
static bool chance(float p) { return randf(0, 1) < p; }
static int32_t since(uint32_t now, uint32_t then) { return (int32_t)(now - then); }
static bool after(uint32_t now, uint32_t when) { return since(now, when) >= 0; }

static void startGesture(GestureType type, uint32_t dur, uint32_t now, int side = 1);
static void startBlink(uint32_t now, bool slow);
static void scheduleBlink(uint32_t from);

static void setMood(Mood next, uint32_t holdMs, bool hold = false) {
  const uint32_t now = millis();
  if (next != mood) {
    moodEnteredAt = now;
    if (next == MOOD_SURPRISED) offVelY -= 95;
    if (next == MOOD_HAPPY || next == MOOD_EXCITED) offVelY -= 55;
    if (next == MOOD_SAD) { tear = 0.001f; tearSide = chance(0.5f) ? -1 : 1; }
  }
  mood = next;
  moodUntil = now + holdMs;
  moodHold = hold;
}

static void wakeUp(uint32_t now) {
  awake = AWAKE;
  drowsy = 0;
  startGesture(G_STRETCH, 700, now);
  pokeHappyPending = true;
  setMood(MOOD_SURPRISED, 700);
  scheduleBlink(now + 750);
  blinkAgain = true;
}

static void touched(uint32_t now) {
  lastInteraction = now;
  if (awake != AWAKE) wakeUp(now);
}

static void fallAsleep(uint32_t now) {
  (void)now;
  awake = ASLEEP;
  gesture = G_NONE;
  glancing = false;
  pokeHappyPending = false;
  setMood(MOOD_ASLEEP, 0, true);
}

static void scheduleBlink(uint32_t from) {
  const bool sleepyish = mood == MOOD_SLEEPY || awake == DROWSY;
  nextBlinkAt = from + (sleepyish ? random(1100, 2200) : random(2600, 5200));
}
static void scheduleGlance(uint32_t from) { nextGlanceAt = from + random(2800, 6200); }
static void scheduleAutoMood(uint32_t from) { nextAutoMoodAt = from + random(12000, 22000); }
static void scheduleFidget(uint32_t from) { nextFidgetAt = from + random(4500, 9000); }
static void scheduleWink(uint32_t from) { nextWinkAt = from + random(8000, 18000); }

static void startBlink(uint32_t now, bool slow) {
  blinking = true;
  blinkStart = now;
  if (slow) { blinkClose = 220; blinkHold = 120; blinkOpen = 420; }
  else { blinkClose = 75; blinkHold = 35; blinkOpen = 160; }
}

static float blinkCurve(int32_t u) {
  if (u < 0) return 0;
  if ((uint32_t)u < blinkClose) return easeIn(u / (float)blinkClose);
  if ((uint32_t)u < blinkClose + blinkHold) return 1;
  const float o = (u - (int32_t)blinkClose - (int32_t)blinkHold) / (float)blinkOpen;
  if (o >= 1) return 0;
  return 1 - easeOut(o);
}

static void startGesture(GestureType type, uint32_t dur, uint32_t now, int side) {
  gesture = type;
  gestureStart = now;
  gestureDur = dur;
  gestureSide = side;
  gAx = gAy = gBx = gBy = 0;
  if (type == G_GLANCE2) {
    gAx = (chance(0.5f) ? -1 : 1) * randf(0.5f, 0.85f);
    gAy = randf(-0.35f, 0.3f);
    gBx = -gAx * randf(0.6f, 1.0f);
    gBy = randf(-0.3f, 0.3f);
  }
}

static void pickFidget(uint32_t now) {
  const bool lonely = since(now, lastInteraction) > (int32_t)LONELY_AFTER_MS;
  const float roll = randf(0, 1);
  if (lonely && roll < 0.12f) { setMood(MOOD_SAD, 2400); return; }
  if (roll < 0.22f) startGesture(G_GLANCE2, random(1500, 2200), now);
  else if (roll < 0.40f) startGesture(G_THINK, random(1000, 1500), now, chance(0.5f) ? -1 : 1);
  else if (roll < 0.55f) startGesture(G_BROW, random(800, 1200), now, chance(0.5f) ? -1 : 1);
  else if (roll < 0.65f) { startBlink(now, false); blinkAgain = true; }
  else if (roll < 0.78f) startGesture(G_WIGGLE, 700, now);
  else if (roll < 0.90f) startGesture(G_STARE, random(1300, 1900), now);
  else startGesture(G_SIGH, 1500, now);
}

static void poke() {
  const uint32_t now = millis();
  touched(now);
  pokeHappyPending = true;
  setMood(MOOD_SURPRISED, 380);
}

static void pokeEye(int side) {
  const uint32_t now = millis();
  touched(now);
  winkEye = side < 0 ? 1 : 2;
  winkStart = now;
  winkDur = 460;
  pokeHappyPending = false;
  setMood(MOOD_ANNOYED, 900);
  glancing = true;
  glanceUntil = now + 900;
  targetLookX = -side * 0.75f;
  targetLookY = -0.35f;
  offVelY -= 40;
}

static void pet(int dir) {
  const uint32_t now = millis();
  touched(now);
  petCount = (since(now, lastPetAt) < 5000) ? petCount + 1 : 1;
  lastPetAt = now;
  glancing = true;
  glanceUntil = now + 800;
  targetLookX = dir * 0.8f;
  targetLookY = 0.05f;
  pokeHappyPending = false;
  if (petCount >= 3) {
    petCount = 0;
    setMood(MOOD_LOVE, 2800);
  } else {
    setMood(MOOD_HAPPY, 1100);
  }
}

static void saveLook() { prefs.putUChar("look", (uint8_t)lookIndex); }

static void setLook(int i, bool animate) {
  i = ((i % LOOK_COUNT) + LOOK_COUNT) % LOOK_COUNT;
  const uint32_t now = millis();
  touched(now);
  if (!animate) { lookIndex = i; saveLook(); return; }
  pendingLook = i;
  startBlink(now, false);
}

static void forceMood(Mood m, uint32_t holdMs) {
  const uint32_t now = millis();
  if (m == MOOD_ASLEEP) { lastInteraction = now - DROWSY_AFTER_MS - 1; fallAsleep(now); return; }
  touched(now);
  pokeHappyPending = false;
  setMood(m, holdMs, m == MOOD_IDLE);
}

static void startWinkNow(uint32_t now) {
  if (blinking || awake != AWAKE) return;
  winkEye = chance(0.6f) ? 1 : 2;
  winkStart = now;
  winkDur = 280;
}

static void update(uint32_t now, float dt) {
  if (awake == AWAKE && since(now, lastInteraction) > (int32_t)DROWSY_AFTER_MS && mood != MOOD_LOVE) {
    awake = DROWSY;
    drowsySince = now;
    gesture = G_NONE;
    pokeHappyPending = false;
    setMood(MOOD_SLEEPY, 0, true);
  }
  if (awake == DROWSY) {
    drowsy = clampf(since(now, drowsySince) / (float)DROWSY_LEN_MS, 0, 1);
    if (drowsy >= 1) fallAsleep(now);
  }
  backlight = lerpf(backlight, awake == ASLEEP ? 0.35f : 1.0f, 0.04f);

  if (mood != MOOD_IDLE && !moodHold && after(now, moodUntil)) {
    if (pokeHappyPending) {
      pokeHappyPending = false;
      setMood(MOOD_HAPPY, 1800);
    } else {
      mood = MOOD_IDLE;
    }
  }

  const bool asleep = awake == ASLEEP;
  if (!blinking && !asleep && after(now, nextBlinkAt) && mood != MOOD_LOVE && mood != MOOD_DIZZY) {
    startBlink(now, mood == MOOD_SLEEPY || awake == DROWSY);
  }
  if (blinking) {
    const int32_t u = since(now, blinkStart);
    blinkL = blinkCurve(u);
    blinkR = blinkCurve(u - 14);
    if (pendingLook >= 0 && u >= (int32_t)blinkClose) {
      lookIndex = pendingLook;
      pendingLook = -1;
      saveLook();
    }
    if (u >= (int32_t)(blinkClose + blinkHold + blinkOpen + 14)) {
      blinking = false;
      blinkL = blinkR = 0;
      openPop = 1;
      if (blinkAgain) { blinkAgain = false; startBlink(now + 60, false); }
      else scheduleBlink(now);
    }
  } else if (asleep) {
    blinkL = lerpf(blinkL, 1, 0.06f);
    blinkR = lerpf(blinkR, 1, 0.06f);
  } else {
    blinkL = blinkR = 0;
  }
  openPop *= 0.82f;

  if (winkEye == 0 && after(now, nextWinkAt) && mood == MOOD_IDLE && !blinking && awake == AWAKE) {
    startWinkNow(now);
  }
  float winkAmt = 0;
  if (winkEye != 0) {
    const float u = since(now, winkStart) / (float)winkDur;
    winkAmt = u < 0.35f ? easeIn(u / 0.35f) : 1 - easeOut((u - 0.35f) / 0.65f);
    if (u >= 1) { winkEye = 0; winkAmt = 0; scheduleWink(now); }
  }

  const bool canIdle = mood == MOOD_IDLE && awake == AWAKE && gesture == G_NONE;
  if (!glancing && canIdle && after(now, nextGlanceAt)) {
    glancing = true;
    glanceUntil = now + random(700, 1400);
    targetLookX = (chance(0.5f) ? -1 : 1) * randf(0.45f, 0.85f);
    targetLookY = randf(-0.3f, 0.35f);
  }
  if (glancing && after(now, glanceUntil)) {
    glancing = false;
    targetLookX = 0;
    targetLookY = 0;
    scheduleGlance(now);
  }
  if (canIdle && !glancing && after(now, nextFidgetAt)) {
    pickFidget(now);
    scheduleFidget(now);
  }
  if (canIdle && after(now, nextAutoMoodAt)) {
    const float roll = randf(0, 1);
    if (roll < 0.40f) setMood(MOOD_HAPPY, 2200);
    else if (roll < 0.70f) setMood(MOOD_CURIOUS, 2000);
    else if (roll < 0.82f) setMood(MOOD_EXCITED, 1600);
    else if (roll < 0.90f) setMood(MOOD_DIZZY, 1800);
    else setMood(MOOD_SLEEPY, 2800);
    scheduleAutoMood(now);
  }
  if (gesture != G_NONE && since(now, gestureStart) >= (int32_t)gestureDur) gesture = G_NONE;

  if (after(now, nextMicroAt)) {
    microX = randf(-0.05f, 0.05f);
    microY = randf(-0.04f, 0.04f);
    nextMicroAt = now + random(600, 1600);
  }

  float lx = targetLookX;
  float ly = targetLookY;
  bool directLook = false;
  if (mood == MOOD_DIZZY) {
    swirl += 0.20f;
    lx = sinf(swirl) * 0.85f;
    ly = cosf(swirl) * 0.5f;
    directLook = true;
  } else if (mood == MOOD_ASLEEP) {
    lx = 0;
    ly = 0.3f;
  } else if (!glancing) {
    switch (mood) {
      case MOOD_ANNOYED: lx = 0; ly = -0.3f; break;
      case MOOD_CURIOUS: lx = 0.7f; ly = -0.2f; break;
      case MOOD_SAD: lx = 0.15f * tearSide; ly = 0.5f; break;
      case MOOD_SLEEPY: lx = 0.1f; ly = 0.35f + drowsy * 0.2f; break;
      case MOOD_HAPPY: lx = 0; ly = 0.06f; break;
      case MOOD_LOVE: lx = 0; ly = -0.05f; break;
      case MOOD_SURPRISED:
      case MOOD_EXCITED: lx = 0; ly = 0; break;
      default: break;
    }
  }
  const float gu = gesture != G_NONE ? clampf(since(now, gestureStart) / (float)gestureDur, 0, 1) : 0;
  if (gesture != G_NONE) {
    switch (gesture) {
      case G_GLANCE2:
        if (gu < 0.4f) { lx = gAx; ly = gAy; }
        else if (gu < 0.8f) { lx = gBx; ly = gBy; }
        else { lx = 0; ly = 0; }
        break;
      case G_THINK: lx = -0.45f * gestureSide; ly = -0.55f; break;
      case G_STARE: lx = 0; ly = -0.04f; break;
      case G_SIGH: ly = gu < 0.6f ? 0.35f : 0; lx = 0; break;
      case G_BROW: lx = 0.3f * gestureSide; ly = -0.1f; break;
      case G_EYEROLL: {
        const float a = gu * PI * 1.6f;
        lx = sinf(a) * 0.7f;
        ly = -fabsf(cosf(a)) * 0.6f;
        break;
      }
      default: break;
    }
  }
  if (mood == MOOD_IDLE) { lx += microX; ly += microY; }

  if (directLook) {
    lookX = lerpf(lookX, lx, 0.3f);
    lookY = lerpf(lookY, ly, 0.3f);
    velX = velY = 0;
  } else {
    const float K = 260, D = 14;
    velX += (lx - lookX) * K * dt;
    velY += (ly - lookY) * K * dt;
    const float damp = expf(-D * dt);
    velX *= damp;
    velY *= damp;
    lookX += velX * dt;
    lookY += velY * dt;
  }

  Shape L, R;
  float offYTarget = 0;
  float wantBlush = 0;
  float rate = 0.16f;
  switch (mood) {
    case MOOD_HAPPY:
      L.bot = R.bot = 0.58f; L.top = R.top = 0.04f; L.pupil = R.pupil = 0.92f;
      offYTarget = -2; wantBlush = 1;
      break;
    case MOOD_SLEEPY:
      L.top = R.top = 0.42f + drowsy * 0.3f; L.curve = R.curve = 0.3f;
      L.bot = R.bot = 0.06f; L.pupil = R.pupil = 0.95f;
      break;
    case MOOD_CURIOUS: {
      Shape &wide = lookX >= 0 ? R : L;
      Shape &narrow = lookX >= 0 ? L : R;
      wide.sx = 1.06f; wide.sy = 1.09f; narrow.top = 0.28f;
      break;
    }
    case MOOD_SURPRISED:
      L.sx = R.sx = 1.12f; L.sy = R.sy = 1.16f; L.top = R.top = -0.04f;
      L.pupil = R.pupil = 0.42f; offYTarget = -4; rate = 0.34f;
      break;
    case MOOD_LOVE: L.bot = R.bot = 0.22f; wantBlush = 1; break;
    case MOOD_DIZZY: L.top = R.top = 0.18f; L.bot = R.bot = 0.08f; L.pupil = R.pupil = 0.8f; wantBlush = 0.5f; break;
    case MOOD_ANNOYED: L.top = R.top = 0.40f; L.ang = R.ang = 0.65f; L.pupil = R.pupil = 0.9f; break;
    case MOOD_SAD:
      L.top = R.top = 0.26f; L.ang = R.ang = -0.55f; L.curve = R.curve = 0.15f;
      L.pupil = R.pupil = 1.05f; offYTarget = 3;
      break;
    case MOOD_EXCITED:
      L.sx = R.sx = 1.05f; L.sy = R.sy = 1.05f; L.bot = R.bot = 0.22f;
      L.pupil = R.pupil = 1.25f; wantBlush = 0.8f;
      break;
    case MOOD_ASLEEP: L.top = R.top = 0.3f; L.curve = R.curve = 0.3f; break;
    default: break;
  }
  const float lean = 1 + 0.04f * fabsf(lookX);
  L.sx *= lean;
  R.sx *= lean;

  float gOffX = 0;
  if (gesture != G_NONE) {
    const float env = sinf(PI * gu);
    switch (gesture) {
      case G_THINK: L.top += 0.3f * env; R.top += 0.3f * env; break;
      case G_BROW: {
        Shape &up = gestureSide > 0 ? R : L;
        Shape &down = gestureSide > 0 ? L : R;
        up.sy *= 1 + 0.12f * env; up.sx *= 1 + 0.04f * env; down.top += 0.24f * env;
        break;
      }
      case G_WIGGLE:
        gOffX = sinf(gu * PI * 4) * 3.5f * env;
        L.bot += 0.22f * env; R.bot += 0.22f * env;
        break;
      case G_STARE:
        L.sx *= 1 + 0.03f * env; R.sx *= 1 + 0.03f * env;
        L.sy *= 1 + 0.04f * env; R.sy *= 1 + 0.04f * env;
        break;
      case G_SIGH: {
        const float e = gu < 0.6f ? (gu / 0.6f) : 1 - easeOut((gu - 0.6f) / 0.4f);
        L.top += 0.32f * e; R.top += 0.32f * e; L.bot += 0.06f * e; R.bot += 0.06f * e;
        break;
      }
      case G_STRETCH:
        L.sx *= 1 + 0.07f * env; R.sx *= 1 + 0.07f * env;
        L.sy *= 1 + 0.13f * env; R.sy *= 1 + 0.13f * env;
        L.top -= 0.06f * env; R.top -= 0.06f * env;
        break;
      case G_EYEROLL: L.top += 0.15f * env; R.top += 0.15f * env; break;
      default: break;
    }
  }
  if (mood == MOOD_ANNOYED && since(now, moodEnteredAt) > 500 && gesture == G_NONE && chance(0.004f)) {
    startGesture(G_EYEROLL, 900, now);
  }

  breath = 0.5f + 0.5f * sinf(now / 1600.0f);
  float wobX = 0, wobY = 0;
  if (mood == MOOD_DIZZY) { wobX = sinf(swirl * 0.9f) * 3; wobY = cosf(swirl * 0.7f) * 2; }
  if (mood == MOOD_EXCITED) wobX = sinf(now / 26.0f) * 1.6f;
  if (mood == MOOD_ASLEEP) wobY = sinf(now / 900.0f) * 1.5f;

  shapeL.sx = lerpf(shapeL.sx, L.sx, rate);
  shapeL.sy = lerpf(shapeL.sy, L.sy, rate);
  shapeL.top = lerpf(shapeL.top, L.top, rate);
  shapeL.ang = lerpf(shapeL.ang, L.ang, rate);
  shapeL.curve = lerpf(shapeL.curve, L.curve, rate);
  shapeL.bot = lerpf(shapeL.bot, L.bot, rate);
  shapeL.pupil = lerpf(shapeL.pupil, L.pupil, rate * 1.4f);
  shapeR.sx = lerpf(shapeR.sx, R.sx, rate);
  shapeR.sy = lerpf(shapeR.sy, R.sy, rate);
  shapeR.top = lerpf(shapeR.top, R.top, rate);
  shapeR.ang = lerpf(shapeR.ang, R.ang, rate);
  shapeR.curve = lerpf(shapeR.curve, R.curve, rate);
  shapeR.bot = lerpf(shapeR.bot, R.bot, rate);
  shapeR.pupil = lerpf(shapeR.pupil, R.pupil, rate * 1.4f);

  const float K2 = 220, D2 = 12;
  offVelY += (offYTarget - offY) * K2 * dt;
  offVelY *= expf(-D2 * dt);
  offY += offVelY * dt;
  offX = lerpf(offX, lookX * PAIR_SHIFT_X + gOffX + wobX, 0.25f);

  blush = lerpf(blush, wantBlush, 0.08f);
  heartbeat += dt * 7;
  if (tear > 0) {
    tear += dt * 0.55f;
    if (mood != MOOD_SAD || tear > 1.6f) tear = 0;
  }

  frameBlinkL = clampf(blinkL + (winkEye == 1 ? winkAmt : 0), 0, 1);
  frameBlinkR = clampf(blinkR + (winkEye == 2 ? winkAmt : 0), 0, 1);
  frameOffX = offX;
  frameOffY = offY + lookY * PAIR_SHIFT_Y + wobY + breath * 0.6f;
  const PupilStyle baseStyle = LOOKS[lookIndex].style;
  frameStyle = mood == MOOD_LOVE ? ST_HEART
             : (mood == MOOD_DIZZY && baseStyle != ST_NONE) ? ST_RING
             : baseStyle;
  const uint8_t *lb = LOOKS[lookIndex].blush;
  blushCol[0] = lb[0];
  blushCol[1] = lb[1];
  blushCol[2] = lb[2];
  const float hb = fmaxf(0, sinf(heartbeat));
  frameHeartPulse = 1 + 0.08f * hb * hb * hb;
}

static inline void markDirty(int y0, int y1) {
  y0 = max(0, y0);
  y1 = min(FB_H - 1, y1);
  if (y0 < dirtyTop) dirtyTop = y0;
  if (y1 > dirtyBot) dirtyBot = y1;
}

static inline uint16_t packColor(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// C6 has no FPU — keep the inner loop integer. Integer sqrt for ellipse spans.
static int isqrt(int n) {
  if (n <= 0) return 0;
  unsigned x = (unsigned)n;
  unsigned r = 0;
  unsigned bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit) {
    unsigned t = r + bit;
    r >>= 1;
    if (x >= t) {
      x -= t;
      r += bit;
    }
    bit >>= 2;
  }
  return (int)r;
}

static void fillEllipse(int cx, int cy, int rx, int ry, uint16_t c) {
  if (rx < 1) rx = 1;
  if (ry < 1) ry = 1;
  const int rx2 = rx * rx;
  const int ry2 = ry * ry;
  const int y0 = max(0, cy - ry);
  const int y1 = min(FB_H - 1, cy + ry);
  markDirty(y0, y1);
  for (int y = y0; y <= y1; y++) {
    const int dy = y - cy;
    const int inner = ry2 - dy * dy;
    if (inner < 0) continue;
    const int half = isqrt((int)((int32_t)rx2 * inner / ry2));
    int x0 = cx - half;
    int x1 = cx + half;
    if (x0 < 0) x0 = 0;
    if (x1 >= FB_W) x1 = FB_W - 1;
    uint16_t *row = fb + y * FB_W + x0;
    for (int x = x0; x <= x1; x++) *row++ = c;
  }
}

static void drawBlush(float cx, float cy, float amt, const uint8_t *col) {
  if (amt < 0.03f) return;
  const int R = (int)lroundf(BLUSH_R * amt);
  if (R < 2) return;
  const int a8 = (int)(amt * 140.0f);
  const uint16_t c = packColor(BG[0] + ((col[0] - BG[0]) * a8 >> 8),
                               BG[1] + ((col[1] - BG[1]) * a8 >> 8),
                               BG[2] + ((col[2] - BG[2]) * a8 >> 8));
  fillEllipse((int)lroundf(cx), (int)lroundf(cy), R + 2, R, c);
}

static void drawTear(float cx, float cy, float amt) {
  if (amt <= 0) return;
  const int x = (int)lroundf(cx);
  const int y = (int)lroundf(cy + amt * 14);
  const uint16_t c = packColor(COL_TEAR[0], COL_TEAR[1], COL_TEAR[2]);
  fillEllipse(x, y, 3, 3, c);
  fillEllipse(x, y - 3, 2, 3, c);
}

static bool pupilHit(PupilStyle style, int ddx, int ddy, int pr, int slitAx, int slitAy, int dotR) {
  const int d2 = ddx * ddx + ddy * ddy;
  switch (style) {
    case ST_ROUND:
      return d2 <= pr * pr;
    case ST_DOT:
      return d2 <= dotR * dotR;
    case ST_SLIT: {
      const int32_t ax2 = slitAx * slitAx;
      const int32_t ay2 = slitAy * slitAy;
      return (int64_t)ddx * ddx * ay2 + (int64_t)ddy * ddy * ax2 <= (int64_t)ax2 * ay2;
    }
    case ST_RING: {
      const int mid = (pr * 4) / 5;
      const int thick = pr / 5 + 1;
      const int outer = mid + thick;
      const int inner = mid - thick;
      const int core = pr / 4;
      return (d2 <= outer * outer && d2 >= inner * inner) || d2 <= core * core;
    }
    case ST_STAR: {
      const int ax = ddx < 0 ? -ddx : ddx;
      const int ay = ddy < 0 ? -ddy : ddy;
      return (ax + ay) <= pr || (ax <= pr / 3 && ay <= pr) || (ay <= pr / 3 && ax <= pr);
    }
    case ST_HEART: {
      const int pr2 = (pr * 2) / 5;
      const int lx = ddx + pr / 3, ly = ddy + pr / 5;
      const int rx = ddx - pr / 3, ry = ddy + pr / 5;
      if (lx * lx + ly * ly <= pr2 * pr2) return true;
      if (rx * rx + ry * ry <= pr2 * pr2) return true;
      const int ax = ddx < 0 ? -ddx : ddx;
      return ddy >= 0 && (ax + ddy) < (pr * 4) / 5;
    }
    default:
      return false;
  }
}

static void drawEye(float cxf, float cyf, const Shape &sh, float blink, int side, const Look &look,
                    const uint8_t *pupilCol) {
  const float sy = sh.sy * (1 - 0.94f * blink) * (1 + 0.05f * openPop);
  const int cx = (int)lroundf(cxf);
  const int cy = (int)lroundf(cyf + blink * EYE_B * 0.10f - openPop * 1.5f);
  int rx = (int)lroundf(EYE_A * sh.sx);
  int ry = (int)lroundf(EYE_B * sy);
  if (rx < 4) rx = 4;
  if (ry < 2) ry = 2;

  const int topBaseQ = (int)lroundf((-1.0f + 2.1f * sh.top) * 256.0f);
  const int topCurveQ = (int)lroundf(sh.curve * 0.5f * 256.0f);
  const int botBaseQ = (int)lroundf((1.0f - 1.35f * sh.bot) * 256.0f);
  const int botCurveQ = (int)lroundf(sh.bot * 0.55f * 256.0f);
  const int angQ = (int)lroundf(sh.ang * 0.55f * (float)side * 256.0f);

  const PupilStyle style = frameStyle;
  const bool pupilOn = blink < 0.75f && style != ST_NONE && pupilCol != nullptr;
  int pr = (int)lroundf(style == ST_HEART ? PUPIL_R * 1.3f * frameHeartPulse : PUPIL_R * sh.pupil);
  if (pr < 3) pr = 3;
  const int slitAx = (int)lroundf(clampf(PUPIL_R * (0.36f + (1 - sh.pupil) * 0.9f), PUPIL_R * 0.18f, PUPIL_R * 0.95f));
  const int slitAy = (int)lroundf(PUPIL_R * 1.3f);
  const int dotR = (int)lroundf(PUPIL_R * 0.36f * clampf(sh.pupil, 0.6f, 1.3f));
  const int px = cx + (int)lroundf(lookX * EYE_A * LOOK_TRAVEL_X);
  const int py = cy + (int)lroundf(lookY * EYE_B * LOOK_TRAVEL_Y);
  const bool shineOn = look.shine && (style == ST_ROUND || style == ST_HEART);
  const uint8_t *pc = style == ST_HEART ? COL_HEART : pupilCol;
  const uint16_t sclera = packColor(look.sclera[0], look.sclera[1], look.sclera[2]);
  const uint16_t pcol = pc ? packColor(pc[0], pc[1], pc[2]) : sclera;
  const uint16_t shine = packColor(COL_SHINE[0], COL_SHINE[1], COL_SHINE[2]);
  const int s1x = px - (pr * 36) / 100, s1y = py - (pr * 38) / 100, sr1 = (pr * 30) / 100;
  const int s2x = px + (pr * 26) / 100, s2y = py + (pr * 26) / 100, sr2 = (pr * 13) / 100;
  const int sr1_2 = sr1 * sr1, sr2_2 = sr2 * sr2;

  const int rx2 = rx * rx;
  const int ry2 = ry * ry;
  const int y0 = max(0, cy - ry - 1);
  const int y1 = min(FB_H - 1, cy + ry + 1);
  markDirty(y0, y1);

  for (int y = y0; y <= y1; y++) {
    const int dy = y - cy;
    const int inner = ry2 - dy * dy;
    if (inner < 0) continue;
    const int half = isqrt((int)((int32_t)rx2 * inner / ry2));
    int x0 = cx - half;
    int x1 = cx + half;
    if (x0 < 0) x0 = 0;
    if (x1 >= FB_W) x1 = FB_W - 1;
    const int nyQ = (dy * 256) / ry;
    uint16_t *row = fb + y * FB_W;
    for (int x = x0; x <= x1; x++) {
      const int nxQ = ((x - cx) * 256) / rx;
      const int nx2 = (nxQ * nxQ) >> 8;
      const int topQ = topBaseQ + ((angQ * nxQ) >> 8) - ((topCurveQ * nx2) >> 8);
      if (nyQ < topQ) continue;
      const int botQ = botBaseQ + ((botCurveQ * nx2) >> 8);
      if (nyQ > botQ) continue;
      uint16_t pix = sclera;
      if (pupilOn && pupilHit(style, x - px, y - py, pr, slitAx, slitAy, dotR)) {
        pix = pcol;
        if (shineOn) {
          const int d1 = (x - s1x) * (x - s1x) + (y - s1y) * (y - s1y);
          const int d2 = (x - s2x) * (x - s2x) + (y - s2y) * (y - s2y);
          if (d1 <= sr1_2 || d2 <= sr2_2) pix = shine;
        }
      }
      row[x] = pix;
    }
  }
}

static void drawFace() {
  const Look &look = LOOKS[lookIndex];
  const uint16_t bg = packColor(BG[0], BG[1], BG[2]);
  const int clear0 = max(0, prevTop) * FB_W;
  const int clearN = (min(FB_H - 1, prevBot) + 1) * FB_W;
  uint32_t *p32 = (uint32_t *)(fb + clear0);
  const uint32_t bg2 = ((uint32_t)bg << 16) | bg;
  const int n32 = (clearN - clear0) >> 1;
  for (int i = 0; i < n32; i++) p32[i] = bg2;
  dirtyTop = FB_H;
  dirtyBot = -1;

  drawBlush(EYE_L_X - BLUSH_DX + frameOffX * 0.5f, BLUSH_Y + frameOffY * 0.5f, blush, blushCol);
  drawBlush(EYE_R_X + BLUSH_DX + frameOffX * 0.5f, BLUSH_Y + frameOffY * 0.5f, blush, blushCol);
  if (tear > 0) {
    const float ex = tearSide < 0 ? EYE_L_X - 36 : EYE_R_X + 36;
    drawTear(ex + frameOffX, EYE_Y + EYE_B * 0.55f + frameOffY, tear);
  }
  const uint8_t *pupilL = look.hasPupil ? look.pupil : nullptr;
  const uint8_t *pupilR = look.hasPupil2 ? look.pupil2 : pupilL;
  drawEye(EYE_L_X + frameOffX, EYE_Y + frameOffY, shapeL, frameBlinkL, +1, look, pupilL);
  drawEye(EYE_R_X + frameOffX, EYE_Y + frameOffY, shapeR, frameBlinkR, -1, look, pupilR);

  if (dirtyBot < dirtyTop) { dirtyTop = 0; dirtyBot = 0; }
  const int top = min(prevTop, dirtyTop);
  const int bot = max(prevBot, dirtyBot);
  gfx->draw16bitRGBBitmap(0, top, fb + top * FB_W, FB_W, bot - top + 1);
  prevTop = dirtyTop;
  prevBot = dirtyBot;
}

static void applyBacklight() {
  const int pwm = (int)(C6_BL_PWM * clampf(backlight, 0.25f, 1.0f));
  analogWrite(C6_LCD_BL_PIN, pwm);
}

static void pulseLed(uint32_t now) {
  const Look &L = LOOKS[lookIndex];
  const uint8_t *c = mood == MOOD_LOVE ? COL_HEART : L.sclera;
  const float u = 0.45f + 0.55f * (0.5f + 0.5f * sinf(now / 420.0f));
  const float pop = 1.0f + 0.35f * openPop;
  rgb.setPixelColor(0, rgb.Color((uint8_t)(c[0] * u * pop), (uint8_t)(c[1] * u * pop),
                                 (uint8_t)(c[2] * u * pop)));
  rgb.show();
}

static Mood moodByName(const String &s) {
  for (int i = 0; i < MOOD_COUNT; i++) {
    if (s.equalsIgnoreCase(MOOD_NAMES[i])) return (Mood)i;
  }
  return MOOD_COUNT;
}

static void printStatus() {
  Serial.printf("mood=%s look=%s(%d) awake=%s look=%.2f,%.2f wink=%u\n",
                MOOD_NAMES[mood], LOOKS[lookIndex].name, lookIndex,
                awake == AWAKE ? "awake" : (awake == DROWSY ? "drowsy" : "asleep"),
                lookX, lookY, (unsigned)winkEye);
}

static void printHelp() {
  Serial.println(F("slab-eyes — landscape Arduino eyes on ESP32-C6-LCD-1.47"));
  Serial.println(F("  mood <name> | look <n|next|prev|list> | gaze x y"));
  Serial.println(F("  blink | wink | poke | pokeeye [left|right] | pet | sleep | wake"));
  Serial.println(F("  status | help"));
  Serial.print(F("moods:"));
  for (int i = 0; i < MOOD_COUNT; i++) Serial.printf(" %s", MOOD_NAMES[i]);
  Serial.println();
  Serial.println(F("Always landscape. No IMU — it looks around and winks on its own."));
  Serial.println(F("BOOT short = poke. Hold BOOT = next look."));
}

static void handleSerialLine(String line) {
  line.trim();
  if (!line.length()) return;
  const uint32_t now = millis();
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  arg.trim();
  cmd.toLowerCase();

  if (cmd == "mood") {
    const Mood m = moodByName(arg);
    if (m == MOOD_COUNT) Serial.println(F("unknown mood"));
    else forceMood(m, 8000);
  } else if (cmd == "look") {
    if (arg == "next") setLook(lookIndex + 1, true);
    else if (arg == "prev") setLook(lookIndex - 1, true);
    else if (arg == "list") {
      for (int i = 0; i < LOOK_COUNT; i++) Serial.printf("%d %s\n", i, LOOKS[i].name);
    } else {
      int found = -1;
      for (int i = 0; i < LOOK_COUNT; i++) {
        if (arg.equalsIgnoreCase(LOOKS[i].name)) found = i;
      }
      setLook(found >= 0 ? found : arg.toInt(), true);
    }
  } else if (cmd == "gaze") {
    int sp2 = arg.indexOf(' ');
    glancing = true;
    glanceUntil = now + 2500;
    targetLookX = clampf(arg.substring(0, sp2 < 0 ? arg.length() : sp2).toFloat(), -1, 1);
    targetLookY = sp2 < 0 ? 0 : clampf(arg.substring(sp2 + 1).toFloat(), -1, 1);
    touched(now);
  } else if (cmd == "blink") {
    startBlink(now, false);
  } else if (cmd == "wink") {
    startWinkNow(now);
  } else if (cmd == "sleep") {
    forceMood(MOOD_ASLEEP, 0);
  } else if (cmd == "wake") {
    touched(now);
  } else if (cmd == "poke") {
    poke();
  } else if (cmd == "pokeeye") {
    pokeEye(arg == "right" ? 1 : -1);
  } else if (cmd == "pet") {
    pet(1);
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "help" || cmd == "?") {
    printHelp();
  } else {
    Serial.println(F("? (try help)"));
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleSerialLine(g_line);
      g_line = "";
    } else if (g_line.length() < 64) {
      g_line += c;
    }
  }
}

static void pollBoot() {
  const bool down = digitalRead(C6_BOOT_PIN) == LOW;
  const uint32_t now = millis();
  if (down && !g_bootDown) {
    g_bootDown = true;
    g_bootLong = false;
    g_bootDownAt = now;
  }
  if (down && g_bootDown && !g_bootLong && (now - g_bootDownAt) >= BOOT_LONG_MS) {
    g_bootLong = true;
    setLook(lookIndex + 1, true);
    Serial.printf("look %s\n", LOOKS[(lookIndex + 1) % LOOK_COUNT].name);
  }
  if (!down && g_bootDown) {
    if (!g_bootLong && (now - g_bootDownAt) > 40) poke();
    g_bootDown = false;
  }
}

static int defaultLookFromChip() {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6; i++) {
    h ^= (uint8_t)(mac >> (8 * i));
    h *= 16777619u;
  }
  return (int)(h % LOOK_COUNT);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("slab-eyes"));

  pinMode(C6_BOOT_PIN, INPUT_PULLUP);
  pinMode(C6_LCD_BL_PIN, OUTPUT);
  analogWrite(C6_LCD_BL_PIN, C6_BL_PWM);

  if (!gfx->begin(40000000)) Serial.println(F("LCD init failed"));
  gfx->setRotation(1);
  gfx->fillScreen(packColor(BG[0], BG[1], BG[2]));

  fb = (uint16_t *)malloc(FB_W * FB_H * sizeof(uint16_t));
  if (!fb) {
    Serial.println(F("fb alloc failed"));
    while (true) delay(1000);
  }
  for (int i = 0; i < FB_W * FB_H; i++) fb[i] = packColor(BG[0], BG[1], BG[2]);

  rgb.begin();
  rgb.clear();
  rgb.show();

  prefs.begin("slabeyes", false);
  lookIndex = prefs.getUChar("look", (uint8_t)defaultLookFromChip()) % LOOK_COUNT;
  Serial.printf("look: %s (%d)  heap=%u\n", LOOKS[lookIndex].name, lookIndex, ESP.getFreeHeap());

  randomSeed(esp_random());
  const uint32_t now = millis();
  lastInteraction = now;
  scheduleBlink(now);
  scheduleGlance(now);
  scheduleAutoMood(now);
  scheduleFidget(now);
  scheduleWink(now);
  nextStatsAt = now + 5000;
  printHelp();
}

void loop() {
  static uint32_t lastMs = millis();
  const uint32_t now = millis();
  const float dt = clampf((now - lastMs) / 1000.0f, 0.001f, 0.08f);
  lastMs = now;

  pollBoot();
  pollSerial();
  update(now, dt);
  drawFace();
  applyBacklight();
  if ((frames & 3) == 0) pulseLed(now);

  frames++;
  if (after(now, nextStatsAt)) {
    Serial.printf("fps=%.1f ", frames / 5.0f);
    printStatus();
    frames = 0;
    nextStatsAt = now + 5000;
  }
}
