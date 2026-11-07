#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_NeoPixel.h>


#define CONTROLER_NAME "Pico Controller"
#define NB_BUTTONS 16
#define NB_TOTAL_ADS 2
#define NB_VAR_RESISTOR_ADS1 3
#define NB_VAR_RESISTOR_ADS2 3
#define NB_TOTAL_VAR_RESISTORS NB_VAR_RESISTOR_ADS1 + NB_VAR_RESISTOR_ADS2
//#define NB_LEDS 16  // Comment this to remove all LED related code
#define LED_PIN 16
#define NB_PCF8574 4
#define PCF8574_INT_PIN0 0
#define PCF8574_INT_PIN1 1
#define PCF8574_INT_PIN2 2
#define PCF8574_INT_PIN3 3
#define PCF8574_ADDRESS0 0x20
#define PCF8574_ADDRESS1 0x21
#define PCF8574_ADDRESS2 0x22
#define PCF8574_ADDRESS3 0x23
// TODO: Check to see if all values from the ADS can be read in a single I2C request.
#define ADS1115_ADDRESS0 0x48
#define ADS1115_ADDRESS1 0x49
#define ADC_POLL_RATE 10 // In ms
#define ADS_GAIN GAIN_TWO//GAIN_ONE
#define MAX_ADS_RANGE 32767
#define DEBUG


// Define constand arrays
const byte PCF_ADDRESSES[NB_PCF8574] = {
  PCF8574_ADDRESS0, PCF8574_ADDRESS1, PCF8574_ADDRESS2, PCF8574_ADDRESS3
};

// MIDI settings
//const byte MIDI_CC = 16;
const byte MIDI_CHANNEL = 1;

// ADS1115 configuration
const byte ADS_ADDRESSES[NB_TOTAL_ADS] = {
  ADS1115_ADDRESS0, ADS1115_ADDRESS1
};

// Define used pins on ads 1
const byte ADS1_INPUT[NB_VAR_RESISTOR_ADS1] = {
  0, 1, 2
};

const byte ADS2_INPUT[NB_VAR_RESISTOR_ADS2] = {
  0, 1, 2
};

// Defines the controle number for each of the variable resistors for ADS1
const byte VAR_RESISTOR_CONTROLE_NUMBERS1[NB_VAR_RESISTOR_ADS1] = {
  17, 18, 19
};

const byte VAR_RESISTOR_CONTROLE_NUMBERS2[NB_VAR_RESISTOR_ADS2] = {
  20, 21, 22
};

// The MIDI value associated with each button.
// Only to be used if button is not togglable.
const byte BUTTON_MIDI_VALUES[NB_BUTTONS] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};

// The MIDI controle number associated with each button
const byte BUTTON_CONTROLE_NUMBERS[NB_BUTTONS] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};

// ADC polling interval
const unsigned long readInterval = ADC_POLL_RATE;
unsigned long lastReadTime = 0;

// MIDI definitions
// Last transmitted MIDI value
int lastCCValues[NB_TOTAL_VAR_RESISTORS] = {-1};

// Array indicating if a button has been toggled
boolean buttonToggleValue[NB_BUTTONS] = {false};

// Array indicating if the button at the associated index is toggolable
boolean buttonUsesToggle[NB_BUTTONS] = {false};

// USB MIDI object
Adafruit_USBD_MIDI usb_midi;

// Create a new instance of the Arduino MIDI Library,
// and attach usb_midi as the transport.
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// Handle incoming Note On
void handleNoteOn(byte channel, byte note, byte velocity) {

}

// Handle incoming Note Off
void handleNoteOff(byte channel, byte note, byte velocity) {

}

// Handle incoming Control Change
void handleControlChange(byte channel, byte number, byte value) {

}

///// DEFINE LED STRIPS ////
#ifdef NB_LEDS
  // Array containing the reference color for each led
  // !!!! Color is GRB !!!!
  uint8_t ledColorValues[NB_LEDS][3] = {{0, 0, 0}};

  Adafruit_NeoPixel strip(
    NB_LEDS,
    LED_PIN,
    NEO_GRB + NEO_KHZ800
  );
#endif

///// DEFINE ADS1115 VARIABLES ////
Adafruit_ADS1115 ads[NB_TOTAL_ADS];

///// DEFINE PCF8574 VARIABLES ////
volatile bool pcfInterruptFlags[NB_PCF8574] = {false};

