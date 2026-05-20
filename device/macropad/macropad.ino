#include <Adafruit_APDS9960.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_seesaw.h>
#include <Button.h>
#include <Quaternion.h>
#include <RotaryEncoder.h>
#include <Wire.h>
#include <USB.h>
#include <seesaw_neopixel.h>

#include <algorithm>
#include <array>

#define PACKETIZER_USE_INDEX_AS_DEFAULT
#define DEBUGLOG_DISABLE_LOG
#include <Packetizer.h>
#include <MsgPack.h>


// Serialize the fields of a struct without wrapping in structured type.
#define MSGPACK_DEFINE_BARE(...) \
  void to_msgpack(MsgPack::Packer& packer) const { \
    packer.serialize(__VA_ARGS__); \
  } \
  void from_msgpack(MsgPack::Unpacker& unpacker) { \
    unpacker.deserialize(__VA_ARGS__); \
  }

// CSS length value
struct Length {
  float value = 0;
  uint8_t unit = 0;

  MSGPACK_DEFINE_BARE(value, unit);
};

// CSS lengths for boxes
struct BoxLength {
  Length top;
  Length bottom;
  Length left;
  Length right;

  MSGPACK_DEFINE_BARE(top, bottom, left, right);
};

// HSL color values
using HSL = std::array<double, 3>;

// An encoder and neopixel on a seesaw chip
struct SeesawEnc {
  const uint8_t SS_SWITCH = 24;

  Adafruit_seesaw encoder;
  seesaw_NeoPixel pixel = seesaw_NeoPixel(1);

  // Begin communication with the given I2C address.
  bool begin(uint8_t addr) {
    if (!(encoder.begin(addr) && pixel.begin(addr))) {
      return false;
    }
    encoder.pinMode(SS_SWITCH, INPUT_PULLUP);
    return true;
  }

  // Returns whether the encoder button is pressed.
  bool pressed() {
    return !encoder.digitalRead(SS_SWITCH);
  }
};

// Style information for the selected element.
struct StyleInfo {
  String element;
  std::vector<uint32_t> colors;
  String font;
  Length fontSize;
  Length lineHeight;
  Length width;
  Length height;
  BoxLength margin;
  BoxLength padding;

  MSGPACK_DEFINE_BARE(element, colors, font, fontSize, lineHeight, width, height, margin, padding);
};

// Different pages of tools.
enum class Page {
  Colors,
  Text,
  Layout,
  Transform,
  Effects,
  LAST,
};

HSL rgbToHsl(uint32_t rgb);
uint32_t hslToRgb(const HSL &hsl);

// A saved color in the color palette.
struct SavedColor {
  HSL hsl;
  uint32_t rgb = 0;
  SavedColor(double h, double s, double l) {
    setHSL({ h, s, l });
  }

  // Set HSL and update RGB.
  void setHSL(HSL val) {
    hsl = val;
    updateRgb();
  }

  // Update RGB from the HSL value.
  void updateRgb() {
    rgb = hslToRgb(hsl);
  }
};

// Communication interface to the daughter board (KB2040)
struct Daughter {
  static const uint8_t address = 0x20;

  // Get the 10-bit slider value.
  static uint16_t getSlider() {
    return getAnalog(0);
  }

  // Get the 10-bit joystick X-axis value.
  static uint16_t getJoyX() {
    return getAnalog(1);
  }

  // Get the 10-bit joystick Y-axis value.
  static uint16_t getJoyY() {
    return getAnalog(2);
  }

  // Get the 10-bit value from an analog pin (0-3).
  static uint16_t getAnalog(uint8_t which) {
    Wire.beginTransmission(address);
    Wire.write(which);
    Wire.endTransmission();
    Wire.requestFrom(address, 2, false);
    union {
      uint16_t s;
      uint8_t buf[2];
    } data;
    data.buf[1] = Wire.read();
    data.buf[0] = Wire.read();
    return data.s;
  }
};


