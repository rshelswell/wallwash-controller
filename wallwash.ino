#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "FastLED.h"
#include "DmxSimple.h"

#define TFT_CS 13
#define TFT_RST 12
#define TFT_DC 11
#define TFT_MOSI 10
#define TFT_CLK 9
#define TFT_LED 8
#define TFT_MISO 7

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);

#define BUTTON_UP A1
#define BUTTON_DOWN A2
#define BUTTON_SELECT A3

#define DEBOUNCE_DELAY 20
unsigned long buttonUpTime = millis();
unsigned long buttonDownTime = millis();
unsigned long buttonSelectTime = millis();

int buttonState[3] = { HIGH, HIGH, HIGH };
int lastButtonState[3] = { HIGH, HIGH, HIGH };
int lastDebounceTime[3] = { 0 };

#define LED_PIN A5
#define NUM_LEDS 16
#define BRIGHTNESS 255
#define LED_TYPE WS2812
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];
CRGBPalette16 currentPalette;
uint8_t colorIndex = 1;
TBlendType currentBlending;
bool paletteUpdated = true;


#define POT_PIN A4
int rawValue;
int oldValue;
byte potPercentage;
byte oldPercentage;

bool sendToDMX = false;
uint8_t universeSize = 48;

struct lightSetting {
  int numColours{ 1 };
  uint32_t firstColour{ CRGB::AntiqueWhite };
  uint32_t lastColour{ CRGB::AntiqueWhite };
};

#define NUM_LIGHT_SETS 40

int menuLevel = 0;
int menuRow = 0;
int menuMax[7] = { 2, 4, 10, 10, 10, 10, 10 };
lightSetting lightSets[NUM_LIGHT_SETS] = {};
bool selectedScreen = false;
int selectedPalette;

void FillLEDsFromPaletteColors(int startIndex, int endIndex);
void drawPalette(int x, int y, lightSetting lSet = {});

void setup() {
  delay(1500);  // power up wait
  Serial.begin(9600);
  createLightSettings();
  selectedPalette = 32;
  currentPalette = RainbowColors_p;
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  tft.begin();
  tft.invertDisplay(true);
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(20, 50, 70);
  }
  FastLED.show(BRIGHTNESS);
  delay(1500);
  currentPalette = RainbowColors_p;
  currentBlending = LINEARBLEND_NOWRAP;
  printMenu(menuLevel);
}

void loop() {
  if (selectedScreen) {
    //pot_brightness = analogRead(POT_PIN);
    updatePot();
  }

  checkButtons();

  if (sendToDMX) {
    writeToDMX();
  }
}

void checkButtons() {
  if (isButtonPushed(0)) {
    // move up
    menuRow--;
    menuRow = menuRow < 0 ? (menuMax[menuLevel] - 1) : menuRow;
    updateMenu();
  } else if (isButtonPushed(1)) {
    // move down
    menuRow++;
    menuRow = menuRow >= menuMax[menuLevel] ? 0 : menuRow;
    updateMenu();
  } else if (isButtonPushed(2)) {

    if (selectedScreen) {
      switch (menuRow) {
        case 8:
          //reset
          selectedScreen = false;
          sendToDMX = false;
          menuLevel = 0;
          menuRow = 0;
          break;
        default:
          sendToDMX = true;
      }
    } else {
    // select an option
    switch (menuLevel) {
      case 0:
        //top menu level
        switch (menuRow) {
          case 0:
            //use previous palette.
            FillLEDsFromPaletteColors(0, 765);
            sendToDMX = true;
            break;
          case 1:
            //change to another palette - move to number of colours selection
            menuRow = 0;
            menuLevel = 1;
            break;
          default:
            break;
        }
        break;
      case 1:
        // numColours selection
        switch (menuRow) {
          case 0:
            //back
            menuLevel = 0;
            break;
          case 1:
            //1 colour
            menuLevel = 2;
            break;
          case 2:
            //2 colours
            menuLevel = 4;
            break;
          case 3:
            //4 colours
            menuLevel = 6;
            break;
          case 4:
            //8 colours
            menuLevel = 8;
            break;
          default:
            //go full rainbow
            menuLevel = 9;
        }
        menuRow = 0;
        break;
      default:
        // colour scheme selection screen
        switch (menuRow) {
          case 0:
            //back
            switch (menuLevel) {
              case 2:
              case 4:
              case 6:
                menuLevel = 1;
                break;
              default:
                menuLevel++;
                break;
            }
            break;
          case 9:
            //next
            switch (menuLevel) {
              case 2:
              case 4:
                menuLevel++;
                break;
              case 3:
              case 5:
                menuLevel--;
                break;
              default:
                break;
            }
            break;
          default:
            // select a colour scheme
            selectedScreen = true;
            selectedPalette = (menuLevel - 2) * 8 + (menuRow - 1);
            break;
        }
        break;
    }
    menuRow = 0;
    printMenu(menuLevel);
    }
  }
}