byte pcfValues[NB_PCF8574] = {0x00};

void pcf0Interrupt() {
    pcfInterruptFlags[0] = true;
}

void pcf1Interrupt() {
    pcfInterruptFlags[1] = true;
}

void pcf2Interrupt() {
    pcfInterruptFlags[2] = true;
}

void pcf3Interrupt() {
    pcfInterruptFlags[3] = true;
}

/**
 * Returns the appropriate value to send for the provided buttoon index
 * based on the buttons state and if the button is togglable.
 * @param buttonIdx The index at which all the buttons states can be found
 * in all of it's associated arrays.
 * @return byte: The value that should be sent over MIDI.
 */
byte selectBtnValueToSend(int buttonIdx){
  if(buttonUsesToggle[buttonIdx]){
    if(buttonToggleValue[buttonIdx]){
      // If botton is currently toggled on
      return(0x00);
    }
    return(0x7f);
  } else {
    return(BUTTON_MIDI_VALUES[buttonIdx]);
  }
}

/**
 * Runs throught the byte provided by a PCF8574 and sends a message if a button
 * has been pressed.
 * !!!!! Messages are only sent on button press not on resease !!!!!
 * @param pcfNumber The index of the pcf that provided the byte. Help map
 * each binary value to a specific button.
 * @param pcfByte The byte containing the state values for each of it's
 * attached buttons.
 */
void sendByteForMidi(int pcfNumber, byte pcfByte){
  for(int i=0;i<8;i++){
    if(!((pcfByte >> i) & 0x01)){  // !!!!!!! This means that a message is only sent when a button is pressed (not on release) !!!!!!!!
      int buttonIdx = (pcfNumber * 8) + i;
      MIDI.sendControlChange(
        BUTTON_CONTROLE_NUMBERS[buttonIdx],
        selectBtnValueToSend(buttonIdx),
        MIDI_CHANNEL
      );
    }
  }
}

/**
 * Reads the byte from the indecated PCF8574, and sends the MIDI message
 * associated with that button if needed.
 * @param pcfNb Index indicating which PCF8574 to read the values from
 * based on the values in the associated PCF I2C address array.
 */
void readPCF8574(int pcfNb) {
    Wire.requestFrom(PCF_ADDRESSES[pcfNb], (uint8_t)1);
    if (Wire.available()) {
        uint8_t newValue = Wire.read();
        if (newValue != pcfValues[pcfNb]) { // TODO: Check if this is redundent due to only read when interupt
          #ifdef DEBUG
            Serial.print("PCF8574 nb ");
            Serial.print(pcfNb);
            Serial.print(": ");
            Serial.println(newValue, BIN);
          #endif
          pcfValues[pcfNb] = newValue;
          sendByteForMidi(pcfNb, newValue);
        }
    }
}

void setup() {
  #ifdef DEBUG
  Serial.begin(115200);
  #endif

  ///// SETUP MIDI /////
  // Manual begin() is required on core without built-in support e.g. mbed rp2040
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usb_midi.setStringDescriptor(CONTROLER_NAME);
  // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  // Initialize MIDI, and listen to all MIDI channels
  // This will also call usb_midi's begin()
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleControlChange);

  // Attach the handleNoteOn function to the MIDI Library. It will
  // be called whenever the Bluefruit receives MIDI Note On messages.
  //MIDI.setHandleNoteOn(handleNoteOn);
  //MIDI.setHandleNoteOff(handleNoteOff);

  ///// INITIALIZE I2C /////
  //Wire.setSDA(4);
  //Wire.setSCL(5);
  Wire.begin();

  ///// INITIALIZE PCF8574 MODULES /////
  #ifdef DEBUG
    for (int i = 0; i < NB_PCF8574; i++) {

      // Release all PCF8574 pins (configure as inputs)
      Wire.beginTransmission(PCF_ADDRESSES[i]);
      Wire.write(0xFF);

      if (Wire.endTransmission() != 0) {
          Serial.println("PCF8574 not found!");
      } else {
          Serial.println("PCF8574 initialized!");
      }
    }
  #endif

  // Configure interrupt pin
  pinMode(PCF8574_INT_PIN0, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN1, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN2, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN3, INPUT_PULLUP);

  attachInterrupt(
      digitalPinToInterrupt(PCF8574_INT_PIN0),
      pcf0Interrupt,
      FALLING
  );

  attachInterrupt(                            
      digitalPinToInterrupt(PCF8574_INT_PIN1),
      pcf1Interrupt,                           
      FALLING                                 
  );

  attachInterrupt(                            
      digitalPinToInterrupt(PCF8574_INT_PIN2),
      pcf2Interrupt,                           
      FALLING                                 
  );

  attachInterrupt(                            
      digitalPinToInterrupt(PCF8574_INT_PIN3),
      pcf3Interrupt,                           
      FALLING                                 
  );

  ///// INITIALIZE ADS1115 MODULES /////
  for(int i=0;i<NB_TOTAL_ADS;i++){
    if(ads[i].begin(ADS_ADDRESSES[i], &Wire)){
      // Set ADC gain: +/- 4.096V
      ads[i].setGain(ADS_GAIN);
    }
  }

  ///// INITIALIZE LED STRIPS /////
  // TODO: Set startup annimation here
  #ifdef NB_LEDS
    strip.begin();
    strip.setBrightness(50);
    strip.show(); // Turn all LEDs off
    for (int i = 0; i < NB_LEDS; i++) {
      strip.setPixelColor(i, strip.Color((uint8_t)200, (uint8_t)0, (uint8_t)200));
    }
    strip.show();
  #endif
}

