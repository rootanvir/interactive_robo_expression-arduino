#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>

// === Pins (ESP32-S3 WROOM-1) ===
#define TFT_CS    14
#define TFT_RST   13
#define TFT_DC    21
#define TFT_MOSI  47
#define TFT_SCLK  48
#define LED_PIN   38
#define TOUCH_PIN 15      // love mode trigger
#define VIB_PIN   18      // vibration sensor (fear expression)

// Use hardware SPI ctor (faster). We'll bind pins via SPI.begin().
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- Layout ---
const int16_t W = 160, H = 128;

const uint16_t BG      = ST77XX_BLACK;
const uint16_t FACE    = ST77XX_BLACK;
const uint16_t OUTLINE = ST77XX_CYAN;
const uint16_t EYE     = ST77XX_WHITE;
const uint16_t PUPIL   = ST77XX_BLACK;
const uint16_t MOUTH   = ST77XX_WHITE;

// Face frame
const int16_t headX=8, headY=8, headW=W-16, headH=H-16, headR=14;

// Eyes (big)
const int16_t eyeY=48, eyeLX=48, eyeRX=112, eyeR=18, pupilR=9;

// Mouth geometry
const int16_t mouthX=50, mouthY=70, mouthW=60, mouthH=5, mouthR=5;
// Max mouth thickness used anywhere (smile/love/fear)
const int16_t MOUTH_THICK_MAX = 5;

// Happy by default (∪). If you want frown (∩), set to true.
const bool MOUTH_FLIPPED_VERT = false;

// Love-eye pulse range (kept within eye area to avoid smear)
const int LOVE_SCALE_MIN = 7;
const int LOVE_SCALE_MAX = 10;  // <=10 fits eyeR=18 safely

// Debounce for vibration
const unsigned long VIB_COOLDOWN_MS = 700;
unsigned long lastVibMs = 0;

// ---------- Helpers ----------
static inline void drawArc(int16_t cx,int16_t cy,int16_t radius,int16_t thickness,
                           int16_t startDeg,int16_t endDeg,uint16_t color){
  float inner = radius - thickness/2.0f;
  float outer = radius + thickness/2.0f;
  for (int a = startDeg; a <= endDeg; a++){
    float rad = a * 3.1415926f/180.0f;
    int x0 = cx + (int)(inner * cosf(rad));
    int y0 = cy + (int)(inner * sinf(rad));
    int x1 = cx + (int)(outer * cosf(rad));
    int y1 = cy + (int)(outer * sinf(rad));
    tft.drawLine(x0,y0,x1,y1,color);
  }
}

// Clear only the head area (not full screen)
void clearHeadArea(){
  const int16_t padX = 6, padY = 12; // small margin
  int16_t x = headX - padX;
  int16_t y = headY - padY;
  int16_t w = headW + padX*2;
  int16_t h = headH + padY*2;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w > 0 && h > 0) tft.fillRect(x, y, w, h, BG);
}

// Eye region clear box (big enough for max heart or eye redraw)
void clearEyeBox(int16_t cx, int16_t cy, int M){
  int16_t L  = cx - (2*M + 2);
  int16_t T  = cy - ((3*M)/2 + 2);
  int16_t Wb = 4*M + 4;
  int16_t Hb = (7*M)/2 + 4;      // ≈ 3.5*M + 4

  if (L < 0)      { Wb += L; L = 0; }
  if (T < 0)      { Hb += T; T = 0; }
  if (L + Wb > W) Wb = W - L;
  if (T + Hb > H) Hb = H - T;

  if (Wb > 0 && Hb > 0) tft.fillRect(L, T, Wb, Hb, FACE);
}

// Brow band (for clearing before redrawing brows)
void clearBrowBand(){
  int16_t top = eyeY - eyeR - 26;
  int16_t height = 18;
  if (top < headY+2) { height -= (headY+2 - top); top = headY+2; }
  int16_t left  = headX+2;
  int16_t right = headX + headW - 2;
  if (height > 0) tft.fillRect(left, top, right - left, height, FACE);
}