// IMU orientation/position management state and logic.
struct IMUPos {
  Adafruit_BNO08x imu;
  Quaternion orientation;
  float xv;
  float yv;
  float zv;
  float xoffset;
  float yoffset;
  float zoffset;
  unsigned long timestamp = 0;

  // Setup the imu device and state (assumes zero-initialized static state).
  void setup() {
    imu.begin_I2C();
    imu.enableReport(SH2_GAME_ROTATION_VECTOR);
    imu.enableReport(SH2_LINEAR_ACCELERATION);
  }

  // Update the current state.
  void tick() {
    if (imu.wasReset()) {
      imu.enableReport(SH2_GAME_ROTATION_VECTOR);
      imu.enableReport(SH2_LINEAR_ACCELERATION);
    }

    /*
     * Below, we swap and negate x and y to change to the desired device
     * coordinates (the rotation vector xyz is fully negated as well to have
     * the desired heading).
     */

    sh2_SensorValue_t sensorValue;
    while (imu.getSensorEvent(&sensorValue)) {
      switch (sensorValue.sensorId) {
        case SH2_GAME_ROTATION_VECTOR:
          orientation = Quaternion(
            sensorValue.un.gameRotationVector.real,
            sensorValue.un.gameRotationVector.j,
            sensorValue.un.gameRotationVector.i,
            -sensorValue.un.gameRotationVector.k);
          break;
        case SH2_LINEAR_ACCELERATION:
          auto now = millis();
          if (timestamp == 0) {
            timestamp = now;
            continue;
          }

          // Translate to world coordinates
          auto world = orientation.rotate(Quaternion(
            -sensorValue.un.linearAcceleration.y,
            -sensorValue.un.linearAcceleration.x,
            sensorValue.un.linearAcceleration.z));

// Use a macro for seconds in the hopes that it will give slightly more
// accurate results by multiplying and then dividing.
#define seconds float(now - timestamp) / 1000.
          xv += world.b * seconds;
          yv += world.c * seconds;
          zv += world.d * seconds;

          // Correct for drift (this isn't perfect, but better than nothing).
          const auto accelMag = sensorValue.un.linearAcceleration.x * sensorValue.un.linearAcceleration.x
          + sensorValue.un.linearAcceleration.y * sensorValue.un.linearAcceleration.y
          + sensorValue.un.linearAcceleration.z * sensorValue.un.linearAcceleration.z;
          const auto noAccel = accelMag < .01;
          const auto velMag = xv * xv + yv * yv + zv * zv;
          if (noAccel && velMag < 1) {
            xv = 0;
            yv = 0;
            zv = 0;
          }

          xoffset += xv * seconds;
          yoffset += yv * seconds;
          zoffset += zv * seconds;
#undef seconds

          timestamp = now;
          break;
      }
    }
  }

  void zero() {
    timestamp = 0;
    orientation = Quaternion();
    xv = 0;
    yv = 0;
    zv = 0;
    xoffset = 0;
    yoffset = 0;
    zoffset = 0;
  }
};

// Track button pressed based on time down (as opposed to press-and-hold).
class ButtonPressTime {
  pin_size_t pin;
  unsigned long startTime = 0;
  unsigned long lastPress = 0;
  bool wasPressed = false;
public:
  ButtonPressTime(pin_size_t pin)
    : pin(pin) {}

  void tick() {
    bool isPressed = !digitalRead(pin);
    lastPress = 0;
    if (isPressed && !wasPressed) {
      startTime = millis();
    } else if (wasPressed && !isPressed) {
      lastPress = millis() - startTime;
    }
    wasPressed = isPressed;
  }

  bool wasClicked() {
    return lastPress != 0 && lastPress < 200;
  }
};

/** Messaging functions */

