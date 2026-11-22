#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Adafruit_NeoPixel.h>


#define CONTROLER_NAME "Pico Controller"
/////////////// NB CONTROLE ELEMENTS ///////////////
#define NB_BUTTONS 32
// NOTE: If adding more potentiometers it may slow rotary encoder read
#define NB_POTS 6
#define NB_ROTARY_ENCODERS 6
//#define NB_LEDS 16  // Comment this to remove all LED related code
/////////////// NB PCF8574 AND MUX ///////////////
#define NB_PCF8574_FOR_BTNS 5
#define NB_PCF_FOR_ENCODERS 2//(NB_ROTARY_ENCODERS / 4 + NB_ROTARY_ENCODERS % 4 > 0 ? 1 : 0)
#define MUX_SIZE 16
#define NB_MUX (int)((NB_POTS / MUX_SIZE) + 1)
/////////////// PIN DEFINITIONS ///////////////
#define LED_PIN 16
#define PCF8574_INT_PIN0 0
#define PCF8574_INT_PIN1 1
#define PCF8574_INT_PIN2 2
#define PCF8574_INT_PIN3 3
#define PCF8574_INT_PIN4 6
#define PCF8574_ENCODER_INT_PIN0 7
#define PCF8574_ENCODER_INT_PIN1 8
#define MUX_S0_PIN 18
#define MUX_S1_PIN 19
#define MUX_S2_PIN 20
#define MUX_S3_PIN 21
/////////////// I2C ADDRESS DEFINITIONS ///////////////
#define PCF8574_ADDR0 0x20
#define PCF8574_ADDR1 0x21
#define PCF8574_ADDR2 0x22
#define PCF8574_ADDR3 0x23
#define PCF8574_ADDR4 0x24 // TODO: Add this to main btn address array
#define PCF8574_ENCODER_ADDR0 0x25
#define PCF8574_ENCODER_ADDR1 0x26
// TODO: Check to see if all values from the ADS can be read in a single I2C request.
#define ADS1115_ADDR 0x48
/////////////// OTHER PARAMETERS ///////////////
#define ADC_POLL_RATE 10 // In ms
#define ENCODER_POLL_RATE 10 // In ms
#define ENCODER_MSG_INTERVAL 30 // In ms
#define MAX_ADS_RANGE 26335//32767
#define ADS_CHANGE_MARGIN 10 // Used to filter noise
/////////////// MESSAGE CODE DEFINITIONS ///////////////
#define MSG_BTN_CODE 1
#define MSG_POT_CODE 2
#define MSG_ENCODER_CODE 3
/////////////// ADS1115 CONFIGURATIONS ///////////////
// And gain with MSB
#define ADS_READ_ADDR_POINTER 0x00
#define ADS_CONFIG_ADDR 0x01
#define ADS_GAIN1 0x00
#define ADS_GAIN2 0x02
#define ADS_GAIN3 0x04
#define ADS_CHAN0 0x40
#define ADS_CHAN1 0x50
#define ADS_CHAN2 0x60
#define ADS_CHAN3 0x70
#define ADS_BASE_CONFIG_MSB ADS_GAIN2//0x42
#define ADS_BASE_CONFIG_LSB 0x83
/////////////// UART CODES ///////////////
#define UART_CODE_BTN 0x62            // (b)
#define UART_CODE_ENCODER 0x65        // (e)
#define UART_CODE_POT 0x70            // (p)
#define UART_CODE_ENCODER_PLUS 0x2b   // (+)
#define UART_CODE_ENCODER_MINUS 0x2d  // (-)

// Define constand arrays
const byte PCF_BTN_ADDRESSES[NB_PCF8574_FOR_BTNS] = {
  PCF8574_ADDR0, PCF8574_ADDR1, PCF8574_ADDR2, PCF8574_ADDR3, PCF8574_ADDR4
};

const byte PCF_ENCODER_ADDR[NB_PCF_FOR_ENCODERS] = {
  PCF8574_ENCODER_ADDR0, PCF8574_ENCODER_ADDR1
};