// Full mouth clear that covers the *entire* smile arc and fear "O"
void clearMouthArea(){
  const int cx = mouthX + mouthW/2;
  const int cy = mouthY + mouthH/2;
  const int r  = mouthW/2 - 4;               // same radius used in smile()
  const int thick = MOUTH_THICK_MAX;          // worst-case thickness
  int16_t left   = mouthX - 4;
  int16_t right  = mouthX + mouthW + 4;
  int16_t top    = cy - thick - 2;            // arc can draw a bit above cy
  int16_t bottom = cy + r + thick + 2;        // bottom of smile ∪

  if (left < 0) left = 0;
  if (right > W) right = W;
  if (top < headY+2) top = headY+2;
  if (bottom > headY + headH - 2) bottom = headY + headH - 2;

  int16_t width  = right - left;
  int16_t height = bottom - top;
  if (width > 0 && height > 0) tft.fillRect(left, top, width, height, FACE);
}

// Heads
void drawHeadRect(){
  clearHeadArea();
  tft.drawRoundRect(headX, headY, headW, headH, headR, OUTLINE);
  tft.fillRoundRect(headX+2, headY+2, headW-4, headH-4, headR-2, FACE);
  int ax=W/2;
  tft.drawLine(ax, headY-4, ax, headY+6, OUTLINE);
  tft.fillCircle(ax, headY-6, 3, OUTLINE);
}

// (kept for completeness; not used here)
void drawHeadRound(){
  clearHeadArea();
  int cx=W/2, cy=H/2, r=min(W,H)/2 - 6;
  tft.drawCircle(cx,cy,r,OUTLINE);
  tft.fillCircle(cx,cy,r-2,FACE);
  tft.drawLine(cx, cy-r-4, cx, cy-r+6, OUTLINE);
  tft.fillCircle(cx, cy-r-6, 3, OUTLINE);
}

// Features
void drawSmile(uint16_t color){
  clearMouthArea();  // <<< full-area clear (prevents old arcs from lingering)

  const int cx = mouthX + mouthW/2;
  const int cy = mouthY + mouthH/2;
  const int r  = mouthW/2 - 4;

  // TFT Y grows downward → happy (∪) is 20..160; frown (∩) is 200..340
  const int startDeg = MOUTH_FLIPPED_VERT ? 200 :  20;
  const int endDeg   = MOUTH_FLIPPED_VERT ? 340 : 160;

  for (int t=0; t<3; ++t){
    for (int deg=startDeg; deg<=endDeg; deg+=2){
      float rad = deg * 3.1415926f/180.0f;
      int x = cx + (int)(r * cosf(rad));
      int y = cy + (int)(r * sinf(rad)) + t;
      tft.drawPixel(x,y,color);
    }
  }
}

void drawEye(int16_t cx,int16_t cy,bool open){
  // avoid std::max type mismatch: cast to int
  int clearM = ((int)eyeR > LOVE_SCALE_MAX) ? (int)eyeR : LOVE_SCALE_MAX;
  clearEyeBox(cx, cy, clearM);

  if (open){
    tft.fillCircle(cx, cy, eyeR, EYE);
    tft.fillCircle(cx, cy-2, pupilR, PUPIL);
    tft.fillCircle(cx-4, cy-6, 3, ST77XX_WHITE);
  }else{
    tft.fillCircle(cx, cy, eyeR, EYE);                  // white lid
    tft.fillRoundRect(cx-eyeR, cy-1, eyeR*2, 4, 2, EYE);
  }
}

void drawFace(bool eyesOpen, bool roundHead){
  if (roundHead) drawHeadRound(); else drawHeadRect();
  drawEye(eyeLX, eyeY, eyesOpen);
  drawEye(eyeRX, eyeY, eyesOpen);
  drawSmile(MOUTH);
}

// ---------- Love-eyes helpers ----------
void drawHeartAt(int16_t cx,int16_t cy,int scale,uint16_t color){
  // draw heart shape only (no clearing here)
  tft.fillCircle (cx - scale, cy - scale/2, scale, color);
  tft.fillCircle (cx + scale, cy - scale/2, scale, color);
  tft.fillTriangle(cx - scale*2, cy - scale/4,
                   cx + scale*2, cy - scale/4,
                   cx,            cy + scale*2, color);
}