// Send a message with the given index and values.
template<typename... Ts>
void sendMessage(const uint8_t index, Ts&&... values) {
  MsgPack::Packer packer;
  packer.serialize(std::forward<Ts>(values)...);
  // Wakeup the USB in case it has gone to sleep
  if (tud_suspended()) { tud_remote_wakeup(); }
  Packetizer::send(Serial, index, packer.data(), packer.size());
}

void logMessage(String msg) {
  sendMessage(0, msg);
}

void setMode(int mode) {
  sendMessage(1, mode);
}

void setColor(uint8_t which, uint32_t c) {
  sendMessage(2, which, c);
}

void setFont(String font) {
  sendMessage(3, font);
}

void setFontSize(Length l) {
  sendMessage(4, l);
}

void setLineHeight(Length l) {
  sendMessage(5, l);
}

void setWidth(Length l) {
  sendMessage(6, l);
}

void setHeight(Length l) {
  sendMessage(7, l);
}

void setMargin(BoxLength l) {
  sendMessage(8, l);
}

void setPadding(BoxLength l) {
  sendMessage(9, l);
}

void setTransform(float rx, float ry, float rz, float a, float x, float y, float z) {
  sendMessage(10, rx, ry, rz, a, x, y, z);
}

void doEffect(uint8_t which) {
  sendMessage(11, which);
}


/* Create all of the device wrappers and global state. */
Button encoder_button(PIN_SWITCH);
SeesawEnc rot1;
SeesawEnc rot2;
Adafruit_APDS9960 apds;
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_NEOPIXEL, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &SPI1, OLED_DC, OLED_RST, OLED_CS);
RotaryEncoder encoder(PIN_ROTA, PIN_ROTB, RotaryEncoder::LatchMode::FOUR3);
IMUPos imupos;
ButtonPressTime buttons[12] = {
  { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 }, { 8 }, { 9 }, { 10 }, { 11 }, { 12 }
};
// Scaling of the sensor-use slide potentiometer
float sensorScale = 0;

// Visual state
struct {
  int mode = 0;
  Page page = Page::Colors;
  StyleInfo styleInfo;
  // HSL colors corresponding to those in styleInfo.
  std::vector<HSL> colorsHsl;
  SavedColor savedColors[6] = {
    { 0, 1, 0.5 }, { 1. / 6, 1, 0.5 }, { 2. / 6, 1, 0.5 }, { 3. / 6, 1, 0.5 }, { 4. / 6, 1, 0.5 }, { 5. / 6, 1, 0.5 }
  };
} state;

void setup() {
  encoder_button.begin();
  Serial.begin(115200);
  delay(100);

  // Setup seesaw encoders
  rot1.begin(0x36);
  rot2.begin(0x37);

  // Setup APDS9960
  apds.begin();
  apds.enableColor();
  apds.enableProximity();

  // Setup IMU
  imupos.setup();

  // Setup incoming message handling
  // Reset
  Packetizer::subscribe(Serial, 0, [&](const uint8_t* data, const size_t size) {
    // No payload
    state.mode = 0;
    state.page = Page::Colors;
  });

  // Style info
  Packetizer::subscribe(Serial, 1, [&](const uint8_t* data, const size_t size) {
    MsgPack::Unpacker unpacker;
    unpacker.feed(data, size);
    unpacker.deserialize(state.styleInfo);

    state.colorsHsl.resize(state.styleInfo.colors.size());
    for (size_t i = 0; i < state.styleInfo.colors.size(); i++) {
      state.colorsHsl[i] = rgbToHsl(state.styleInfo.colors[i]);
    }
  });

  // start pixels
  pixels.begin();
  pixels.setBrightness(255);
  pixels.show();

  // Start OLED
  display.begin(0, true);  // we dont use the i2c address but we will reset!
  display.display();

  // set all mechanical keys to inputs
  for (uint8_t i = 0; i <= 12; i++) {
    pinMode(i, INPUT_PULLUP);
  }

  // set rotary encoder inputs and interrupts
  pinMode(PIN_ROTA, INPUT_PULLUP);
  pinMode(PIN_ROTB, INPUT_PULLUP);

  // Setup I2C
  Wire.begin();

  // text display defaults
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE, SH110X_BLACK);  // white text, black background
}

