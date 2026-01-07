/*************** FILE DESCRIPTION ***************/

/**
 * Filename: ErlingClock1.ino
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Purpose:  The main file of the firmware (the Arduino sketch) written for
 *           the Arduino Pro Mini-based electronic clock I made in 2022.
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Notes:    The project's Git repositories:
 *           * https://github.com/ErlingSigurdson/ErlingClock1
 *           * https://gitflic.ru/project/efimov-d-v/erlingclock1
 *           * https://codeberg.org/ErlingSigurdson/ErlingClock1
 */


/************ PREPROCESSOR DIRECTIVES ***********/

/*--- Includes ---*/

// 7-segment display driver.
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


/*--- Timing and counters ---*/

#define BASIC_INTERVAL               1000UL  // In milliseconds.
#define I2C_READ_INTERVAL_MULTIPLIER 10


/*--- UART ---*/

#define SERIAL_OUTPUT_ENABLED  // Comment out to suppress the UART output.
#define BAUD_RATE 115200


/*--- Misc ---*/

// A pair of macros for turning an argument into a string.
#define STRINGIFY(x) INTERMEDIATE_STRINGIFY(x)
#define INTERMEDIATE_STRINGIFY(x) #x

#define VERSION 2.0.8


/****************** DATA TYPES ******************/

struct CurrentTime {
    public:
        struct raw_t {
            uint32_t hours;
            uint32_t minutes;
            uint32_t seconds;
        };

        // size_t is used in this data type because respective values are used as indices.
        struct hours_t {
            size_t tens;
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

        raw_t     raw;
        hours_t   hours;
        minutes_t minutes;
        seconds_t seconds;

        void apply_max_count();
        void decompose_by_digits();

    private:
        static constexpr uint32_t _max_count_hours = 24;
        static constexpr uint32_t _max_count_minutes = 60;
        static constexpr uint32_t _max_count_seconds = 60;
};


/************** FUNCTION PROTOTYPES *************/

/*--- Multiplexing-friendly I/O wrappers ---*/

namespace mp_safe_io {
    // I2C.
    void read_rtc_time(GyverDS3231Min& RTC, CurrentTime& current_time);
    void write_rtc_time(GyverDS3231Min& RTC, CurrentTime& current_time);

    // UART.
    void serial_print(const char* msg);
    void serial_print(size_t val);

    #if defined(UINT32_MAX) && defined(SIZE_MAX) && (UINT32_MAX > SIZE_MAX)
    void serial_print(uint32_t val);
    #endif
}


/*--- Modes ---*/

namespace modes {
    namespace time_setting {
        void loop(GyverDS3231Min& RTC, CurrentTime& current_time,
                  uButton& btn_1, uButton& btn_2, uButton& btn_3,
                  bool& dark_mode_flag,
                  bool& time_setting_mode_flag
                 );
    }

    // More modes may be added later.
}


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


    /*--- Dark mode control ---*/

    static bool dark_mode_flag = false;
    constexpr uint8_t blank = 0;
    static uint8_t only_dot_on = SegMap595.turn_on_dot(blank);  // Indicate that clock is on.


    /*--- Counters and update triggers ---*/

    // Counters.
    uint32_t current_millis = millis();
    static uint32_t previous_millis = current_millis;

    static CurrentTime current_time = {};  // Initialize with all-zero values.

    static uint32_t updates = 0;

    // Update triggers.
    static bool update_output_due = true;
    static bool update_i2c_due = true;


    /*--- Output values update and optional UART output ---*/

    if (update_i2c_due) {
        mp_safe_io::read_rtc_time(RTC, current_time);

        #ifdef SERIAL_OUTPUT_ENABLED
        mp_safe_io::serial_print("ErlingClock1 sketch version: " STRINGIFY(VERSION) "\r\n");
        #endif
        
        update_i2c_due = false;
    }