void updatePot() {
  // read input twice
  rawValue = analogRead(POT_PIN);
  rawValue = analogRead(POT_PIN);  // double read
  // ignore bad hop-on region of a pot by removing 8 values at both extremes
  rawValue = constrain(rawValue, 8, 1015);
  // add some deadband
  if (rawValue < (oldValue - 4) || rawValue > (oldValue + 4)) {
    oldValue = rawValue;
    // convert to percentage
    potPercentage = map(oldValue, 8, 1008, 0, 100);
    // Only print if %value changes
    if (oldPercentage != potPercentage) {
      tft.setCursor(0, 96);
      tft.println("Brightness:");
      tft.setCursor(0, 120);
      tft.println("     ");
      tft.setCursor(0, 120);
      tft.println(potPercentage);
      oldPercentage = potPercentage;
    }
  }
}

void writeToDMX() {
  Serial.println("sending");
  int ch = 1;
  for (int l = 0; l < NUM_LEDS * 2; l++) {
    DmxSimple.write(ch, leds[l].r);
    ch++;
    DmxSimple.write(ch, leds[l].g);
    ch++;
    DmxSimple.write(ch, leds[l].b);
    ch++;
  }
}

void printMenu(int level) {
  // redraw screen based on where in the menu (level) we are
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  if (selectedScreen) {
    tft.println("Selected colours");
    drawPalette(12, 24, lightSets[selectedPalette]);
    tft.setCursor(0, 48);
    tft.println("Set brightness");
    tft.println("then press select");
  } else {
    switch (level) {
      case 0:
        tft.println("> Last used setting");
        potPercentage = 100;
        drawPalette(12, 24, lightSets[selectedPalette]);
        tft.setCursor(0, 48);
        tft.println("  Choose new scheme");
        break;
      case 1:
        tft.println("> back");
        tft.setCursor(0, 24);
        tft.println("  1 colour");
        tft.setCursor(0, 48);
        tft.println("  2 colours");
        tft.setCursor(0, 72);
        tft.println("  rainbow");
        break;
      default:
        tft.println("> back");
        for (int i = 0; i < 8; i++) {
          tft.setCursor(0, 24 + 24 * i);
          tft.println("  ");
          drawPalette(24, 24 + 24 * i, lightSets[i + (level - 2) * 8]);
        }
        tft.setCursor(0, 216);
        tft.println("  next");
        break;
    }
  }
}

void updateMenu() {
  tft.setCursor(0, 0);
  for (int r = 0; r < menuMax[menuLevel]; r++) {
    if (r == menuRow) {
      tft.println(">");
    } else {
      tft.println(" ");
    }
    if (menuLevel == 0 && r == 0) {
      tft.setCursor(0, 48);
    } else {
      tft.setCursor(0, 24 + 24 * r);
    }
  }
}