/** Color helpers */
HSL rgbToHsl(uint32_t rgb) {
  uint8_t r = (rgb >> 16) & 0xff;
  uint8_t g = (rgb >> 8) & 0xff;
  uint8_t b = rgb & 0xff;
  double rd = (double)r / 255;
  double gd = (double)g / 255;
  double bd = (double)b / 255;
  double max = MAX(MAX(rd, gd), bd);
  double min = MIN(MIN(rd, gd), bd);
  double h, s, l = (max + min) / 2;

  if (max == min) {
    // achromatic, but default to max saturation so we see color when twiddling
    h = 0;
    s = 1;
  } else {
    double d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    if (max == rd) {
      h = (gd - bd) / d + (gd < bd ? 6 : 0);
    } else if (max == gd) {
      h = (bd - rd) / d + 2;
    } else if (max == bd) {
      h = (rd - gd) / d + 4;
    }
    h /= 6;
  }
  return { h, s, l };
}

double hueTorgb(double p, double q, double t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1 / 6.0) return p + (q - p) * 6 * t;
  if (t < 1 / 2.0) return q;
  if (t < 2 / 3.0) return p + (q - p) * (2 / 3.0 - t) * 6;
  return p;
}

uint32_t hslToRgb(const HSL& hsl) {
  const auto& [h, s, l] = hsl;
  double r, g, b;

  if (s == 0) {
    r = g = b = l;  // achromatic
  } else {
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    double p = 2 * l - q;
    r = hueTorgb(p, q, h + 1 / 3.0);
    g = hueTorgb(p, q, h);
    b = hueTorgb(p, q, h - 1 / 3.0);
  }

  return (uint32_t(r * 255) << 16) | (uint32_t(g * 255) << 8) | uint32_t(b * 255);
}

void shiftHSL(HSL& hsl, int32_t hue, int32_t sat, int32_t light) {
  auto& [h, s, l] = hsl;
  h += double(hue * 4) / 360;
  while (h < 0.) h += 1.;
  while (h > 1.) h -= 1.;
  s += double(sat * 4) / 100;
  s = std::clamp(s, 0., 1.);
  l += double(light * 2) / 100;
  l = std::clamp(l, 0., 1.);
}

