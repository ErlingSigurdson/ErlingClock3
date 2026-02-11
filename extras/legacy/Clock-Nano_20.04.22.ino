// This sketch reads DS3231 RTC output and drives multiplexed 7-segment 4-digit LED display with 4 common cathodes.
// Between GND and each common cathode a small-signal NPN BJT is required.

#include <Adafruit_BusIO_Register.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_I2CRegister.h>
#include <RTClib.h>

RTC_DS3231 rtc;


// GENERIC GLOBAL VARIABLES

// For storing numbers from RTC output.
uint8_t RTC_hours;
uint8_t RTC_minutes;
uint8_t RTC_seconds;

// For storing digits to be displayed.
uint8_t hours_first_digit;
uint8_t hours_second_digit;
uint8_t minutes_first_digit;
uint8_t minutes_second_digit;
uint8_t seconds_first_digit;
uint8_t seconds_second_digit;


// GLOBAL VARIABLES RELATED TO USER-DEFINED FUNCTIONS

// dispay_digit()
  // Pins which drive transistors connecting common cathodes to GND
  // and thus switch the active display section ("D" is for "digit").
  const uint8_t D1_PIN = 12;
  const uint8_t D2_PIN = 11;
  const uint8_t D3_PIN = 10;
  const uint8_t D4_PIN = 9;

  // Pins which interact with 74HC595 (known as simply "595") latched shift register IC.
  // The 595 is used to create a set of 8 parallel output signals ("byte mask") that lights up only necessary segments at any given time.
  const uint8_t DATA_PIN = 6;
  const uint8_t LATCH_PIN = 7;
  const uint8_t CLOCK_PIN = 8;

// serial_output()
  uint8_t previous_RTC_seconds;                     // Reference point for finding out if seconds output has been changed, which entails sending data via UART.
  bool initial_RTC_seconds_was_stored = 0;          // Makes the code which evaluates initial previous_RTC_seconds run only once.

// manual_settime()
  bool settime_mode = 0;                            // Used to begin and end while() loops during which time may be set manually.

  // Pins for active-low manual input buttons.
  const uint8_t SETTIME_TOGGLE_PIN = A2;
  const uint8_t SETTIME_HOURS_PIN = A1;             // At first I wanted to use A7 and A6 for button pins, but turned out they can't be used as digital inputs on Nano.
  const uint8_t SETTIME_MINUTES_PIN = A0;

  // For button debounce.
  bool settime_toggle_button_is_pressed;
  bool settime_toggle_button_wasnt_pressed;
  bool settime_hours_button_is_pressed;
  bool settime_hours_button_wasnt_pressed;
  bool settime_minutes_button_is_pressed;
  bool settime_minutes_button_wasnt_pressed;

  // For storing temporary numbers to be loaded into RTC when settime mode is turned off.
  uint8_t settime_hours;
  uint8_t settime_minutes;
  uint8_t settime_seconds;


void setup() {
  rtc.begin();

  // May be handy for debugging purposes etc.
  Serial.begin(9600);

  // If necessary, uncomment following line to set RTC time. Then upload the sketch, comment the following line once again and then re-upload the sketch.
  // rtc.adjust(DateTime(2022, 4, 18, 12, 0, 0));  // year, month, date, hours, minutes, seconds

  pinMode(D1_PIN, OUTPUT);
  pinMode(D2_PIN, OUTPUT);
  pinMode(D3_PIN, OUTPUT);
  pinMode(D4_PIN, OUTPUT);
  
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  
  pinMode(SETTIME_TOGGLE_PIN, INPUT_PULLUP);
  pinMode(SETTIME_HOURS_PIN, INPUT_PULLUP);
  pinMode(SETTIME_MINUTES_PIN, INPUT_PULLUP);

  // Prevents artifacts from being displayed immediately after boot.
  digitalWrite(D1_PIN, 0);
  digitalWrite(D2_PIN, 0);
  digitalWrite(D3_PIN, 0);
  digitalWrite(D4_PIN, 0);
}


void loop() {
  DateTime now = rtc.now();

  RTC_hours = now.hour(), DEC;
  RTC_minutes = now.minute(), DEC;
  RTC_seconds = now.second(), DEC;

  // Division and modulo operators are used
  // to separate numbers into first and second digit.
  hours_first_digit = RTC_hours / 10;
  hours_second_digit = RTC_hours % 10;
  minutes_first_digit = RTC_minutes / 10;
  minutes_second_digit = RTC_minutes % 10;
  seconds_first_digit = RTC_seconds / 10;
  seconds_second_digit = RTC_seconds % 10;

  // Display function callers.
  display_digit(D1_PIN, hours_first_digit, 0);
  display_digit(D2_PIN, hours_second_digit, 1);    // Decimal point is used to visually separate hours from minutes and indicate that the clock is running.
  display_digit(D3_PIN, minutes_first_digit, 0);
  display_digit(D4_PIN, minutes_second_digit, 0);

  // Serial output function caller.
  serial_output();

  // Turn on settime mode.
  settime_toggle_button_is_pressed = !digitalRead(SETTIME_TOGGLE_PIN);
  if (settime_toggle_button_is_pressed && settime_toggle_button_wasnt_pressed) {
    delay(10);
    settime_toggle_button_is_pressed = !digitalRead(SETTIME_TOGGLE_PIN);
    if (settime_toggle_button_is_pressed) {
      manual_settime();                            // Settime function caller.
    }
  }
  settime_toggle_button_wasnt_pressed = !settime_toggle_button_is_pressed;
}


