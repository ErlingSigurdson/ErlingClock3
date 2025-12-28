/*************** FILE DESCRIPTION ***************/

/**
 * Filename: ErlingClock1.ino
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Purpose:  The main file of the firmware (the Arduino sketch) written for
 *           the Arduino Pro Mini-based electronic clock I made in 2022.
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Notes:
 */


/************ PREPROCESSOR DIRECTIVES ***********/

/*--- Includes ---*/

// Display driver.
#include <Drv7SegQ595.h>
#include <SegMap595.h>

// Buttons.
#include <GyverIO.h>
#include <uButton.h>

// RTC interfacing.
#include <Wire.h>
#include <DS3231.h>


/*--- Drv7SegQ595 library API parameters ---*/

#define POS_SWITCH_TYPE Drv7SegActiveHigh

#define DATA_PIN  4
#define LATCH_PIN 2
#define CLOCK_PIN 3

#define POS_1_PIN 9
#define POS_2_PIN 8
#define POS_3_PIN 7
#define POS_4_PIN 6

#define ANTI_GHOSTING_RETENTION_DURATION 1500


/*--- SegMap595 library API parameters ---*/

#define MAP_STR "DA@FCBGE"
#define DISPLAY_COMMON_PIN SegMap595CommonCathode


/*--- Buttons ---*/

#define BTN_1_PIN 10
#define BTN_2_PIN 12
#define BTN_3_PIN 11


/*--- Counters and timing ---*/

#define INTERVAL                     1000  // In milliseconds.
#define I2C_READ_INTERVAL_MULTIPLIER 10

#define MAX_COUNT_HOURS   24
#define MAX_COUNT_MINUTES 60
#define MAX_COUNT_SECONDS 60


/*--- UART ---*/

#define SERIAL_OUTPUT_ENABLED  // Comment out or delete to suppress the output of current timer values via UART.
#define BAUD_RATE 115200


/****************** DATA TYPES ******************/

struct current_time_t {
    uint32_t raw_hours;
    uint32_t raw_minutes;
    uint32_t raw_seconds;

    struct hours_t {
        size_t tens;  // size_t is used because this and following values are used as indices.
        size_t ones;
    }; 

    struct minutes_t {
        size_t tens;
        size_t ones;
    };

    struct seconds_t {
        size_t tens;
        size_t ones;
    };
    
    hours_t   hours;
    minutes_t minutes;
    seconds_t seconds;
};


/*************** GLOBAL VARIABLES ***************/

uButton btn_1(BTN_1_PIN);
uButton btn_2(BTN_2_PIN);
uButton btn_3(BTN_3_PIN);


/************** FUNCTION PROTOTYPES *************/

/*--- Multiplexing-friendly I/O wrappers ---*/

namespace mp_safe_io {
    // I2C.
    void read_rtc_time(DS3231& RTC, current_time_t& CurrentTime);
    void write_rtc_time(DS3231& RTC, current_time_t& CurrentTime);

    // UART.
    void serial_print(const char* msg);
    void serial_print(size_t val);
    void serial_print(uint32_t val);
}


/*--- Misc ---*/

void decompose_rtc_time(current_time_t& CurrentTime);
void time_setting_mode(DS3231& RTC, current_time_t& CurrentTime, bool& time_setting_mode_flag);


/******************* FUNCTIONS ******************/

/*--- Basic functions ---*/

