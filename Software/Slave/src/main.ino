#include <Arduino.h>
#include <DMXSerial.h>
#define dimmerPin 9
#define RS485_ERROR_PIN 2
#define STATUS_PIN 5

#define CHANNEL_PIN_B1 6
#define CHANNEL_PIN_B2 7
#define CHANNEL_PIN_B3 8
#define CHANNEL_PIN_B4 9


int dimLevel = 0;
int currentChannel = 0;
unsigned long lastCurrentChannelRead = 0;
unsigned long lastLightDimmingEvent = 0;

// Set 'TOP' for PWM resolution.  Assumes 16 MHz clock.
// const unsigned int TOP = 0xFFFF; // 16-bit resolution.   244 Hz PWM
// const unsigned int TOP = 0x7FFF; // 15-bit resolution.   488 Hz PWM
// const unsigned int TOP = 0x3FFF; // 14-bit resolution.   976 Hz PWM
// const unsigned int TOP = 0x1FFF; // 13-bit resolution.  1953 Hz PWM
// const unsigned int TOP = 0x0FFF; // 12-bit resolution.  3906 Hz PWM
// const unsigned int TOP = 0x07FF; // 11-bit resolution.  7812 Hz PWM
const unsigned int TOP = 0x03FF; // 10-bit resolution. 15624 Hz PWM

void PWM16Begin()
{
  // Stop Timer/Counter1
  TCCR1A = 0;  // Timer/Counter1 Control Register A
  TCCR1B = 0;  // Timer/Counter1 Control Register B
  TIMSK1 = 0;   // Timer/Counter1 Interrupt Mask Register
  TIFR1 = 0;   // Timer/Counter1 Interrupt Flag Register
  ICR1 = TOP;
  OCR1A = 0;  // Default to 0% PWM
  OCR1B = 0;  // Default to 0% PWM


  // Set clock prescale to 1 for maximum PWM frequency
  TCCR1B |= (1 << CS10);


  // Set to Timer/Counter1 to Waveform Generation Mode 14: Fast PWM with TOP set by ICR1
  TCCR1A |= (1 << WGM11);
  TCCR1B |= (1 << WGM13) | (1 << WGM12) ;
}


void PWM16EnableA()
{
  // Enable Fast PWM on Pin 9: Set OC1A at BOTTOM and clear OC1A on OCR1A compare
  TCCR1A |= (1 << COM1A1);
  pinMode(9, OUTPUT);
}


void PWM16EnableB()
{
  // Enable Fast PWM on Pin 10: Set OC1B at BOTTOM and clear OC1B on OCR1B compare
  TCCR1A |= (1 << COM1B1);
  pinMode(10, OUTPUT);
}


inline void PWM16A(unsigned int PWMValue)
{
  OCR1A = constrain(PWMValue, 0, TOP);
}


inline void PWM16B(unsigned int PWMValue)
{
  OCR1B = constrain(PWMValue, 0, TOP);
}

void readCurrentChannelSwitches() {
  int channel = 0;

  if(digitalRead(CHANNEL_PIN_B1) == LOW) {
    channel = 1;
  }
  if(digitalRead(CHANNEL_PIN_B2) == LOW) {
    channel = (1 << 1) | channel;
  }
  if(digitalRead(CHANNEL_PIN_B3) == LOW) {
    channel = (1 << 2) | channel;
  }
  if(digitalRead(CHANNEL_PIN_B4) == LOW) {
    channel = (1 << 3) | channel;
  }
  // Add +1 to read the correct register in the DMX library
  currentChannel = channel;
  lastCurrentChannelRead = millis();
}


void setup() {
  DMXSerial.init(DMXReceiver);
  for(int i = 0; i < 15; i++) {
    DMXSerial.write(i + 1, 0);
  }
  pinMode(RS485_ERROR_PIN, OUTPUT);
  pinMode(STATUS_PIN, OUTPUT);
  digitalWrite(STATUS_PIN, HIGH);

  pinMode(CHANNEL_PIN_B1, INPUT_PULLUP);
  pinMode(CHANNEL_PIN_B2, INPUT_PULLUP);
  pinMode(CHANNEL_PIN_B3, INPUT_PULLUP);
  pinMode(CHANNEL_PIN_B4, INPUT_PULLUP);
  readCurrentChannelSwitches();

  PWM16B(0);
  PWM16Begin();
  PWM16EnableB();
}

void loop() {
  // Get the currently set channels from the DIP-switches.
  if(millis() - lastCurrentChannelRead > 1000) {
    readCurrentChannelSwitches();
  }

  if(currentChannel > 0) {
    // Calculate how long since the last DMX data was received.
    unsigned long lastPacket = DMXSerial.noDataSince();

    if (lastPacket < 5000) {
      // Convert sent value with a curve to the high value corresponding to the bit-mode of the PWM
      //dimLevel = round(pow((DMXSerial.read(currentChannel) * 0.25), 2));  // Convert to 12-bit
      dimLevel = round(pow((DMXSerial.read(currentChannel) * 0.125), 2));  // Convert to 10-bit
    } else if (dimLevel > 0) {
      if(millis() - lastLightDimmingEvent > 100) {
        dimLevel--;
        lastLightDimmingEvent = millis();
      }
    }
    PWM16B(dimLevel);

    if(lastPacket > 500) {
      digitalWrite(RS485_ERROR_PIN, HIGH);
    } else {
      digitalWrite(RS485_ERROR_PIN, LOW);
    }

    // Blink the status LED to show that it is running
    digitalWrite(STATUS_PIN, (millis() / 1000) % 2 == 0 ? HIGH : LOW);
  } else {
    // Dim down the lights as not channel is configured.
    if(millis() - lastLightDimmingEvent > 10 && dimLevel > 0) {
      dimLevel--;
      lastLightDimmingEvent = millis();
      PWM16B(dimLevel);
    }

    // Show an error as no channel has been set.
    digitalWrite(STATUS_PIN, HIGH);

    // Blink RS485-error led fast to show that no channel has been set.
    digitalWrite(RS485_ERROR_PIN, (millis() / 100) % 2 == 0 ? HIGH : LOW);
  }
}