/**
 * MiniLab Oscilloscope - 3-Channel Digital Oscilloscope
 * Hardware: ESP32 "Cheap Yellow Display" (CYD) — 320x240 TFT (ILI9341)
 * Library:  TFT_eSPI
 *
 * Channel Layout:
 *   CH1 (green)  — 5V probe  — voltage divider 2/3 → ADC pin GPIO34
 *   CH2 (pink)   — 5V probe  — voltage divider 2/3 → ADC pin GPIO35
 *   CH3 (orange) — 3.3V probe — direct input       → ADC pin GPIO32
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// XPT2046 sits on VSPI 
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, 255);  // 255 = no IRQ pin wired

// ─────────────────────────────────────────────
//  PIN DEFINITIONS
// ─────────────────────────────────────────────
#define PIN_CH1  35
#define PIN_CH2  34

// ─────────────────────────────────────────────
//  ADC CALIBRATION
//  *** CALIBRATION POINT: adjust ADC_VREF_MV if your 3.3V rail differs ***
// ─────────────────────────────────────────────
#define ADC_MAX         4095.0f
#define ADC_VREF_MV     3300.0f

// *** CALIBRATION POINT: fine-tune if your resistors differ from 10k/20k ***
#define DIVIDER_FACTOR  1.45f     // Inverse of 2/3 divider: recovers 0–5V from 0–3.3V ADC

#define CH1_VMAX  5.0f
#define CH2_VMAX  3.3f

// ─────────────────────────────────────────────
//  DISPLAY & LAYOUT
// ─────────────────────────────────────────────
#define SCREEN_W       320
#define SCREEN_H       240
#define TOP_BAR_H       20
#define LEFT_MARGIN     28

#define PLOT_X          LEFT_MARGIN
#define PLOT_W         (SCREEN_W - LEFT_MARGIN)   // 292px
#define PLOT_H_EACH    ((SCREEN_H - TOP_BAR_H) / 2)  // ~110px each
#define CH1_Y_TOP      (TOP_BAR_H)
#define CH2_Y_TOP      (TOP_BAR_H + PLOT_H_EACH)

// Pause button (shown in top bar — only active while paused)
#define PAUSE_BTN_X    (SCREEN_W - 70)
#define PAUSE_BTN_Y     2
#define PAUSE_BTN_W     65
#define PAUSE_BTN_H     16

// Waveform line thickness (pixels). 2–3 recommended for readability on 320x240.
// *** CALIBRATION POINT: increase for thicker lines, decrease for detail ***
#define WAVE_THICKNESS  2

// ─────────────────────────────────────────────
//  COLORS (RGB565)
// ─────────────────────────────────────────────
#define COL_BG        0x0000
#define COL_GRID      0x2104
#define COL_BORDER    0x4208
#define COL_TEXT      0xef7d
#define COL_CH1       0xf181
#define COL_CH2       0x67a1
#define COL_CH3       0xd6da
#define COL_PAUSE_ON  0xF800
#define COL_PAUSE_OFF 0x07E0
#define COL_LABEL     0xAD55

// ─────────────────────────────────────────────
//  CIRCULAR BUFFER
//  *** CALIBRATION POINT: increase BUF_SIZE for longer scroll history ***
// ─────────────────────────────────────────────
#define BUF_SIZE  1024

uint16_t bufCH1[BUF_SIZE];
uint16_t bufCH2[BUF_SIZE];

volatile uint16_t bufHead = 0;
volatile bool     bufFull = false;

// ─────────────────────────────────────────────
//  SAMPLING TIMING
//  *** CALIBRATION POINT: lower = faster sample rate / narrower time window ***
// ─────────────────────────────────────────────
#define SAMPLE_INTERVAL_US  1000UL
unsigned long lastSampleUs = 0;

// ─────────────────────────────────────────────
//  PAUSE / SCROLL STATE
// ─────────────────────────────────────────────
bool    paused       = false;
int16_t scrollOffset = 0;

// ─────────────────────────────────────────────
//  TOUCH STATE
// ─────────────────────────────────────────────
bool          touchWasDown   = false;
int16_t       touchStartX    = 0;
int16_t       touchStartY    = 0;
int16_t       scrollAtTouch  = 0;
unsigned long touchDownMs    = 0;
uint16_t frozenHead = 0; // The point where we paused

// ─────────────────────────────────────────────
//  TFT + SPRITE
// ─────────────────────────────────────────────
TFT_eSPI    tft      = TFT_eSPI();
TFT_eSprite spriteCH = TFT_eSprite(&tft);

// ─────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────
void    drawStaticUI();
void    drawYLabel(int y, const char* txt, uint16_t color);
void    drawPauseButton();
void    drawChannelStrip(uint16_t* buf, uint16_t head, int16_t scroll,
                         uint16_t color, float vMax, bool is5V, int yTop,
                         const char* label);
void    drawGridLines(TFT_eSprite& spr, int h);
void    drawThickLine(TFT_eSprite& spr, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, uint16_t color, uint8_t thickness);
void    sampleChannels();
void    handleTouch();
float   adcToVoltage(uint16_t raw, bool is5V);

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  // Start touch on the same HSPI bus
  touchSPI.begin(25, 39, 32, 33);  // SCLK, MISO, MOSI, CS
  ts.begin(touchSPI);
  ts.setRotation(3);  // Match TFT rotation
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_CH1, INPUT);
  pinMode(PIN_CH2, INPUT);

  tft.init();
  tft.invertDisplay(true); // Toggles the inversion state
  //tft.setRotation(1);   // Landscape 320×240
  tft.fillScreen(COL_BG);
  tft.setRotation(3); // Rotates the screen 180 degrees



  spriteCH.createSprite(PLOT_W, PLOT_H_EACH);

  memset(bufCH1, 0, sizeof(bufCH1));
  memset(bufCH2, 0, sizeof(bufCH2));

  drawStaticUI();
  lastSampleUs = micros();
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  handleTouch();
  if (!paused){
    sampleChannels();
  }
  // Use the ternary operator to decide which index to draw
  uint16_t headToDraw = paused ? frozenHead : bufHead;

  // Only draw if we have enough data (you can add logic here to refresh only when needed)
  drawChannelStrip(bufCH1, headToDraw, scrollOffset, COL_CH1, CH1_VMAX, true, CH1_Y_TOP, "Channel 1");
  drawChannelStrip(bufCH2, headToDraw, scrollOffset, COL_CH2, CH2_VMAX, true, CH2_Y_TOP, "Channel 2");
  
  drawPauseButton();
}

// ═══════════════════════════════════════════════════════════════
//  NON-BLOCKING SAMPLE
// ═══════════════════════════════════════════════════════════════
void sampleChannels() {
  unsigned long now = micros();
  if (now - lastSampleUs < SAMPLE_INTERVAL_US) return;
  lastSampleUs = now;

  uint16_t r1 = (uint16_t)constrain(analogRead(PIN_CH1), 0, (int)ADC_MAX);
  uint16_t r2 = (uint16_t)constrain(analogRead(PIN_CH2), 0, (int)ADC_MAX);

  bufCH1[bufHead] = r1;
  bufCH2[bufHead] = r2;

  bufHead = (bufHead + 1) % BUF_SIZE;
  if (bufHead == 0) bufFull = true;
}

// ═══════════════════════════════════════════════════════════════
//  TOUCH HANDLING
//
//  PAUSE LOGIC:
//    • Any tap anywhere on screen (< 250ms, < 20px drag) → PAUSE
//    • While paused: tap in top bar → UNPAUSE
//    • While paused: drag horizontally anywhere → SCROLL
// ═══════════════════════════════════════════════════════════════
void handleTouch() {
  uint16_t tx = 0, ty = 0;
  bool currentlyPressed = ts.touched(); // Poll the hardware once

// DEBUGGING: Remove this once you see output
  static bool lastPrint = false;
  if(currentlyPressed != lastPrint) {
     Serial.printf("Touch state changed: %s\n", currentlyPressed ? "PRESSED" : "RELEASED");
     lastPrint = currentlyPressed;
  }

  if (!currentlyPressed) return; // Stop if not touching

  if (currentlyPressed) {
      TS_Point p = ts.getPoint();
      // Mapping logic
      tx = map(p.x, 200, 3700, 0, SCREEN_W);
      ty = map(p.y, 350, 3800, 0, SCREEN_H);
      tx = constrain(tx, 0, SCREEN_W - 1);
      ty = constrain(ty, 0, SCREEN_H - 1);
  }

  // 1. TOUCH START (Transition from Not Pressed -> Pressed)
  if (currentlyPressed && !touchWasDown) {
      touchWasDown = true;
      touchStartX = (int16_t)tx;
      touchStartY = (int16_t)ty;
      scrollAtTouch = scrollOffset;
      touchDownMs = millis();
  } 
  // 2. TOUCH RELEASE (Transition from Pressed -> Not Pressed)
  else if (!currentlyPressed && touchWasDown) {
      touchWasDown = false; // Reset state
      
      unsigned long elapsed = millis() - touchDownMs;
      // We use the last known touch coordinates (tx/ty remain from last poll)
      int16_t dx = (int16_t)tx - touchStartX;
      int16_t dy = (int16_t)ty - touchStartY;
      bool isTap = (elapsed < 250) && (abs(dx) < 20) && (abs(dy) < 20);

      if (isTap) {
          if (!paused) {
              paused = true;
              frozenHead = bufHead;
              scrollOffset = 0;
              drawPauseButton();
          } else if (touchStartY < TOP_BAR_H + 10) {
              paused = false;
              scrollOffset = 0;
              drawPauseButton();
          }
      }
  } 
  // 3. DRAGGING (Currently Pressed and was already pressed)
  else if (currentlyPressed && touchWasDown && paused) {
      int16_t dx = (int16_t)tx - touchStartX;
      int16_t maxScroll = (int16_t)(bufFull ? BUF_SIZE - PLOT_W : max((int)bufHead - PLOT_W, 0));
      scrollOffset = constrain((int16_t)(scrollAtTouch - dx), (int16_t)0, maxScroll);
  }
}

// ═══════════════════════════════════════════════════════════════
//  STATIC UI (called once at startup)
// ═══════════════════════════════════════════════════════════════
void drawStaticUI() {
  tft.fillScreen(COL_BG);

  // Horizontal dividers
  tft.drawFastHLine(0, TOP_BAR_H,                   SCREEN_W, COL_BORDER);
  tft.drawFastHLine(0, TOP_BAR_H + PLOT_H_EACH,     SCREEN_W, COL_BORDER);
  tft.drawFastHLine(0, TOP_BAR_H + 2 * PLOT_H_EACH, SCREEN_W, COL_BORDER);
  tft.drawFastHLine(0, TOP_BAR_H + 3 * PLOT_H_EACH, SCREEN_W, COL_BORDER);

  // Left margin divider
  tft.drawFastVLine(LEFT_MARGIN, TOP_BAR_H, SCREEN_H - TOP_BAR_H, COL_BORDER);

  // Timebase label
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(2, 6);
  tft.print("T-BASE");

  tft.setTextColor(COL_LABEL, COL_BG);
tft.setTextSize(1);


// Calculate the pixel position of each grid line (must match drawGridLines)
int divW = PLOT_W / 5;

// *** CALIBRATION POINT: change unit string and divisor to match your
//     SAMPLE_INTERVAL_US. At 1000us, each division = 58ms. ***
for (int i = 1; i <= 5; i++) { //Skip first label to account for T-BASE text
    int xPixel = PLOT_X + (i * divW);  // screen X of each division marker
    int ms = i * 58;                    // ms value at that division
    if (xPixel < 60) continue;
    tft.setCursor(xPixel - 8, 6);      // -8 to roughly centre the text
    if (ms == 90) continue;
    tft.print(ms);
    tft.print("ms");
}

  // Y-axis peak labels
  drawYLabel(CH1_Y_TOP + 14,      "PEAK",    COL_CH1);
  drawYLabel(CH1_Y_TOP + 28, "5.0V", COL_CH1);
  drawYLabel(CH2_Y_TOP + 14,      "PEAK",    COL_CH2);
  drawYLabel(CH2_Y_TOP + 28, "3.3V", COL_CH2);


  drawPauseButton();
}

void drawYLabel(int y, const char* txt, uint16_t color) {
  tft.setTextColor(color, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(1, y + 2);
  tft.print(txt);
}

// ═══════════════════════════════════════════════════════════════
//  PAUSE BUTTON
// ═══════════════════════════════════════════════════════════════
void drawPauseButton() {
  uint16_t bg = paused ? COL_PAUSE_ON : COL_PAUSE_OFF;
  tft.fillRect(PAUSE_BTN_X, PAUSE_BTN_Y, PAUSE_BTN_W, PAUSE_BTN_H, 0xd6da);
  tft.setTextColor(0xFFFF, bg);

  tft.setTextSize(1);
  tft.setCursor(PAUSE_BTN_X + 6, PAUSE_BTN_Y + 4);
  tft.print(paused ? " PAUSED " : " PAUSE  ");
}

// ═══════════════════════════════════════════════════════════════
//  THICK LINE DRAW (Bresenham with perpendicular width padding)
//
//  Draws a line from (x0,y0) to (x1,y1) at the given thickness
//  by offsetting parallel lines in the perpendicular direction.
//  This is sprite-local — coordinates are within the sprite.
// ═══════════════════════════════════════════════════════════════
void drawThickLine(TFT_eSprite& spr,
                   int16_t x0, int16_t y0,
                   int16_t x1, int16_t y1,
                   uint16_t color, uint8_t thickness) {
  // For nearly-vertical segments, widen horizontally.
  // For everything else (most waveform segments), widen vertically.
  int16_t dx = abs(x1 - x0);
  int16_t dy = abs(y1 - y0);
  int8_t  half = (int8_t)(thickness / 2);

  if (dy > dx) {
    // Steep segment — offset horizontally
    for (int8_t t = -half; t <= half; t++) {
      spr.drawLine(x0 + t, y0, x1 + t, y1, color);
    }
  } else {
    // Shallow/flat segment — offset vertically
    for (int8_t t = -half; t <= half; t++) {
      spr.drawLine(x0, y0 + t, x1, y1 + t, color);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  GRID LINES
// ═══════════════════════════════════════════════════════════════
void drawGridLines(TFT_eSprite& spr, int h) {
  int divW = PLOT_W / 5;
  for (int i = 1; i < 5; i++) {
    spr.drawFastVLine(i * divW, 0, h, COL_GRID);
  }
  spr.drawFastHLine(0, h / 2, PLOT_W, COL_GRID);
  for (int x = 0; x < PLOT_W; x += 2) {
    spr.drawPixel(x, h / 4,     COL_GRID);
    spr.drawPixel(x, 3 * h / 4, COL_GRID);
  }
}

// ═══════════════════════════════════════════════════════════════
//  DRAW ONE CHANNEL STRIP
// ═══════════════════════════════════════════════════════════════
void drawChannelStrip(uint16_t* buf, uint16_t head, int16_t scroll,
                      uint16_t color, float vMax, bool is5V,
                      int yTop, const char* label) {

  spriteCH.fillSprite(COL_BG);
  drawGridLines(spriteCH, PLOT_H_EACH);

  int totalSamples = bufFull ? BUF_SIZE : (int)head;
  if (totalSamples < 2) {
    spriteCH.pushSprite(PLOT_X, yTop);
    return;
  }

  int startIdx = (int)head - PLOT_W - (int)scroll;
  if (bufFull && startIdx < 0) {
    startIdx = (startIdx % BUF_SIZE + BUF_SIZE) % BUF_SIZE;
  } else {
    startIdx = max(startIdx, 0);
  }

  int16_t prevY = -1;

  for (int x = 0; x < PLOT_W; x++) {
    int idx = ((int)startIdx + x) % BUF_SIZE;

    float v = adcToVoltage(buf[idx], is5V);
    v = constrain(v, 0.0f, vMax);

    // Map voltage to Y pixel (0 = top, PLOT_H_EACH-1 = bottom)
    int16_t py = (int16_t)(PLOT_H_EACH - 1 -
                           (int)((v / vMax) * (float)(PLOT_H_EACH - 2)));
    py = constrain(py, 0, PLOT_H_EACH - 1);

    if (prevY >= 0 && x > 0) {
      drawThickLine(spriteCH, x - 1, prevY, x, py, color, WAVE_THICKNESS);
    }
    prevY = py;
  }

  // Channel label (top-left of strip)
  spriteCH.setTextColor(color);
  spriteCH.setTextSize(1);
  spriteCH.setCursor(2, 2);
  spriteCH.print(label);

  spriteCH.pushSprite(PLOT_X, yTop);
}

// ═══════════════════════════════════════════════════════════════
//  ADC → VOLTAGE
//
//  *** CALIBRATION POINT ***
//  If readings are off, measure your 3.3V rail and adjust ADC_VREF_MV.
//  Apply a known voltage to a channel and tweak DIVIDER_FACTOR for 5V probes.
//  For best ESP32 ADC accuracy, replace analogRead() with
//  esp_adc_cal_characterize() + esp_adc_cal_raw_to_voltage() from ESP-IDF.
// ═══════════════════════════════════════════════════════════════
float adcToVoltage(uint16_t raw, bool is5V) {
  float vAdc = (float)raw * (ADC_VREF_MV / 1000.0f) / ADC_MAX;
  return is5V ? vAdc * DIVIDER_FACTOR : vAdc;
}