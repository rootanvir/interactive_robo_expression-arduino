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
#define TOUCH_PIN 15

// Use **hardware SPI** ctor (faster). We'll bind custom pins in setup() via SPI.begin().
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- Layout ---
const int16_t W = 160, H = 128;

const uint16_t BG      = ST77XX_BLACK;
const uint16_t FACE    = ST77XX_BLACK;
const uint16_t OUTLINE = ST77XX_CYAN;
const uint16_t EYE     = ST77XX_WHITE;
const uint16_t PUPIL   = ST77XX_BLACK;
const uint16_t MOUTH   = ST77XX_WHITE;

const int16_t headX=8, headY=8, headW=W-16, headH=H-16, headR=14;

// Eyes (big)
const int16_t eyeY=48, eyeLX=48, eyeRX=112, eyeR=18, pupilR=9;

// Mouth box
const int16_t mouthX=50, mouthY=70, mouthW=60, mouthH=5, mouthR=5;

// Happy by default (∪). If you want frown (∩), set to true.
const bool MOUTH_FLIPPED_VERT = false;

// --- Fast helpers ---
static inline void drawArc(int16_t cx,int16_t cy,int16_t radius,int16_t thickness,
                           int16_t startDeg,int16_t endDeg,uint16_t color){
  float inner = radius - thickness/2.0f;
  float outer = radius + thickness/2.0f;
  for (int a = startDeg; a <= endDeg; a++){
    float rad = a * (3.1415926f/180.0f);
    int x0 = cx + (int)(inner * cosf(rad));
    int y0 = cy + (int)(inner * sinf(rad));
    int x1 = cx + (int)(outer * cosf(rad));
    int y1 = cy + (int)(outer * sinf(rad));
    tft.drawLine(x0,y0,x1,y1,color);
  }
}

// Clear only the area that contains head + antenna (no full-screen wipe)
void clearHeadArea(){
  int16_t padX = 6, padY = 12; // small margin
  tft.fillRect(headX - padX, headY - padY, headW + padX*2, headH + padY*2, BG);
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
  // Clear mouth box only
  tft.fillRoundRect(mouthX, mouthY, mouthW, mouthH, mouthR, FACE);

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
  if (open){
    tft.fillCircle(cx, cy, eyeR, EYE);
    tft.fillCircle(cx, cy-2, pupilR, PUPIL);
    tft.fillCircle(cx-4, cy-6, 3, ST77XX_WHITE);
  }else{
    tft.fillCircle(cx, cy, eyeR+1, FACE);
    tft.fillRoundRect(cx-eyeR, cy-1, eyeR*2, 4, 2, EYE);
  }
}

void drawFace(bool eyesOpen, bool roundHead){
  if (roundHead) drawHeadRound(); else drawHeadRect();
  drawEye(eyeLX, eyeY, eyesOpen);
  drawEye(eyeRX, eyeY, eyesOpen);
  drawSmile(MOUTH);
}

// Heart helper (updates only the eye’s region)
void drawHeartAt(int16_t cx,int16_t cy,int scale,uint16_t color){
  tft.fillCircle(cx, cy, eyeR+3, FACE); // clear eye region
  tft.fillCircle(cx - scale, cy - scale/2, scale, color);
  tft.fillCircle(cx + scale, cy - scale/2, scale, color);
  tft.fillTriangle(cx - scale*2, cy - scale/4,
                   cx + scale*2, cy - scale/4,
                   cx, cy + scale*2, color);
}

// ---------- LOVE MODE: rectangular head + pulsing love eyes + happy mouth (4s) ----------
// Only the head area boxes (eyes/mouth) are touched; no full-screen clears.
void drawLoveEyesAnimatedRoundHead(){  // keep the same name to avoid changing other code
  const uint16_t heartColor = ST77XX_RED;
  unsigned long start = millis();

  // Ensure the rectangular head stays on screen
  drawHeadRect(); // draw once; we'll only update eyes/mouth in the loop

  while (millis() - start < 4000UL){
    float t = (millis() - start) / 200.0f;
    int scale = 7 + (int)((sinf(t) + 1.0f) * 2.5f);  // 7..12 pulse

    // Eyes (only repaint eye regions)
    drawHeartAt(eyeLX, eyeY, scale, heartColor);
    drawHeartAt(eyeRX, eyeY, scale, heartColor);

    // Mouth (only repaint mouth box)
    int cx = mouthX + mouthW/2;
    int cy = mouthY + mouthH/2 + 1;
    int r  = mouthW/2 - 3;
    int thick = 2 + (int)((sinf(t) + 1.0f) * 1.5f); // 2..5
    tft.fillRoundRect(mouthX, mouthY, mouthW, mouthH, mouthR, FACE);
    drawArc(cx, cy, r, thick, 20, 160, MOUTH); // happy ∪

    delay(28); // ~35 FPS
  }

  // Refresh normal face (rectangular head)
  drawFace(true, false);
}


// Blink state
unsigned long lastBlink=0;
bool eyesOpenState=true;
int blinkPhase=0;

void setup(){
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(TOUCH_PIN, INPUT);  // if active-low: INPUT_PULLUP and check LOW

  Serial.begin(115200);

  // Bind custom pins to **hardware SPI** (much faster than software SPI)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);

  // Draw a single BG once; we won't use fillScreen() again in transitions
  tft.fillRect(0, 0, W, H, BG);

  drawFace(true, false);
}

void loop(){
  unsigned long now = millis();

  // Touch → love mode (round head + pulsing hearts)
  if (digitalRead(TOUCH_PIN) == HIGH){
    drawLoveEyesAnimatedRoundHead();
    return; // skip blink during love animation
  }

  // Blink animation (only eyes)
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