    if (update_output_due) {
        current_time.apply_max_count();
        current_time.decompose_by_digits();

        uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(current_time.hours.tens);
        uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(current_time.hours.ones);
        uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(current_time.minutes.tens);
        uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(current_time.minutes.ones);

        // Handy for debugging.
        /*
        uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(current_time.minutes.tens);
        uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(current_time.minutes.ones);
        uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(current_time.seconds.tens);
        uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(current_time.seconds.ones);
        */

        // Dot-segment blink.
        if (current_time.raw.seconds % 2) {
            seg_byte_pos_2 = SegMap595.toggle_dot(seg_byte_pos_2);
        }

        if (!dark_mode_flag) {
            Drv7Seg.set_glyph_to_pos(seg_byte_pos_1, Drv7SegPos1);
            Drv7Seg.set_glyph_to_pos(seg_byte_pos_2, Drv7SegPos2);
            Drv7Seg.set_glyph_to_pos(seg_byte_pos_3, Drv7SegPos3);
            Drv7Seg.set_glyph_to_pos(seg_byte_pos_4, Drv7SegPos4);
        } else {
            Drv7Seg.set_glyph_to_pos(blank, Drv7SegPos1);
            Drv7Seg.set_glyph_to_pos(blank, Drv7SegPos2);
            Drv7Seg.set_glyph_to_pos(blank, Drv7SegPos3);
            Drv7Seg.set_glyph_to_pos(only_dot_on, Drv7SegPos4);
        }

        #ifdef SERIAL_OUTPUT_ENABLED
            mp_safe_io::serial_print("Timer values (hours, minutes and seconds): ");
            mp_safe_io::serial_print(current_time.hours.tens);
            mp_safe_io::serial_print(current_time.hours.ones);
            mp_safe_io::serial_print(":");
            mp_safe_io::serial_print(current_time.minutes.tens);
            mp_safe_io::serial_print(current_time.minutes.ones);
            mp_safe_io::serial_print(":");
            mp_safe_io::serial_print(current_time.seconds.tens);
            mp_safe_io::serial_print(current_time.seconds.ones);
            mp_safe_io::serial_print("\r\n");
        #endif

        update_output_due = false;
    }

    /* Basic output call spot (it's not the only one, output is also
     * commenced before and after every UART and I2C I/O operation).
     */
    Drv7Seg.output_all();


    /*--- Counter and value update trigger, continued ---*/

    uint32_t elapsed = current_millis - previous_millis;
    if (elapsed >= BASIC_INTERVAL) {
        uint32_t increments = elapsed / BASIC_INTERVAL;
        current_time.raw.seconds += increments;
        update_output_due = true;
        updates += increments;
        previous_millis += increments * BASIC_INTERVAL;
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
        modes::time_setting::loop(RTC, current_time,
                                  btn_1, btn_2, btn_3,
                                  dark_mode_flag,
                                  time_setting_mode_flag
                                 );
    }


    /*--- Dark mode control, continued ---*/

    if (btn_3.tick()) {
        if (btn_3.press()) {
            dark_mode_flag = !dark_mode_flag;
            update_output_due = true;
        }
    }
}


/*--- Extra functions ---*/

void CurrentTime::apply_max_count()
{
    if (raw.seconds >= _max_count_seconds) {
        raw.minutes += raw.seconds / _max_count_seconds;
        raw.seconds %= _max_count_seconds;
    }

    if (raw.minutes >= _max_count_minutes) {
        raw.hours += raw.minutes / _max_count_minutes;
        raw.minutes %= _max_count_minutes;
    }

    if (raw.hours >= _max_count_hours) {
        raw.hours %= _max_count_hours;
    }
}

void CurrentTime::decompose_by_digits()
{
    hours.tens = raw.hours / 10;
    hours.ones = raw.hours % 10;

    minutes.tens = raw.minutes / 10;
    minutes.ones = raw.minutes % 10;

    seconds.tens = raw.seconds / 10;
    seconds.ones = raw.seconds % 10;
}

