# Page Playground

A device using Web Serial to modify elements selected with the mouse. The browser side uses a
GreaseMonkey script to inject JavaScript into each page, adding a "Play" button in the bottom-right
corner.

See the [Adafruit Playground
Note](https://adafruit-playground.com/u/afranchuk/pages/page-playground-using-web-serial-in-firefox)
for details about its development and a demo.

## Communication

Communication is done using COBS-encoded MessagePack messages.


## Inputs

Tool pages use a mixture of inputs to alter values:
- rotary encoders,
- buttons,
- an IMU,
- a proximity/color sensor,
- a joystick, and
- a slide potentiometer.


## Element Selection

The element under the cursor is the one that is changed. The selected element tag and id is
displayed at the top of the display. You can click the MACROPAD rotary encoder to lock the selected
element (and click again to unlock). When locked, the element tag is displayed with inverted colors.


## Tool Pages

### Colors

Edit the foreground and background colors of the selected element.

The first two keys correspond to foreground and background. The bottom 6 keys are a device-local
color palette. Press and hold a color (including those in the color palette) to edit it. Any number
of keys can be held down, but when only one is held down, the color RGB and HSL values are displayed
on the screen. Clicking another color will copy the color to the selected color(s). The left rotary
encoder changes hue, pressing down changes saturation, and the right rotary encoder changes
lightness.

If the slide potentiometer is non-zero, the color sensor is used instead of the encoders. The slide
potentiometer value is used to boost the lightness of the color read from the sensor.


### Text

The screen displays the font family, font size, and line spacing of the selected element. The first 10 keys
change the font family to common families. The left encoder changes font size, and the right encoder
changes line spacing.


### Layout

The screen displays a box model of the selected element, including margin, padding, width, and
height. A plus-shaped set of keys select top/left/bottom/right (or any combination), and the center
selects all 4. The top-right of the plus selects width, and the bottom-left selects height
(corresponding to the values on the screen). Change values using the left rotary encoder. Press the
rotary encoder to change padding (rather than margin) where applicable.

If the slide potentiometer is non-zero, the proximity sensor is used instead of the encoder. Scale
the proximity value using the potentiometer position.


### Transform

Hold the first key to change the orientation using the joystick. Hold the second key to translate
the element using the joystick.

If the slide potentiometer is non-zero, the IMU is used to set orientation and translation rather
than the joystick (you still need to hold the appropriate keys). Simply hold the key(s) and
orient/move the device.


### Effects

Clicking the first button results in Kit popping up at the bottom of the screen.

Clicking the second button results in the Adafruit logo rolling on and off the screen.