void colorsPage() {
  const size_t colorCount = MIN(state.colorsHsl.size(), 6);

  // First record if we've pressed and released a color, as that indicates the color to copy.
  HSL copyHsl;
  bool copy = false;
  for (size_t i = 0; i < colorCount; i++) {
    if (buttons[i].wasClicked()) {
      copyHsl = state.colorsHsl[i];
      copy = true;
    }
  }
  for (size_t i = 6; i < 12; i++) {
    if (buttons[i].wasClicked()) {
      copyHsl = state.savedColors[i - 6].hsl;
      copy = true;
    }
  }

  if (sensorScale != 0) {
    if (apds.colorDataReady()) {
      uint16_t r, g, b, c;
      apds.getColorData(&r, &g, &b, &c);
      uint32_t newColor = uint32_t(r & 0xff) << 16 | uint32_t(g & 0xff) << 8 | uint32_t(b & 0xff);
      auto newHsl = rgbToHsl(newColor);
      // Boost lightness with sensor scale value
      newHsl[2] = MIN(newHsl[2] * sensorScale * 20, 1);

      for (size_t i = 0; i < colorCount; i++) {
        if (!digitalRead(i + 1)) {
          auto& hsl = state.colorsHsl[i];
          auto& rgb = state.styleInfo.colors[i];
          hsl = newHsl;
          rgb = hslToRgb(hsl);
          setColor(i, rgb);
        }
      }

      for (size_t i = 6; i < 12; i++) {
        if (!digitalRead(i + 1)) {
          auto& color = state.savedColors[i - 6];
          color.setHSL(newHsl);
        }
      }
    }
  } else {
    int32_t hue_shift = rot1.encoder.getEncoderDelta();
    int32_t sat_shift = 0;
    const auto light_shift = rot2.encoder.getEncoderDelta();

    if (rot1.pressed()) {
      std::swap(hue_shift, sat_shift);
    }

    if (hue_shift != 0 || sat_shift != 0 || light_shift != 0 || copy) {
      for (size_t i = 0; i < colorCount; i++) {
        if (!digitalRead(i + 1)) {
          auto& hsl = state.colorsHsl[i];
          auto& rgb = state.styleInfo.colors[i];
          if (copy) {
            hsl = copyHsl;
          } else {
            shiftHSL(hsl, hue_shift, sat_shift, light_shift);
          }
          rgb = hslToRgb(hsl);
          setColor(i, rgb);
        }
      }

      for (size_t i = 6; i < 12; i++) {
        if (!digitalRead(i + 1)) {
          auto& color = state.savedColors[i - 6];
          if (copy) {
            color.hsl = copyHsl;
          } else {
            shiftHSL(color.hsl, hue_shift, sat_shift, light_shift);
          }
          color.updateRgb();
        }
      }
    }
  }

  for (size_t i = 0; i < colorCount; i++) {
    pixels.setPixelColor(i, state.styleInfo.colors[i]);
  }
  for (size_t i = 6; i < 12; i++) {
    pixels.setPixelColor(i, state.savedColors[i - 6].rgb);
  }

  // If exactly one button is held down, show the values
  size_t buttonsDown = 0;
  HSL downHSL;
  uint32_t downRGB;
  for (size_t i = 0; i < colorCount; i++) {
    if (!digitalRead(i + 1)) {
      downHSL = state.colorsHsl[i];
      downRGB = state.styleInfo.colors[i];
      buttonsDown++;
    }
  }
  for (size_t i = 6; i < 12; i++) {
    if (!digitalRead(i + 1)) {
      const auto& color = state.savedColors[i - 6];
      downHSL = color.hsl;
      downRGB = color.rgb;
      buttonsDown++;
    }
  }
  if (buttonsDown == 1) {
    display.setCursor(8 * 5, 24);
    display.printf("#%02x%02x%02x", (downRGB >> 16) & 0xff, (downRGB >> 8) & 0xff, downRGB & 0xff);
    display.setCursor(4 * 5, 32);
    display.printf("%3d %3d%% %3d%%", int(downHSL[0] * 360), int(downHSL[1] * 100), int(downHSL[2] * 100));
  }
}

void textPage() {
  static String fonts[10] = {
    "serif", "sans-serif", "monospace", "cursive", "fantasy",
    "system-ui", "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded"
  };

  display.setCursor(0, 16);

  String* newFont = nullptr;
  for (size_t i = 0; i < 10; i++) {
    pixels.setPixelColor(i, 0xffffff);
    if (!digitalRead(i + 1)) {
      newFont = &fonts[i];
    }
  }

  if (newFont) {
    state.styleInfo.font = *newFont;
    setFont(state.styleInfo.font);
  }

  auto r1Delta = rot1.encoder.getEncoderDelta();
  if (r1Delta != 0) {
    state.styleInfo.fontSize.value += r1Delta;
    if (state.styleInfo.fontSize.value < 1.) state.styleInfo.fontSize.value = 1;
    setFontSize(state.styleInfo.fontSize);
  }

  auto r2Delta = rot2.encoder.getEncoderDelta();
  if (r2Delta != 0) {
    state.styleInfo.lineHeight.value += r2Delta;
    if (state.styleInfo.lineHeight.value < 1.) state.styleInfo.lineHeight.value = 1;
    setLineHeight(state.styleInfo.lineHeight);
  }

  display.print("Font: ");
  display.println(state.styleInfo.font);
  display.printf("Size: %d\n", int(state.styleInfo.fontSize.value));
  display.printf("Line height: %d", int(state.styleInfo.lineHeight.value));
}