void setup()
{
    /*--- Interface initialization ---*/

    #ifdef SERIAL_OUTPUT_ENABLED
    Serial.begin(BAUD_RATE);
    #endif
    
    Wire.begin();


    /*--- Byte mapping ---*/

    int32_t mapping_status = SegMap595.init(MAP_STR, DISPLAY_COMMON_PIN);

    if (mapping_status < 0) {
        while(true) {
            Serial.print("Error: mapping failed, error code ");
            Serial.println(mapping_status);
            delay(INTERVAL);
        }
    }


    /*--- Driver object configuration ---*/

    int32_t drv_config_status = Drv7Seg.begin_bb(POS_SWITCH_TYPE,
                                                 DATA_PIN, LATCH_PIN, CLOCK_PIN,
                                                 POS_1_PIN,
                                                 POS_2_PIN,
                                                 POS_3_PIN,
                                                 POS_4_PIN
                                                );

    if (drv_config_status < 0) {
        while(true) {
            Serial.print("Error: driver configuration failed, error code ");
            Serial.println(drv_config_status);
            delay(INTERVAL);
        }
    }

    #ifdef ANTI_GHOSTING_RETENTION_DURATION
    Drv7Seg.set_anti_ghosting_retention_duration(ANTI_GHOSTING_RETENTION_DURATION);
    #endif
}

void loop()
{
    DS3231 RTC;
    static current_time_t CurrentTime = {};  // Initialize with all-zero values.


    /*--- Counter and value update trigger ---*/

    // Counter.
    uint32_t current_millis = millis();
    static uint32_t previous_millis = current_millis;

    static uint32_t updates = 0;

    if (CurrentTime.raw_seconds >= MAX_COUNT_SECONDS) {
        CurrentTime.raw_minutes++;
        CurrentTime.raw_seconds = 0;
    }

    if (CurrentTime.raw_minutes >= MAX_COUNT_MINUTES) {
        CurrentTime.raw_hours++;
        CurrentTime.raw_minutes = 0;
    }

    if (CurrentTime.raw_hours >= MAX_COUNT_HOURS) {
        CurrentTime.raw_hours = 0;
    }

    // Value update trigger.
    static bool update_output_due = true;
    static bool update_i2c_due = true;


    /*--- Time update and output ---*/

    if (update_i2c_due) {
        mp_safe_io::read_rtc_time(RTC, CurrentTime);
        update_i2c_due = false;
    }

    if (update_output_due) {
        decompose_rtc_time(CurrentTime);

        uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(CurrentTime.hours.tens);
        uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(CurrentTime.hours.ones);
        uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(CurrentTime.minutes.tens);
        uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(CurrentTime.minutes.ones);

        // Dot-segment blink.
        if (CurrentTime.raw_seconds % 2) {
            static int32_t dot_bit_pos = SegMap595.get_dot_bit_pos();
            if (dot_bit_pos >= 0) {
                uint8_t mask = static_cast<uint8_t>(1u << dot_bit_pos);
                seg_byte_pos_2 ^= mask;
            }
        }

        Drv7Seg.set_glyph_to_pos(seg_byte_pos_1, Drv7SegPos1);
        Drv7Seg.set_glyph_to_pos(seg_byte_pos_2, Drv7SegPos2);
        Drv7Seg.set_glyph_to_pos(seg_byte_pos_3, Drv7SegPos3);
        Drv7Seg.set_glyph_to_pos(seg_byte_pos_4, Drv7SegPos4);

        #ifdef SERIAL_OUTPUT_ENABLED
            mp_safe_io::serial_print("Timer values (minutes and seconds): ");
            mp_safe_io::serial_print(CurrentTime.hours.tens);
            mp_safe_io::serial_print(CurrentTime.hours.ones);
            mp_safe_io::serial_print(":");
            mp_safe_io::serial_print(CurrentTime.minutes.tens);
            mp_safe_io::serial_print(CurrentTime.minutes.ones);
            mp_safe_io::serial_print(":");
            mp_safe_io::serial_print(CurrentTime.seconds.tens);
            mp_safe_io::serial_print(CurrentTime.seconds.ones);
            mp_safe_io::serial_print("\r\n");
        #endif

        update_output_due = false;
    }

    // Basic output call spot (it's not the only one).
    Drv7Seg.output_all();


    /*--- Counter and value update trigger, continued ---*/

    if (current_millis - previous_millis >= INTERVAL) {
        CurrentTime.raw_seconds++;
        update_output_due = true;
        ++updates;
        previous_millis = current_millis;
    }

    if (updates >= I2C_READ_INTERVAL_MULTIPLIER) {
        update_i2c_due = true;
        updates = 0;
    }


    /*--- Time setting mode control ---*/

    static bool time_setting_mode_flag = false;

    if (btn_1.tick()) {
        if (btn_1.press()) {
            time_setting_mode_flag = true;
        }
    }

    if (time_setting_mode_flag) {
        time_setting_mode(RTC, CurrentTime, time_setting_mode_flag);
    }
}


