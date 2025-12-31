/*************** FILE DESCRIPTION ***************/

/**
 * Filename: ErlingClock1.ino
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Purpose:  A main file of a firmware (an Arduino sketch) written for
 *           the Arduino Pro Mini-based electronic clock I made in 2022.
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Notes:
 */


/************ PREPROCESSOR DIRECTIVES ***********/

/*--- Includes ---*/

// Display driver.
#include <Drv7SegQ595.h>

// Byte mapping.
#include <SegMap595.h>

// Buttons.
#include <uButton.h>

// DS3231 RTC interfacing.
#include <GyverDS3231Min.h>


/*--- Drv7SegQ595 library API parameters ---*/

#define POS_SWITCH_TYPE Drv7SegActiveHigh

#define DATA_PIN  4
#define LATCH_PIN 2
#define CLOCK_PIN 3

#define POS_1_PIN 9
#define POS_2_PIN 8
#define POS_3_PIN 7
#define POS_4_PIN 6

/* Not strictly necessary, but it's a guard against ripple caused by
 * gaps in multiplexing that occur due to long-ish I/O.
 */
#define ANTI_GHOSTING_RETENTION_DURATION 1500


/*--- SegMap595 library API parameters ---*/

#define MAP_STR "DA@FCBGE"
#define DISPLAY_COMMON_PIN SegMap595CommonCathode


/*--- Buttons ---*/

#define BTN_1_PIN 10
#define BTN_2_PIN 12
#define BTN_3_PIN 11


/*--- Counters and timing ---*/

#define BASIC_INTERVAL               1000  // In milliseconds.
#define I2C_READ_INTERVAL_MULTIPLIER 10

#define MAX_COUNT_HOURS   24
#define MAX_COUNT_MINUTES 60
#define MAX_COUNT_SECONDS 60


/*--- UART ---*/

#define SERIAL_OUTPUT_ENABLED  // Comment out to suppress the UART output.
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


/************** FUNCTION PROTOTYPES *************/

/*--- Multiplexing-friendly I/O wrappers ---*/

namespace mp_safe_io {
    // I2C.
    void read_rtc_time(GyverDS3231Min& RTC, current_time_t& CurrentTime);
    void write_rtc_time(GyverDS3231Min& RTC, current_time_t& CurrentTime);

    // UART.
    void serial_print(const char* msg);
    void serial_print(size_t val);

    #if defined(UINT32_MAX) && defined(SIZE_MAX) && (UINT32_MAX > SIZE_MAX)
    void serial_print(uint32_t val);
    #endif
}


/*--- Misc ---*/

void decompose_rtc_time(current_time_t& CurrentTime);
void time_setting_mode(GyverDS3231Min& RTC, current_time_t& CurrentTime,
                       uButton& btn_1, uButton& btn_2, uButton& btn_3,
                       bool& time_setting_mode_flag
                      );


/******************* FUNCTIONS ******************/

/*--- Basic functions ---*/

void setup()
{
    /*--- Interface initialization ---*/

    #ifdef SERIAL_OUTPUT_ENABLED
    Serial.begin(BAUD_RATE);
    #endif


    /*--- Byte mapping ---*/

    int32_t mapping_status = SegMap595.init(MAP_STR, DISPLAY_COMMON_PIN);

    if (mapping_status < 0) {
        while(true) {
            Serial.print("Error: mapping failed, error code ");
            Serial.println(mapping_status);
            delay(BASIC_INTERVAL);
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
            delay(BASIC_INTERVAL);
        }
    }

    #ifdef ANTI_GHOSTING_RETENTION_DURATION
    Drv7Seg.set_anti_ghosting_retention_duration(ANTI_GHOSTING_RETENTION_DURATION);
    #endif
}