// MIDI settings
const byte MIDI_ENCODER_CHANNEL[NB_ROTARY_ENCODERS] = {
  1, 1, 1, 1, 1, 1
};

const byte MIDI_ENCODER_VELOCITY[NB_ROTARY_ENCODERS] = {
  0, 1, 2, 3, 4, 5
};

//const byte MIDI_CC = 16;
const byte MIDI_BTN_CHANNELS[NB_BUTTONS + NB_ROTARY_ENCODERS] = {
  5, 5, 5, 5, 6, 6, 6, 6, // Loop in, loop out loop jump -, +
  5, 5, 5, 5, 5, 5, 5, 5, // Loop cue deck 1
  6, 6, 6, 6, 6, 6, 6, 6, // Loop cue deck 2
  //7, 7, 7, 7, 7, 7, 7, 7, // Loop cue deck 3
  //8, 8, 8, 8, 8, 8, 8, 8, // Loop cue deck 4
  9, 9, 9, 10, 10, 10, 0, 0, // FX toggle /
  0, 0, 0, 0, 0, 0 // Rotary encoder buttons
};

// The channels used for the potentiometers
const byte MIDI_POT_CHANNELS[NB_POTS] = {
  9, 9, 9, 10, 10, 10
};

// ADS1115 configuration
// Defines the controle number for each of the potentiometers for ADS1
const byte MIDI_POT_CONTROLE_NUMBERS[NB_POTS] = {
  0, 1, 2, 0, 1, 2
};

// The MIDI controle number associated with each button
const byte MIDI_BUTTON_CONTROLE_NUMBERS[NB_BUTTONS + NB_ROTARY_ENCODERS] = {
  21, 22, 73, 74, 21, 22, 73, 74, // Loop in, loop out loop jump -, +
  1, 2, 3, 4, 5, 6, 7, 8, // Loop cue deck 1
  1, 2, 3, 4, 5, 6, 7, 8, // Loop cue deck 2
  //2, 3, 4, 5, 6, 7, 8, 9, // Loop cue deck 3
  //2, 3, 4, 5, 6, 7, 8, 9, // Loop cue deck 4
  0, 1, 2, 0, 1, 2, // Toggle FX
  0, 1, 2, 3, 4, 5 // Rotary encoder buttons
};

// ADC polling interval
const unsigned long readInterval = ADC_POLL_RATE;
unsigned long lastPotReadTime = 0;

// Rotary encoder debounce periode
const unsigned long encoderReadInterval = ENCODER_POLL_RATE;
unsigned long lastEncoderReadTime[NB_ROTARY_ENCODERS] = {0};

// The interval of time between rotary encoder message sending
const unsigned long encoderMsgInterval = ENCODER_MSG_INTERVAL;
unsigned long encoderLastMsgTime = 0;

// MIDI definitions
// Last recorded potentiometer value
uint16_t lastPotVal[NB_POTS] = {0};

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

///// DEFINE PCF8574 VARIABLES ////
volatile bool pcfInterruptFlags[NB_PCF8574_FOR_BTNS] = {false};

volatile bool pcfEncoderInterruptFlags[NB_PCF_FOR_ENCODERS] = {false};

// Last recorded state of the button
bool buttonValues[NB_BUTTONS] = {false};

// Last recorded state for each rotary encoder
int lastEncoderValues[NB_ROTARY_ENCODERS] = {0x7f};

void pcf0Interrupt(){
    pcfInterruptFlags[0] = true;
}

void pcf1Interrupt(){
    pcfInterruptFlags[1] = true;
}

void pcf2Interrupt(){
    pcfInterruptFlags[2] = true;
}

void pcf3Interrupt(){
    pcfInterruptFlags[3] = true;
}

// NOTE: This pcf has the buttons for the rotary encoders
void pcf4Interrupt(){
  pcfInterruptFlags[4] = true;
}

// Rotary encoder PCF interupts
void encoderPcf0Interrupt(){
  pcfEncoderInterruptFlags[0] = true;
}

void encoderPcf1Interrupt(){
  pcfEncoderInterruptFlags[1] = true;
}