void layoutPage() {
  display.drawRect(34, 30, 60, 20, 1);
  display.setCursor(59, 22);
  display.printf("%2d", int(state.styleInfo.margin.top.value));
  display.setCursor(59, 32);
  display.printf("%2d", int(state.styleInfo.padding.top.value));

  display.setCursor(59, 41);
  display.printf("%2d", int(state.styleInfo.padding.bottom.value));
  display.setCursor(59, 51);
  display.printf("%2d", int(state.styleInfo.margin.bottom.value));

  display.setCursor(18, 36);
  display.printf("%2d", int(state.styleInfo.margin.left.value));
  display.setCursor(36, 36);
  display.printf("%2d", int(state.styleInfo.padding.left.value));

  display.setCursor(78, 36);
  display.printf("%2d", int(state.styleInfo.padding.right.value));
  display.setCursor(95, 36);
  display.printf("%2d", int(state.styleInfo.margin.right.value));

  display.setCursor(94, 8);
  display.printf("%d", int(state.styleInfo.width.value));
  display.setCursor(0, 50);
  display.printf("%d", int(state.styleInfo.height.value));

  int32_t r1Delta = rot1.encoder.getEncoderDelta();
  auto r1Box = &state.styleInfo.margin;
  auto r1Update = setMargin;
  if (rot1.pressed()) {
    r1Box = &state.styleInfo.padding;
    r1Update = setPadding;
  }

  for (size_t i = 1; i <= 7; i++) {
    pixels.setPixelColor(i, 0x222222);
  }

  std::function<void(float&)> applyValue = sensorScale != 0
    ? std::function<void(float&)>([](float& f) { f = (255 - apds.readProximity()) * (1 + sensorScale * 4) / 5; })
    : std::function<void(float&)>([&](float& f) { f += r1Delta; });

  // Width
  if (!digitalRead(3)) {
    pixels.setPixelColor(2, 0xffffff);
    applyValue(state.styleInfo.width.value);
    setWidth(state.styleInfo.width);
  }

  // Height
  if (!digitalRead(7)) {
    pixels.setPixelColor(6, 0xffffff);
    applyValue(state.styleInfo.height.value);
    setHeight(state.styleInfo.height);
  }

  bool change = false;
  // Box (all)
  if (!digitalRead(5)) {
    change = true;
    for (auto i : { 1, 3, 5, 7 }) {
      pixels.setPixelColor(i, 0xffffff);
    }
    applyValue(r1Box->top.value);
    applyValue(r1Box->bottom.value);
    applyValue(r1Box->left.value);
    applyValue(r1Box->right.value);
    // Box (individual)
  } else {
    for (auto i : { 2, 4, 6, 8 }) {
      if (!digitalRead(i)) {
        change = true;
        pixels.setPixelColor(i - 1, 0xffffff);
        if (i == 2) {
          applyValue(r1Box->top.value);
        } else if (i == 4) {
          applyValue(r1Box->left.value);
        } else if (i == 6) {
          applyValue(r1Box->right.value);
        } else if (i == 8) {
          applyValue(r1Box->bottom.value);
        }
      }
    }
  }

  if ((r1Delta != 0 || sensorScale != 0) && change) {
    r1Update(*r1Box);
  }
}