/*--- Extra functions ---*/

void mp_safe_io::read_rtc_time(DS3231& RTC, current_time_t& CurrentTime)
{
    static bool h12_flag = false;
    static bool pm_time_flag = false;

    Drv7Seg.output_all();
    CurrentTime.raw_hours   = static_cast<uint32_t>(RTC.getHour(h12_flag, pm_time_flag));
    Drv7Seg.output_all();
    CurrentTime.raw_minutes = static_cast<uint32_t>(RTC.getMinute());
    Drv7Seg.output_all();
    CurrentTime.raw_seconds = static_cast<uint32_t>(RTC.getSecond());
    Drv7Seg.output_all();
}

void mp_safe_io::write_rtc_time(DS3231& RTC, current_time_t& CurrentTime)
{
    RTC.setHour(CurrentTime.raw_hours);
    RTC.setMinute(CurrentTime.raw_minutes);
    RTC.setSecond(CurrentTime.raw_seconds);
}

void mp_safe_io::serial_print(const char *msg)
{
    Serial.print(msg);
}

void mp_safe_io::serial_print(size_t val)
{
    Serial.print(val);
}

void mp_safe_io::serial_print(uint32_t val)
{
    serial_print(static_cast<size_t>(val));
}

void decompose_rtc_time(current_time_t& CurrentTime)
{
    CurrentTime.hours.tens = CurrentTime.raw_hours / 10;
    CurrentTime.hours.ones = CurrentTime.raw_hours % 10;
    
    CurrentTime.minutes.tens = CurrentTime.raw_minutes / 10;
    CurrentTime.minutes.ones = CurrentTime.raw_minutes % 10;
    
    CurrentTime.seconds.tens = CurrentTime.raw_seconds / 10;
    CurrentTime.seconds.ones = CurrentTime.raw_seconds % 10;
}

void time_setting_mode(DS3231& RTC, current_time_t& CurrentTime, bool& time_setting_mode_flag)
{
    CurrentTime = {};  // Assign all-zero values.
    bool update_glyphs_due = true;

    while (time_setting_mode_flag) {
        Drv7Seg.output_all();


        /*--- Buttons ---*/

        if (btn_1.tick()) {
            if (btn_1.press()) {
                mp_safe_io::write_rtc_time(RTC, CurrentTime);
                time_setting_mode_flag = false;
            }
        }

        if (btn_2.tick()) {
            if (btn_2.press() || btn_2.step()) {
                CurrentTime.raw_hours++;
                update_glyphs_due = true;
            }
        }

        if (btn_3.tick()) {
            if (btn_3.press() || btn_3.step()) {
                CurrentTime.raw_minutes++;
                update_glyphs_due = true;
            }
        }    

        if (CurrentTime.raw_minutes >= MAX_COUNT_MINUTES) {
            CurrentTime.raw_minutes = 0;
        }

        if (CurrentTime.raw_hours >= MAX_COUNT_HOURS) {
            CurrentTime.raw_hours = 0;
        }

        if (update_glyphs_due) {
                decompose_rtc_time(CurrentTime);

                uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(CurrentTime.hours.tens);
                uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(CurrentTime.hours.ones);
                uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(CurrentTime.minutes.tens);
                uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(CurrentTime.minutes.ones);

                Drv7Seg.set_glyph_to_pos(seg_byte_pos_1, Drv7SegPos1);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_2, Drv7SegPos2);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_3, Drv7SegPos3);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_4, Drv7SegPos4);

                update_glyphs_due = false;
        }
    }
}