void display_digit(int current_cathode, int digit_to_display, bool whether_dot_is_used) {  // User-defined function dedicated to actually displaying digits.

  // Defines which display section (digit) is to be turned on right now.
  digitalWrite(D1_PIN, current_cathode == D1_PIN ? 1 : 0);
  digitalWrite(D2_PIN, current_cathode == D2_PIN ? 1 : 0);
  digitalWrite(D3_PIN, current_cathode == D3_PIN ? 1 : 0);
  digitalWrite(D4_PIN, current_cathode == D4_PIN ? 1 : 0);

  // Byte masks for digits from 0 to 9.
  uint8_t output_matrix[] = {
    0b11101110,  // 0
    0b00000110,  // 1
    0b11100011,  // 2
    0b01100111,  // 3
    0b00001111,  // 4
    0b01101101,  // 5
    0b11101101,  // 6
    0b00100110,  // 7
    0b11101111,  // 8
    0b01101111   // 9
  };

  bool ifblink = whether_dot_is_used && seconds_second_digit % 2 == 0 ? 1 : 0;                // If third argument of function holds 1, decimal point will blink once per second.

  // Displaying.
  digitalWrite(LATCH_PIN, 0);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, output_matrix[digit_to_display] | ifblink << 4);    // 4th bit is the dot bit.
  digitalWrite(LATCH_PIN, 1);

  // Anti-ghosting sequence.
  delay(4);  // Anti-ghosting delay
  digitalWrite(LATCH_PIN, 0);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, 0b00000000); // Turns all display sections off for one clock period.
  digitalWrite(LATCH_PIN, 1);
}


void serial_output() {  // Optional user-defined function. Used for sending RTC output via UART for debugging purposes etc.
  if (!initial_RTC_seconds_was_stored) {
    previous_RTC_seconds = RTC_seconds;
    initial_RTC_seconds_was_stored = 1;
  }

  if (RTC_seconds != previous_RTC_seconds) {
    Serial.print(RTC_hours);
    Serial.print(":");
    Serial.print(RTC_minutes);
    Serial.print(":");
    Serial.println(RTC_seconds);
    previous_RTC_seconds = RTC_seconds;
  }
}


void manual_settime() {  // Settime mode allows setting time manually without neither using IDE nor resetting the MCU.
  // This stuff is executed just once, after that function gets caught into a while() loop.
  settime_mode = true;
  settime_hours = 12;
  settime_minutes = 0;
  settime_seconds = 0;
  Serial.print(settime_hours);
  Serial.print(":");
  Serial.print(settime_minutes);
  Serial.print(":");
  Serial.println(settime_seconds);
  settime_toggle_button_wasnt_pressed = !settime_toggle_button_is_pressed;
      
  // While settime mode is on.
  while (settime_mode) {
    
    // Set hours.
    settime_hours_button_is_pressed = !digitalRead(SETTIME_HOURS_PIN);
    if (settime_hours_button_is_pressed && settime_hours_button_wasnt_pressed) {
      delay(10);
      settime_hours_button_is_pressed = !digitalRead(SETTIME_HOURS_PIN);
      if (settime_hours_button_is_pressed) {
        ++settime_hours;
        if (settime_hours > 23) {
          settime_hours = 0;
        }
        Serial.print(settime_hours);
        Serial.print(":");
        Serial.print(settime_minutes);
        Serial.print(":");
        Serial.println(settime_seconds);
      }
    }
    settime_hours_button_wasnt_pressed = !settime_hours_button_is_pressed;

    // Set minutes.
    settime_minutes_button_is_pressed = !digitalRead(SETTIME_MINUTES_PIN);
    if (settime_minutes_button_is_pressed && settime_minutes_button_wasnt_pressed) {
      delay(10);
      settime_minutes_button_is_pressed = !digitalRead(SETTIME_MINUTES_PIN);
      if (settime_minutes_button_is_pressed) {
        ++settime_minutes;
        if (settime_minutes > 59) {
          settime_minutes = 0;
        }
        Serial.print(settime_hours);
        Serial.print(":");
        Serial.print(settime_minutes);
        Serial.print(":");
        Serial.println(settime_seconds);
      }
    }
    settime_minutes_button_wasnt_pressed = !settime_minutes_button_is_pressed;

    // Same as outside settime while() loop, but calculated from temporary settime numbers, not actual RTC output.
    hours_first_digit = settime_hours / 10;
    hours_second_digit = settime_hours % 10;
    minutes_first_digit = settime_minutes / 10;
    minutes_second_digit = settime_minutes % 10;
    seconds_first_digit = settime_seconds / 10;
    seconds_second_digit = settime_seconds % 10;

    // Same as outside settime while() loop.
    display_digit(D1_PIN, hours_first_digit, 0);
    display_digit(D2_PIN, hours_second_digit, 0);    // Decimal point is off to indicate that settime mode is on.
    display_digit(D3_PIN, minutes_first_digit, 0);
    display_digit(D4_PIN, minutes_second_digit, 0);
   
    // Turn off settime mode.
    settime_toggle_button_is_pressed = !digitalRead(SETTIME_TOGGLE_PIN);
    if (settime_toggle_button_is_pressed && settime_toggle_button_wasnt_pressed) {
      delay(10);
      settime_toggle_button_is_pressed = !digitalRead(SETTIME_TOGGLE_PIN);
      if (settime_toggle_button_is_pressed) {
        rtc.adjust(DateTime(2022, 4, 18, settime_hours, settime_minutes, settime_seconds));  // year, month, date, hours, minutes, seconds
        settime_mode = false;
      }
    }
    settime_toggle_button_wasnt_pressed = !settime_toggle_button_is_pressed;
  }
}