void transformPage() {
  static auto transformActive = false;
  static float lastRx, lastRy, lastRz, lastAngle, lastXOffset, lastYOffset, lastZOffset;

  pixels.setPixelColor(0, 0x00ff00);
  pixels.setPixelColor(1, 0x0000ff);
  const auto enableOrientation = !digitalRead(1);
  const auto enableTranslation = !digitalRead(2);

  if (sensorScale == 0) {
    const auto x = -((float)Daughter::getJoyX() - 512) / 512;
    const auto y = -((float)Daughter::getJoyY() - 512) / 512;
    if (enableOrientation) {
      lastRx = -y;
      lastRy = x;
      lastRz = 0;
      lastAngle = sqrt(x * x + y * y) * PI / 4;
    }
    if (enableTranslation) {
      lastXOffset += x / 1000;
      lastYOffset += y / 1000;
      lastZOffset = 0;
    }
    setTransform(lastRx, lastRy, lastRz, lastAngle,
                 lastXOffset, lastYOffset, lastZOffset);
    return;
  }


  if (enableOrientation || enableTranslation) {
    if (!transformActive) {
      imupos.zero();
      transformActive = true;
    }
    imupos.tick();

    if (enableOrientation) {
      lastAngle = 2 * acos(imupos.orientation.a);
      auto scale = lastAngle / sin(lastAngle / 2);
      lastRx = imupos.orientation.b * scale;
      lastRy = imupos.orientation.c * scale;
      lastRz = imupos.orientation.d * scale;
    }
    if (enableTranslation) {
      lastXOffset = imupos.xoffset;
      lastYOffset = imupos.yoffset;
      lastZOffset = imupos.zoffset;
    }
    setTransform(lastRx, lastRy, lastRz, lastAngle,
                 lastXOffset, lastYOffset, lastZOffset);
  } else {
    transformActive = false;
  }
}

void effectsPage() {
  pixels.setPixelColor(0, 0xf57a01);
  pixels.setPixelColor(1, 0xffffff);
  for (size_t i = 0; i < 2; i++) {
    if (buttons[i].wasClicked()) {
      doEffect(i);
    }
  }
}

void loop() {
  // Read incoming serial packets and perform callbacks
  Packetizer::parse();

  display.clearDisplay();
  display.setCursor(0, 0);
  pixels.clear();

  for (auto& b : buttons) {
    b.tick();
  }

  // Update sensor-use scaling
  auto rawVal = Daughter::getSlider();
  // Deadband to 0
  if (rawVal < 20) sensorScale = 0;
  else sensorScale = float(MIN(rawVal, 1000)) / 1000.;

  // Draw a little indicator if the sensor-use is enabled (I couldn't scrounge
  // up an LED somehow).
  if (sensorScale != 0) {
    display.drawRect(126, 62, 2, 2, 1);
  }

  // Encoder press rotates through selection modes.
  if (encoder_button.pressed()) {
    state.mode = (state.mode + 1) % 2;
    setMode(state.mode);
  }

  // Invert color of the element name when locked.
  if (state.mode == 1) {
    display.setTextColor(SH110X_BLACK, SH110X_WHITE);
  }
  display.print(state.styleInfo.element);
  display.setTextColor(SH110X_WHITE, SH110X_BLACK);
  display.println();

  // Check encoder to change page
  encoder.tick();
  auto dir = encoder.getDirection();
  // We subtract dir because it is inverted (CW is negative)
  state.page = Page((int8_t(state.page) + int8_t(Page::LAST) - int8_t(dir)) % int8_t(Page::LAST));

  switch (state.page) {
    case Page::Colors:
      display.setCursor(128 - 6 * 6, 0);
      display.print("Colors");
      colorsPage();
      break;
    case Page::Text:
      display.setCursor(128 - 6 * 4, 0);
      display.print("Text");
      textPage();
      break;
    case Page::Layout:
      display.setCursor(128 - 6 * 6, 0);
      display.print("Layout");
      layoutPage();
      break;
    case Page::Transform:
      display.setCursor(128 - 6 * 9, 0);
      display.print("Transform");
      transformPage();
      break;
    case Page::Effects:
      display.setCursor(128 - 6 * 7, 0);
      display.print("Effects");
      effectsPage();
      break;
  }

  // show neopixels
  pixels.show();
  // display oled
  display.display();
}