void loop()
{
    /*--- Interface initialization, continued ---*/

    static GyverDS3231Min RTC;
    static current_time_t CurrentTime = {};  // Initialize with all-zero values.

    static bool interface_begin_flag = false;
    if (!interface_begin_flag) {
        Wire.begin();
        RTC.begin();
        interface_begin_flag = true;
    }


    /*--- Button initialization ---*/

    static uButton btn_1(BTN_1_PIN);
    static uButton btn_2(BTN_2_PIN);
    static uButton btn_3(BTN_3_PIN);


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

    // Update triggers.
    static bool update_output_due = true;
    static bool update_i2c_due = true;


    /*--- Time update and output ---*/

    if (update_i2c_due) {
        mp_safe_io::read_rtc_time(RTC, CurrentTime);
        mp_safe_io::serial_print("ErlingClock1 sketch version: 2.0.5\r\n");
        update_i2c_due = false;
    }

    if (update_output_due) {
        decompose_rtc_time(CurrentTime);

        uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(CurrentTime.hours.tens);
        uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(CurrentTime.hours.ones);
        uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(CurrentTime.minutes.tens);
        uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(CurrentTime.minutes.ones);

        // Handy for debugging.
        /*
        uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(CurrentTime.minutes.tens);
        uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(CurrentTime.minutes.ones);
        uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(CurrentTime.seconds.tens);
        uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(CurrentTime.seconds.ones);
        */

        // Dot-segment blink.
        if (CurrentTime.raw_seconds % 2) {
            seg_byte_pos_2 = SegMap595.toggle_dot_bit(seg_byte_pos_2);
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

    /* Basic output call spot (it's not the only one, output is also
     * commenced before and after every UART and I2C I/O operation).
     */
    Drv7Seg.output_all();


    /*--- Counter and value update trigger, continued ---*/

    if (current_millis - previous_millis >= BASIC_INTERVAL) {
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
        time_setting_mode(RTC, CurrentTime,
                          btn_1, btn_2, btn_3,
                          time_setting_mode_flag);
    }
}


/*--- Extra functions ---*/

void mp_safe_io::read_rtc_time(GyverDS3231Min& RTC, current_time_t& CurrentTime)
{
    Drv7Seg.output_all();
    Datime dt = RTC.getTime();
    Drv7Seg.output_all();

    CurrentTime.raw_hours   = static_cast<uint32_t>(dt.hour);
    CurrentTime.raw_minutes = static_cast<uint32_t>(dt.minute);
    CurrentTime.raw_seconds = static_cast<uint32_t>(dt.second);
}

void mp_safe_io::write_rtc_time(GyverDS3231Min& RTC, current_time_t& CurrentTime)
{
    Datime dt(2001, 1, 1,  /* Arbitrary placeholders.
                            * Date must NOT be 01.01.2000, because for some reason setTime() is programmed to
                            * return false early in case the date is Y2K month 1 day 1 (AlexGyver's design decision).
                            */
              static_cast<uint8_t>(CurrentTime.raw_hours),
              static_cast<uint8_t>(CurrentTime.raw_minutes),
              static_cast<uint8_t>(CurrentTime.raw_seconds)
             );

    Drv7Seg.output_all();
    RTC.setTime(dt);
    Drv7Seg.output_all();

    // Another variant, pretty much equivalent.
    /*
    Drv7Seg.output_all();
    RTC.setTime(2001, 1, 1,
                static_cast<uint8_t>(CurrentTime.raw_hours),
                static_cast<uint8_t>(CurrentTime.raw_minutes),
                static_cast<uint8_t>(CurrentTime.raw_seconds)
               );
    Drv7Seg.output_all();
    */
}

void mp_safe_io::serial_print(const char *msg)
{
    Drv7Seg.output_all();
    Serial.print(msg);
    Drv7Seg.output_all();
}

void mp_safe_io::serial_print(size_t val)
{
    Drv7Seg.output_all();
    Serial.print(val);
    Drv7Seg.output_all();
}

#if defined(UINT32_MAX) && defined(SIZE_MAX) && (UINT32_MAX > SIZE_MAX)
void mp_safe_io::serial_print(uint32_t val)
{
    serial_print(static_cast<size_t>(val));
}
#endif

void decompose_rtc_time(current_time_t& CurrentTime)
{
    CurrentTime.hours.tens = CurrentTime.raw_hours / 10;
    CurrentTime.hours.ones = CurrentTime.raw_hours % 10;
    
    CurrentTime.minutes.tens = CurrentTime.raw_minutes / 10;
    CurrentTime.minutes.ones = CurrentTime.raw_minutes % 10;
    
    CurrentTime.seconds.tens = CurrentTime.raw_seconds / 10;
    CurrentTime.seconds.ones = CurrentTime.raw_seconds % 10;
}

void time_setting_mode(GyverDS3231Min& RTC, current_time_t& CurrentTime,
                       uButton& btn_1, uButton& btn_2, uButton& btn_3,
                       bool& time_setting_mode_flag
                      )
{
    CurrentTime = {};  // Assign all-zero values.
    bool update_output_due = true;

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
                // Handy for debugging.
                //CurrentTime.raw_minutes++;
                update_output_due = true;
            }
        }

        if (btn_3.tick()) {
            if (btn_3.press() || btn_3.step()) {
                CurrentTime.raw_minutes++;
                // Handy for debugging.
                //CurrentTime.raw_seconds++;
                update_output_due = true;
            }
        }    

        // Handy for debugging.
        /*
        if (CurrentTime.raw_seconds >= MAX_COUNT_SECONDS) {
            CurrentTime.raw_seconds = 0;
        }
        */

        if (CurrentTime.raw_minutes >= MAX_COUNT_MINUTES) {
            CurrentTime.raw_minutes = 0;
        }

        if (CurrentTime.raw_hours >= MAX_COUNT_HOURS) {
            CurrentTime.raw_hours = 0;
        }

        if (update_output_due) {
                decompose_rtc_time(CurrentTime);

                uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(CurrentTime.hours.tens);
                uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(CurrentTime.hours.ones);
                uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(CurrentTime.minutes.tens);
                uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(CurrentTime.minutes.ones);

                // Handy for debugging.
                /*
                uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(CurrentTime.minutes.tens);
                uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(CurrentTime.minutes.ones);
                uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(CurrentTime.seconds.tens);
                uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(CurrentTime.seconds.ones);
                */

                Drv7Seg.set_glyph_to_pos(seg_byte_pos_1, Drv7SegPos1);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_2, Drv7SegPos2);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_3, Drv7SegPos3);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_4, Drv7SegPos4);

                update_output_due = false;
        }
    }
}