// LOVE MODE: rectangular head + pulsing love eyes + happy mouth (4s)
void drawLoveEyesAnimatedRoundHead(){  // keeps rectangular head
  const uint16_t heartColor = ST77XX_RED;
  unsigned long start = millis();

  // Draw rect head once; then update only eyes/mouth regions
  drawHeadRect();

  while (millis() - start < 4000UL){
    float t = (millis() - start) / 200.0f;

    // Smooth pulse in [LOVE_SCALE_MIN .. LOVE_SCALE_MAX]
    float u = (sinf(t) * 0.5f) + 0.5f;  // 0..1
    int scale = LOVE_SCALE_MIN + (int)(u * (LOVE_SCALE_MAX - LOVE_SCALE_MIN) + 0.5f);

    // Clear eye boxes for the largest heart every frame (no trails)
    clearEyeBox(eyeLX, eyeY, LOVE_SCALE_MAX);
    clearEyeBox(eyeRX, eyeY, LOVE_SCALE_MAX);

    // Draw hearts
    drawHeartAt(eyeLX, eyeY, scale, heartColor);
    drawHeartAt(eyeRX, eyeY, scale, heartColor);

    // Mouth (use full-area clear so smile artifacts never remain)
    int cx = mouthX + mouthW/2;
    int cy = mouthY + mouthH/2 + 1;
    int r  = mouthW/2 - 3;
    int thick = 2 + (int)((sinf(t) + 1.0f) * 1.5f); // 2..5
    clearMouthArea();
    drawArc(cx, cy, r, thick, 20, 160, MOUTH); // happy ∪

    delay(28); // ~35 FPS
  }

  // Hard refresh of head area to ensure nothing remains
  drawFace(true, false);
}

// ---------- Fear (scared) expression ----------
void drawScaredBrows(){
  // raised / slanted brows above eyes
  tft.drawLine(eyeLX - 10, eyeY - 18, eyeLX + 6,  eyeY - 22, EYE);
  tft.drawLine(eyeRX - 6,  eyeY - 22, eyeRX + 10, eyeY - 18, EYE);
}

void showScaredExpression(uint16_t duration_ms){
  unsigned long start = millis();

  while (millis() - start < duration_ms){
    // Clear regions first (prevents ghosting)
    clearEyeBox(eyeLX, eyeY, eyeR);
    clearEyeBox(eyeRX, eyeY, eyeR);
    clearBrowBand();
    clearMouthArea();  // <<< full-area clear ensures no half-moon remains

    // eyes: big whites + tiny pupils (slightly up for "shock")
    tft.fillCircle(eyeLX, eyeY, eyeR, EYE);
    tft.fillCircle(eyeRX, eyeY, eyeR, EYE);
    tft.fillCircle(eyeLX, eyeY - 3, 4, PUPIL);
    tft.fillCircle(eyeRX, eyeY - 3, 4, PUPIL);

    // brows
    drawScaredBrows();

    // mouth "O" (ring)
    drawArc(mouthX + mouthW/2, mouthY + mouthH/2 + 1, 6, 3, 0, 359, MOUTH);

    delay(40);
  }

  // Solid refresh of head area
  drawFace(true, false);
}

// ---------- Blink state ----------
unsigned long lastBlink=0;
bool eyesOpenState=true;
int blinkPhase=0;

void setup(){
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(TOUCH_PIN, INPUT);   // if active-low: INPUT_PULLUP & check LOW
  pinMode(VIB_PIN,   INPUT);   // set INPUT_PULLUP & invert if your module is active-low

  Serial.begin(115200);

  // Bind custom pins to hardware SPI (fast)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);

  // Draw BG once; avoid fillScreen() during transitions
  tft.fillRect(0, 0, W, H, BG);

  drawFace(true, false);
}

void loop(){
  unsigned long now = millis();

  // 🔔 Vibration → FEAR expression (debounced)
  if (digitalRead(VIB_PIN) == HIGH && (now - lastVibMs) > VIB_COOLDOWN_MS){
    lastVibMs = now;
    showScaredExpression(1200);   // ~1.2s scared look
    return; // skip touch/blink this loop
  }

  // ❤️ Touch → love mode (rect head + pulsing hearts)
  if (digitalRead(TOUCH_PIN) == HIGH){
    drawLoveEyesAnimatedRoundHead();
    return; // skip blink during love animation
  }

  // 👀 Blink animation (only eyes)
  if (blinkPhase == 0){
    if (now - lastBlink > 2000 + (now % 3000)) blinkPhase = 1;
  }
  if (blinkPhase == 1){
    eyesOpenState = false;
    drawEye(eyeLX, eyeY, false);
    drawEye(eyeRX, eyeY, false);
    blinkPhase = 2;
    lastBlink = now;
  }else if (blinkPhase == 2){
    if (now - lastBlink > 120){ blinkPhase = 3; lastBlink = now; }
  }else if (blinkPhase == 3){
    eyesOpenState = true;
    drawEye(eyeLX, eyeY, true);
    drawEye(eyeRX, eyeY, true);
    blinkPhase = 0;
    lastBlink = now;
  }

  delay(10);
}