bool isButtonPushed(int button) {
  // check if one of the 6 buttons is being pushed.
  //return true if it is, false if not.
  int buttonPin;
  switch (button) {
    // select which pin to read from, to match the button
    case 0:
      buttonPin = BUTTON_UP;
      break;
    case 1:
      buttonPin = BUTTON_DOWN;
      break;
    case 2:
      buttonPin = BUTTON_SELECT;
      break;
    default:
      return false;  // safest option
      break;
  }

  // read the data from the pin
  int reading = digitalRead(buttonPin);

  // compare over time to ensure that the reading is correct.
  if (reading != lastButtonState[button]) {
    lastDebounceTime[button] = millis();
  }
  if ((millis() - lastDebounceTime[button]) > DEBOUNCE_DELAY) {
    if (reading != buttonState[button]) {
      buttonState[button] = reading;
      if (buttonState[button] == LOW) {
        //Serial.println("pushed button");
        //Serial.println(button);
        lastButtonState[button] = reading;
        return true;
      }
    }
  }
  lastButtonState[button] = reading;
  return false;
}

/*
void drawPalette(int y = 25, int startIndex = 0, int endIndex = 255) {
  int step = 0;
  if (startIndex != endIndex) {
    step = (endIndex - startIndex - 1) / (3 * NUM_LEDS);
  }
  for (int p = 0; p < NUM_LEDS; p++) {
    tft.fillRect(24 + 18 * p, y, 15, 15, getRGB565(ColorFromPalette(currentPalette, startIndex + (step * p), 255, currentBlending)));
    tft.drawRect(24 + 18 * p, y, 15, 15, ILI9341_WHITE);
  }
}
*/

void drawPalette(int x = 24, int y = 24, lightSetting lSet = {}) {
  uint16_t colour = getRGB565(lSet.firstColour);
  int step = 0;
  if (lSet.numColours <= 2) {
    // single colour throughout
    // or two colours
    for (int p = 0; p < NUM_LEDS; p++) {
      if (lSet.numColours == 2) {
        // change colour if needing 2 colours
        if (p >= NUM_LEDS / 2) {
          colour = getRGB565(lSet.lastColour);
        }
      }
      tft.fillRect(x + 18 * p, y, 15, 15, colour);
      tft.drawRect(x + 18 * p, y, 15, 15, ILI9341_WHITE);
    }
  } else if (lSet.numColours == 16) {
    // rainbow sections
    step = (lSet.lastColour - lSet.firstColour - 1) / NUM_LEDS;
    for (int p = 0; p < NUM_LEDS; p++) {
      tft.fillRect(x + 18 * p, y, 15, 15, getRGB565(ColorFromPalette(currentPalette, lSet.firstColour + (step * p), 255, currentBlending)));
      tft.drawRect(x + 18 * p, y, 15, 15, ILI9341_WHITE);
    }
  }
}

void FillLEDsFromPaletteColors(int startIndex = 0, int endIndex = 255) {
  int step = 0;
  if (startIndex != endIndex) {
    step = (endIndex - startIndex - 1) / (3 * NUM_LEDS);
  }
  for (int i = 0; i < NUM_LEDS; ++i) {
    leds[i] = ColorFromPalette(currentPalette, startIndex + (step * i), map(potPercentage, 0, 100, 0, 255), currentBlending);
    colorIndex += 256 / NUM_LEDS;
  }
  FastLED.show();
}

uint16_t getRGB565(CRGB paletteColour) {
  uint32_t RGB888 = paletteColour.as_uint32_t();
  //Serial.println(RGB888);
  uint16_t RGB565 = (((RGB888 & 0xf80000) >> 8) + ((RGB888 & 0xfc00) >> 5) + ((RGB888 & 0xf8) >> 3));
  //Serial.println(RGB565);
  return RGB565;
}