void loop() {
  #ifdef TINYUSB_NEED_POLLING_TASK
  // Manual call tud_task since it isn't called by Core's background
  TinyUSBDevice.task();
  #endif

  // not enumerated()/mounted() yet: nothing to do
  if (!TinyUSBDevice.mounted()) {
    return;
  }

  // Read ADS1115 periodically
  if (millis() - lastReadTime >= readInterval) {
    lastReadTime = millis();

    for(int i=0;i<NB_VAR_RESISTOR_ADS1;i++){
      // Read analog input A0
      int16_t adcValue = ads[0].readADC_SingleEnded(ADS1_INPUT[i]);
      // Convert ADC reading (0-32767) to MIDI (0-127) and keep value within MIDI range
      int newMidiValue = constrain(map(adcValue, 0, MAX_ADS_RANGE, 0, 127), 0, 127);;
      // Send MIDI only when value changes
      if (newMidiValue != lastCCValues[i]) {
        MIDI.sendControlChange(
          VAR_RESISTOR_CONTROLE_NUMBERS1[i],
          newMidiValue,
          MIDI_CHANNEL
        );
        lastCCValues[i] = newMidiValue;
        #ifdef DEBUG
          Serial.print("Knob: ");
          Serial.print(i);
          Serial.print(", val: ");
          Serial.println(newMidiValue);
        #endif
      }
    }

    for(int i=NB_VAR_RESISTOR_ADS1;i<NB_TOTAL_VAR_RESISTORS;i++){
      // Read analog input A0
      int16_t adcValue = ads[1].readADC_SingleEnded(ADS2_INPUT[i - NB_VAR_RESISTOR_ADS1]);
      // Convert ADC reading (0-32767) to MIDI (0-127) and keep value within MIDI range
      int newMidiValue = constrain(map(adcValue, 0, MAX_ADS_RANGE, 0, 127), 0, 127);
      // Send MIDI only when value changes
      if (newMidiValue != lastCCValues[i]) {
        MIDI.sendControlChange(
          VAR_RESISTOR_CONTROLE_NUMBERS2[i - NB_VAR_RESISTOR_ADS1],
          newMidiValue,
          MIDI_CHANNEL
        );
        lastCCValues[i] = newMidiValue;
        #ifdef DEBUG
          Serial.print("Knob: ");
          Serial.print(i);
          Serial.print(", val: ");
          Serial.println(newMidiValue);
        #endif
      }
    }
  }
  
  // Process PCF8574 interrupt
  for(int i=0;i<NB_PCF8574;i++){
    if (pcfInterruptFlags[i]) {
      noInterrupts();
      pcfInterruptFlags[i] = false;
      interrupts();
      readPCF8574(i);
    }
  }
  
  // read any new MIDI messages
  MIDI.read();

  // Send Note On for current position at full velocity (127) on channel 1.
  //MIDI.sendNoteOn(45, 127, 1);
  // Send Note Off for previous note.
  //MIDI.sendNoteOff(note_sequence[previous], 0, 1);
  // Send Control Change:
  // CC number 16, value 85, MIDI channel 1
  //MIDI.sendControlChange(16, 85, 1);
}
