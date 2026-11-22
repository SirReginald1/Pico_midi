# Pico mid

The code and hardware configuration is designed to work with small to medium sized controllers. With small adjustments to code and hardware this configuration can be made to work for large controllers with hundreds of control elements.

## Hardware &#x1F39B;

The controller code is currently setup to use the following hardware:
- Raspbery pico (rp2040)
- ADS1115: For reading potentiometer values.
- HP4067 multiplexer: Is placed in front of the ADS1115 to increase the number of potentiometers that can be used.
- PCF8574: To connect buttons and rotary encoders.
- ws2812 LEDs (optional)

The above hardware allows for a maximum of 64 potentiometers. As well as a combination of up to 64 buttons OR 32 rotary encoders.

> [!NOTE]
> The number of buttons and rotary encoders can be doubled without changing the structure of the code by using an I2C multiplexer as PCF8574 can only be set to 8 different addresses. This limit is due to the number of free pins that can be used as independent interrupts for each PCF8574.Can be increased even further by routing interrupts through there own dedicated PCF8574 or by connecting multiple PCF8574 interrupt pins to the same pico pin. 

> [!NOTE]
> Large numbers of potentiometers may cause rotary encoder readings to glitch. As the potentiometer values must be polled via I2C and the multiplexer channels switched on each read. Using a different ADC may be needed for large numbers of potentiometers.

## Communication &#x1F6F0;

When a control element is triggered both a MIDI message and a UART message is sent. The MIDI message is sent first, then the UART message.

### MIDI communication &#x1F3B5;

The MIDI communication seeks to imitate the Roland R2 loop hotcues, FX and loop in, loop out, loop jump.

Buttons send a MIDI note on message with a note value of 127 when button in pressed and a value of 0 when it is released.

Potentiometers send a MIDI control change message with values ranging between 0 and 127.

Rotary encoders send a MIDI note on message whit a note value of 43 for clockwise rotation and 45 for an anti-clockwise rotation.

### UART communication &#x1F50C;

UART messages are constituted of 3 bytes. 
The first indicates the type of control element:
- `0x62` (b): Button (Rotary encoders buttons, joystick buttons etc...)
- `0x65` (e): Rotary encoders
- `0x70` (p): Potentiometer

The second byte indicates the id number of the control element type. Ids go from 0 to number of used control element type. All clickable control elements that are not buttons are tacked on the end of the button ids. Example buttons ids are [0;NB_POTS + NB_ENCODERS + NB_JOYSTICKS].

The third byte is the value of the control element:
- Buttons:
    + `0x7F`: Button currently pressed
    + `0x00`: Button released
- Potentiometer:
    + From 0 to 255
- Rotary encoders:
    + `0x2B` (+)
    + `0x2D` (-)

## Next features &#x1F9ED;

- ws2812 LED indicator lights around the control elements.
- Receive MIDI and UART from connected application to set LED colors depending on application.