/**
 * Selects the multiplexer channel to read from
 * @param channel The channel to set both multiplexers to.
 */
void selectMUXChannel(int channel){
  digitalWrite(MUX_S0_PIN, channel & 0x1);
  digitalWrite(MUX_S1_PIN, (channel >> 1) & 0x1);
  digitalWrite(MUX_S2_PIN, (channel >> 2) & 0x1);
  digitalWrite(MUX_S3_PIN, (channel >> 3) & 0x1);
  //uint32_t mask = (channel & 0xF) << 18;
  //sio_hw->gpio_set = mask;
  //sio_hw->gpio_clr = (~mask) & 0x000C0000u;  // Sets pins low
}

void setup() {
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

  // Configure interrupt pin
  pinMode(PCF8574_INT_PIN0, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN1, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN2, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN3, INPUT_PULLUP);
  pinMode(PCF8574_INT_PIN4, INPUT_PULLUP);
  pinMode(PCF8574_ENCODER_INT_PIN0, INPUT_PULLUP);
  pinMode(PCF8574_ENCODER_INT_PIN1, INPUT_PULLUP);
  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);

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

  attachInterrupt(                            
      digitalPinToInterrupt(PCF8574_INT_PIN4),
      pcf4Interrupt,                           
      FALLING                                 
  );

  attachInterrupt(                            
    digitalPinToInterrupt(PCF8574_ENCODER_INT_PIN0),
    encoderPcf0Interrupt,                           
    FALLING                                 
  );

  attachInterrupt(                            
      digitalPinToInterrupt(PCF8574_ENCODER_INT_PIN1),
      encoderPcf1Interrupt,                           
      FALLING                                 
  );

  ///// INITIALIZE ADS1115 MODULES /////
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_CONFIG_ADDR);          // Write to config register
  Wire.write(ADS_BASE_CONFIG_MSB | ADS_CHAN0); // MSB
  Wire.write(ADS_BASE_CONFIG_LSB);             // LSB
  Wire.endTransmission();

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
  if (millis() - lastPotReadTime >= readInterval) {
    lastPotReadTime = millis();
    for(int i=0;i<NB_POTS;i++){
      selectMUXChannel(i);
      // Point to Conversion register
      Wire.beginTransmission(ADS1115_ADDR);
      Wire.write(ADS_READ_ADDR_POINTER);         // pointer → Conversion
      Wire.endTransmission();
      // Read 2 bytes (MSB, LSB)
      Wire.requestFrom(ADS1115_ADDR, (uint8_t)2);
      int16_t msb = Wire.read();
      int16_t lsb = Wire.read();
      int16_t adcValue = (int16_t)((msb << 8) | lsb);
      // Send MIDI only when value changes
      if(abs(adcValue - lastPotVal[i]) > ADS_CHANGE_MARGIN){
        rp2040.fifo.push(MSG_POT_CODE);
        rp2040.fifo.push(i);
        rp2040.fifo.push(adcValue);
        lastPotVal[i] = adcValue;
      }
    }
  }
  
  // Process PCF8574 interrupt
  for(int pcfId=0;pcfId<NB_PCF8574_FOR_BTNS;pcfId++){
    if(pcfInterruptFlags[pcfId]){
      noInterrupts();
      pcfInterruptFlags[pcfId] = false;
      interrupts();
      Wire.requestFrom(PCF_BTN_ADDRESSES[pcfId], (uint8_t)1);
      if (Wire.available()){
          rp2040.fifo.push(MSG_BTN_CODE);
          rp2040.fifo.push(pcfId);
          rp2040.fifo.push(Wire.read());
      }
    }
  }
  
  // Process PCF8574 rotary encoder interupts
  for(int pcfId=0;pcfId<NB_PCF_FOR_ENCODERS;pcfId++){
    if(millis() - lastEncoderReadTime[pcfId] >= encoderReadInterval && pcfEncoderInterruptFlags[pcfId]){
      lastEncoderReadTime[pcfId] = millis();
      noInterrupts();
      pcfEncoderInterruptFlags[pcfId] = false;
      interrupts();
      Wire.requestFrom(PCF_ENCODER_ADDR[pcfId], (uint8_t)1);
      if (Wire.available()){
        int pcfValue = Wire.read();
        // Loop through all paires of encoder values
        int byteShiftAmount = 0;
        for(int encoderPcfId=0;encoderPcfId<4;encoderPcfId++){
          int encoderId = (pcfId * 4) + encoderPcfId;
          uint8_t encoderVal = (pcfValue >> byteShiftAmount) & 0x03;
          if(encoderVal != lastEncoderValues[encoderId] && millis() - encoderLastMsgTime >= encoderMsgInterval){
            encoderLastMsgTime = millis();
            switch((lastEncoderValues[encoderId] << 2) | encoderVal){
              // Rotation to the right
              case 0b1101:
              case 0b0100:
              case 0b0010:
              //case 0b1011:
                rp2040.fifo.push(MSG_ENCODER_CODE);
                rp2040.fifo.push(encoderId);
                rp2040.fifo.push(UART_CODE_ENCODER_PLUS);
                //rp2040.fifo.push(1);
                break;
              // Rotation to the left
              case 0b1110:
              case 0b1000:
              case 0b0001:
              //case 0b0111:
                rp2040.fifo.push(MSG_ENCODER_CODE);
                rp2040.fifo.push(encoderId);
                rp2040.fifo.push(UART_CODE_ENCODER_MINUS);
                //rp2040.fifo.push(0);
                break;
            }
          }
          lastEncoderValues[encoderId] = encoderVal;
          byteShiftAmount += 2;
        }
      }
    }
  }
}

