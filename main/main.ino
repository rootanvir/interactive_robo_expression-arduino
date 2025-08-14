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

// --- Mouth geometry ---
// Keep the same arc width/shape, but use a lower Y only for the normal face.
const int16_t mouthX=50, mouthW=60, mouthH=5, mouthR=5;
const int16_t mouthY_ANIM = 80;   // used in love/fear animations (unchanged)
const int16_t mouthY_OK   = 78;   // a little lower for the normal/OK face
const int16_t MOUTH_THICK_MAX = 5;

// Happy by default (∪). If you want frown (∩), set to true.
const bool MOUTH_FLIPPED_VERT = false;

// Love-eye pulse range (kept within eye area to avoid smear)
const int LOVE_SCALE_MIN = 7;
const int LOVE_SCALE_MAX = 10;  // <=10 fits eyeR=18 safely

// Debounce for vibration
const unsigned long VIB_COOLDOWN_MS = 700;
unsigned long lastVibMs = 0;

// ====== X-eye tuning ======
const uint8_t XEYE_THICK     = 5;   // stroke thickness
const int     XEYE_EXTEND_X  = 6;   // widen X horizontally
const int     XEYE_EXTEND_Y  = -2;  // reduce vertical length (negative = shorter Y)

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

// Eye region clear box (big enough for max heart / X-eye)
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

// Full mouth clear that covers the *entire* smile arc and fear "O"
// Parameterized by mouth Y so we can clear the correct row.
void clearMouthAreaAt(int16_t mouthYParam){
  const int cx = mouthX + mouthW/2;
  const int cy = mouthYParam + mouthH/2;
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
void drawSmileAt(int16_t mouthYParam, uint16_t color){
  clearMouthAreaAt(mouthYParam);

  const int cx = mouthX + mouthW/2;
  const int cy = mouthYParam + mouthH/2;
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
  // Normal/OK smile is a bit lower now:
  drawSmileAt(mouthY_OK, MOUTH);
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

  drawHeadRect(); // draw once

  while (millis() - start < 4000UL){
    float t = (millis() - start) / 200.0f;
    float u = (sinf(t) * 0.5f) + 0.5f;  // 0..1
    int scale = LOVE_SCALE_MIN + (int)(u * (LOVE_SCALE_MAX - LOVE_SCALE_MIN) + 0.5f);

    // Eyes
    clearEyeBox(eyeLX, eyeY, LOVE_SCALE_MAX);
    clearEyeBox(eyeRX, eyeY, LOVE_SCALE_MAX);
    drawHeartAt(eyeLX, eyeY, scale, heartColor);
    drawHeartAt(eyeRX, eyeY, scale, heartColor);

    // Mouth (use animation row, unchanged)
    int cx = mouthX + mouthW/2;
    int cy = mouthY_ANIM + mouthH/2 + 1;
    int r  = mouthW/2 - 3;
    int thick = 2 + (int)((sinf(t) + 1.0f) * 1.5f); // 2..5
    clearMouthAreaAt(mouthY_ANIM);
    drawArc(cx, cy, r, thick, 20, 160, MOUTH); // happy ∪

    delay(28); // ~35 FPS
  }

  drawFace(true, false);
}

// ====== Proper thick, wide diagonals for X-eye ======
void drawThickLineWide(int x0, int y0, int x1, int y1, uint8_t thick, uint16_t color){
  int dx = x1 - x0;
  int dy = y1 - y0;
  int adx = abs(dx), ady = abs(dy);
  int denom = (adx > ady) ? adx : ady;   // approximate normalization
  if (denom == 0) denom = 1;

  // perpendicular (nx, ny) ~ (-dy, dx) / denom
  for (int k = -(int)thick/2; k <= (int)thick/2; ++k){
    int offx = (-dy * k) / denom;
    int offy = ( dx * k) / denom;
    tft.drawLine(x0 + offx, y0 + offy, x1 + offx, y1 + offy, color);
  }
}

void drawXEyeWide(int16_t cx, int16_t cy, int16_t baseR, uint16_t color, uint8_t thick){
  // Make the X wider: extend X horizontally (rx) and adjust vertical (ry)
  int rx = baseR + XEYE_EXTEND_X;                // half-length in X (wider)
  int ry = baseR + XEYE_EXTEND_Y;                // half-length in Y (can be shorter)
  if (ry < 6) ry = 6;                             // keep reasonable height

  // Diagonal 1: \  from (cx - rx, cy - ry) to (cx + rx, cy + ry)
  drawThickLineWide(cx - rx, cy - ry, cx + rx, cy + ry, thick, color);
  // Diagonal 2: /  from (cx - rx, cy + ry) to (cx + rx, cy - ry)
  drawThickLineWide(cx - rx, cy + ry, cx + rx, cy - ry, thick, color);
}

// ---------- Fear (scared) expression with wide X X eyes (NO eyebrows) ----------
void showScaredExpression(uint16_t duration_ms){
  unsigned long start = millis();

  while (millis() - start < duration_ms){
    // Clear regions (no ghosting). Use bigger M for wide X.
    int Mclear = eyeR + XEYE_EXTEND_X + XEYE_THICK + 2; // generous box
    clearEyeBox(eyeLX, eyeY, Mclear);
    clearEyeBox(eyeRX, eyeY, Mclear);
    clearMouthAreaAt(mouthY_ANIM);

    // Wide X X eyes (no eyebrows drawn)
    drawXEyeWide(eyeLX, eyeY, eyeR - 2, EYE, XEYE_THICK);
    drawXEyeWide(eyeRX, eyeY, eyeR - 2, EYE, XEYE_THICK);

    // round "O" mouth on animation row
    drawArc(mouthX + mouthW/2, mouthY_ANIM + mouthH/2 + 1, 6, 3, 0, 359, MOUTH);

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

  // 🔔 Vibration → FEAR expression (with wide X X eyes, NO eyebrows)
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
  }

  delay(10);
}