void mp_safe_io::read_rtc_time(GyverDS3231Min& RTC, CurrentTime& current_time)
{
    Drv7Seg.output_all();
    Datime dt = RTC.getTime();
    Drv7Seg.output_all();

    current_time.raw.hours   = static_cast<uint32_t>(dt.hour);
    current_time.raw.minutes = static_cast<uint32_t>(dt.minute);
    current_time.raw.seconds = static_cast<uint32_t>(dt.second);
}

void mp_safe_io::write_rtc_time(GyverDS3231Min& RTC, CurrentTime& current_time)
{
    Datime dt(2001, 1, 1,  /* Arbitrary placeholders.
                            * Date must NOT be 01.01.2000, because for some reason setTime() is programmed to
                            * return false early in case the date is Y2K month 1 day 1 (AlexGyver's design decision).
                            */
              static_cast<uint8_t>(current_time.raw.hours),
              static_cast<uint8_t>(current_time.raw.minutes),
              static_cast<uint8_t>(current_time.raw.seconds)
             );

    Drv7Seg.output_all();
    RTC.setTime(dt);
    Drv7Seg.output_all();

    // Another variant, pretty much equivalent.
    /*
    Drv7Seg.output_all();
    RTC.setTime(2001, 1, 1,
                static_cast<uint8_t>(current_time.raw.hours),
                static_cast<uint8_t>(current_time.raw.minutes),
                static_cast<uint8_t>(current_time.raw.seconds)
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

// This overload can theoretically truncate the argument value, but given the realistic time values, it's a non-issue.
#if defined(UINT32_MAX) && defined(SIZE_MAX) && (UINT32_MAX > SIZE_MAX)
void mp_safe_io::serial_print(uint32_t val)
{
    serial_print(static_cast<size_t>(val));
}
#endif

void modes::time_setting::loop(GyverDS3231Min& RTC, CurrentTime& current_time,
                               uButton& btn_1, uButton& btn_2, uButton& btn_3,
                               bool& dark_mode_flag,
                               bool& time_setting_mode_flag
                              )
{
    current_time = {};  // Assign all-zero values.
    bool update_output_due = true;

    while (true) {
        Drv7Seg.output_all();


        /*--- Buttons ---*/

        if (btn_1.tick()) {
            if (btn_1.press()) {
                mp_safe_io::write_rtc_time(RTC, current_time);
                dark_mode_flag = false;
                time_setting_mode_flag = false;
                break;
            }
        }

        if (btn_2.tick()) {
            if (btn_2.press() || btn_2.step()) {
                current_time.raw.hours++;
                // Handy for debugging.
                //current_time.raw.minutes++;
                update_output_due = true;
            }
        }

        if (btn_3.tick()) {
            if (btn_3.press() || btn_3.step()) {
                current_time.raw.minutes++;
                // Handy for debugging.
                //current_time.raw.seconds++;
                update_output_due = true;
            }
        }

        if (update_output_due) {
                current_time.apply_max_count();
                current_time.decompose_by_digits();

                uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(current_time.hours.tens);
                uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(current_time.hours.ones);
                uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(current_time.minutes.tens);
                uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(current_time.minutes.ones);

                // Handy for debugging.
                /*
                uint8_t seg_byte_pos_1 = SegMap595.get_mapped_byte(current_time.minutes.tens);
                uint8_t seg_byte_pos_2 = SegMap595.get_mapped_byte(current_time.minutes.ones);
                uint8_t seg_byte_pos_3 = SegMap595.get_mapped_byte(current_time.seconds.tens);
                uint8_t seg_byte_pos_4 = SegMap595.get_mapped_byte(current_time.seconds.ones);
                */

                Drv7Seg.set_glyph_to_pos(seg_byte_pos_1, Drv7SegPos1);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_2, Drv7SegPos2);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_3, Drv7SegPos3);
                Drv7Seg.set_glyph_to_pos(seg_byte_pos_4, Drv7SegPos4);

                update_output_due = false;
        }
    }
}