void createLightSettings() {
  lightSets[0].firstColour = CRGB::White;
  lightSets[1].firstColour = CRGB::Red;
  lightSets[2].firstColour = CRGB::Orange;
  lightSets[3].firstColour = CRGB::Yellow;
  lightSets[4].firstColour = CRGB::Green;
  lightSets[5].firstColour = CRGB::Blue;
  lightSets[6].firstColour = CRGB::Indigo;
  lightSets[7].firstColour = CRGB::DeepPink;
  lightSets[8].firstColour = CRGB::Gold;
  lightSets[9].firstColour = CRGB::Lime;
  lightSets[10].firstColour = CRGB::Cyan;
  lightSets[11].firstColour = CRGB::LightSkyBlue;
  lightSets[12].firstColour = CRGB::Purple;
  lightSets[13].firstColour = CRGB::Magenta;
  lightSets[14].firstColour = CRGB::Coral;
  lightSets[15].firstColour = CRGB::Pink;
  lightSets[16].numColours = 2;
  lightSets[16].firstColour = CRGB::Red;
  lightSets[16].lastColour = CRGB::Blue;
  lightSets[17].numColours = 2;
  lightSets[17].firstColour = CRGB::Blue;
  lightSets[17].lastColour = CRGB::Red;
  lightSets[18].numColours = 2;
  lightSets[18].firstColour = CRGB::Red;
  lightSets[18].lastColour = CRGB::Orange;
  lightSets[19].numColours = 2;
  lightSets[19].firstColour = CRGB::Orange;
  lightSets[19].lastColour = CRGB::Red;
  lightSets[20].numColours = 2;
  lightSets[20].firstColour = CRGB::Green;
  lightSets[20].lastColour = CRGB::Blue;
  lightSets[21].numColours = 2;
  lightSets[21].firstColour = CRGB::Blue;
  lightSets[21].lastColour = CRGB::Green;
  lightSets[22].numColours = 2;
  lightSets[22].firstColour = CRGB::DeepPink;
  lightSets[22].lastColour = CRGB::Indigo;
  lightSets[23].numColours = 2;
  lightSets[23].firstColour = CRGB::Indigo;
  lightSets[23].lastColour = CRGB::DeepPink;
  lightSets[24].numColours = 2;
  lightSets[24].firstColour = CRGB::Coral;
  lightSets[24].lastColour = CRGB::Yellow;
  lightSets[25].numColours = 2;
  lightSets[25].firstColour = CRGB::Yellow;
  lightSets[25].lastColour = CRGB::Coral;
  lightSets[26].numColours = 2;
  lightSets[26].firstColour = CRGB::LightSkyBlue;
  lightSets[26].lastColour = CRGB::Purple;
  lightSets[27].numColours = 2;
  lightSets[27].firstColour = CRGB::Purple;
  lightSets[27].lastColour = CRGB::LightSkyBlue;
  lightSets[28].numColours = 2;
  lightSets[28].firstColour = CRGB::Gold;
  lightSets[28].lastColour = CRGB::Cyan;
  lightSets[29].numColours = 2;
  lightSets[29].firstColour = CRGB::Cyan;
  lightSets[29].lastColour = CRGB::Gold;
  lightSets[30].numColours = 2;
  lightSets[30].firstColour = CRGB::Lime;
  lightSets[30].lastColour = CRGB::RoyalBlue;
  lightSets[31].numColours = 2;
  lightSets[31].firstColour = CRGB::RoyalBlue;
  lightSets[31].lastColour = CRGB::Lime;
  // continuous change rainbows

  // red to dark violet
  lightSets[32].numColours = 16;
  lightSets[32].firstColour = 0; 
  lightSets[32].lastColour = 255;
  lightSets[33].numColours = 16;
  lightSets[33].firstColour = 255;
  lightSets[33].lastColour = 0;

  // red to blue
  lightSets[34].numColours = 16;
  lightSets[34].firstColour = 0;  
  lightSets[34].lastColour = 200; 
  lightSets[35].numColours = 16;
  lightSets[35].firstColour = 200;
  lightSets[35].lastColour = 0;

  // red to green 
  lightSets[36].numColours = 16;
  lightSets[36].firstColour = 0;
  lightSets[36].lastColour = 128;
  lightSets[37].numColours = 16;
  lightSets[37].firstColour = 128;
  lightSets[37].lastColour = 0;

  // green to purple
  lightSets[38].numColours = 16;
  lightSets[38].firstColour = 128;
  lightSets[38].lastColour = 255;
  lightSets[39].numColours = 16;
  lightSets[39].firstColour = 255;
  lightSets[39].lastColour = 128;
}