/////////////////////////////////// Core 1 ///////////////////////////

void setup1() {
  // NOTE: Using both serial and midi means debug output does not work
  // as pico only hase UART0 and UART1
  Serial.begin(115200);
}

void loop1() {
  if (rp2040.fifo.available()) {
    uint32_t msgType = rp2040.fifo.pop();
    /*uint32_t*/uint8_t pcfId = rp2040.fifo.pop();
    /*uint32_t*/uint8_t message = rp2040.fifo.pop();
    switch(msgType){
      case 1:  // Send button related messages
        for(int i=0;i<8;i++){
          bool btnValue = !((message >> i) & 0x01); // Reversed as default PCF8574 values are 1
          int buttonIdx = (pcfId * 8) + i;
          if(btnValue != buttonValues[buttonIdx]){
            byte message;
            if(btnValue){
              message = 0x7f;
            } else {
              message = 0x00;
            }
            MIDI.sendNoteOn(
              message,
              MIDI_BUTTON_CONTROLE_NUMBERS[buttonIdx],
              MIDI_BTN_CHANNELS[buttonIdx]
            );
            //uint8_t data[3] = {UART_CODE_BTN, (uint8_t)buttonIdx, message};
            Serial.write(new uint8_t[3]{UART_CODE_BTN, buttonIdx, message}, 3);
            Serial.flush();
            buttonValues[buttonIdx] = btnValue;
          }
        }
        break;
      case 2:
        // Here pcfId just represents the pot number.
        MIDI.sendControlChange(
          MIDI_POT_CONTROLE_NUMBERS[pcfId],
          constrain(map(message, 0, MAX_ADS_RANGE, 0, 127), 0, 127),
          MIDI_POT_CHANNELS[pcfId]
        );
        //uint8_t data[3] = {UART_CODE_POT, (uint8_t)pcfId, message};
        Serial.write(new uint8_t[3]{UART_CODE_POT, pcfId, message}, 3);
        Serial.flush();
        break;
      case 3:
        MIDI.sendNoteOn(
          message,
          MIDI_ENCODER_VELOCITY[pcfId],
          MIDI_ENCODER_CHANNEL[pcfId]
        );
        //uint8_t data[3] = {UART_CODE_ENCODER, (uint8_t)pcfId, message};
        Serial.write(new uint8_t[3]{UART_CODE_ENCODER, pcfId, message}, 3);
        Serial.flush();
        break;
    }
  }
  // read any new MIDI messages
  MIDI.read();
}