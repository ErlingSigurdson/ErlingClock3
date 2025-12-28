/*************** FILE DESCRIPTION ***************/

/**
 * Filename: ErlingClock1.ino
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Purpose:  
 * ----------------------------------------------------------------------------|---------------------------------------|
 * Notes:
 */


/************ PREPROCESSOR DIRECTIVES ***********/

/*--- Includes ---*/

#include <Drv7SegQ595.h>
#include <SegMap595.h>

#include <GyverIO.h>
#include <uButton.h>

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


/*--- Misc ---*/

#define BTN_1_PIN 10
#define BTN_2_PIN 12
#define BTN_3_PIN 11

#define MAX_COUNT_MINUTES 60
#define MAX_COUNT_SECONDS 60

// Comment out or delete to suppress the output of current timer values via UART.
#define SERIAL_OUTPUT

// Set appropriately based on the baud rate you use.
#define BAUD_RATE 115200

// Output interval ("once every X milliseconds").
#define INTERVAL 1000


/****************** DATA TYPES ******************/

struct current_time_t {
    uint8_t raw_hours;
    uint8_t raw_minutes;
    uint8_t raw_seconds;

    struct hours_t {
        uint8_t tens;
        uint8_t ones;
    }; 

    struct minutes_t {
        uint8_t tens;
        uint8_t ones;
    };

    struct seconds_t {
        uint8_t tens;
        uint8_t ones;
    };
    
    hours_t   hours;
    minutes_t minutes;
    seconds_t seconds;
};


/*************** GLOBAL VARIABLES ***************/

DS3231 RTC;

uButton btn_1(BTN_1_PIN);
uButton btn_2(BTN_2_PIN);
uButton btn_3(BTN_3_PIN);


/************** FUNCTION PROTOTYPES *************/

/*--- Multiplexing-friendly I/O wrappers ---*/

namespace mp_safe_io {
    // I2C interface.
    void copy_rtc_time(DS3231& RTC, current_time_t& CurrentTime);
    void set_rtc_time(DS3231& RTC, current_time_t& CurrentTime);

    // I2C helpers.
    void read_time(DS3231& RTC, current_time_t& CurrentTime);

    // UART interface.
    void serial_print(const char* msg, bool add_newline);
}


/*--- Misc ---*/

void decompose_time(current_time_t& CurrentTime);
void time_setting_mode(bool& time_setting_mode_flag, DS3231& RTC, current_time_t& CurrentTime);


/******************* FUNCTIONS ******************/

void setup()
{
    /*--- Interface initialization ---*/

    #ifdef SERIAL_OUTPUT
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
    static current_time_t CurrentTime = {0, 0, 0, 0, 0, 0, 0, 0, 0};


    /*--- Counter and value update trigger ---*/

    // Counter.
    uint32_t current_millis = millis();
    static uint32_t previous_millis = current_millis;

    // Value update trigger.
    static bool update_due = true;


    /*--- Demo output ---*/

    if (update_due) {
        get_time(RTC, CurrentTime);

        uint8_t seg_byte_minutes_tens = SegMap595.get_mapped_byte(static_cast<uint32_t>(CurrentTime.minutes.tens));
        uint8_t seg_byte_minutes_ones = SegMap595.get_mapped_byte(static_cast<uint32_t>(CurrentTime.minutes.ones));
        uint8_t seg_byte_seconds_tens = SegMap595.get_mapped_byte(static_cast<uint32_t>(CurrentTime.seconds.tens));
        uint8_t seg_byte_seconds_ones = SegMap595.get_mapped_byte(static_cast<uint32_t>(CurrentTime.seconds.ones));

        // Dot-segment blink.
        if (CurrentTime.raw_seconds % 2) {
            /* static keyword is only suitable if you're not planning subsequent
             * init() calls that can change the actual dot bit position.
             */
            static int32_t dot_bit_pos = SegMap595.get_dot_bit_pos();
            if (dot_bit_pos >= 0) {
                uint8_t mask = static_cast<uint8_t>(1u << dot_bit_pos);
                seg_byte_minutes_ones ^= mask;
            }
        }

        Drv7Seg.set_glyph_to_pos(seg_byte_minutes_tens, Drv7SegPos1);
        Drv7Seg.set_glyph_to_pos(seg_byte_minutes_ones, Drv7SegPos2);
        Drv7Seg.set_glyph_to_pos(seg_byte_seconds_tens, Drv7SegPos3);
        Drv7Seg.set_glyph_to_pos(seg_byte_seconds_ones, Drv7SegPos4);

        #ifdef SERIAL_OUTPUT
            Serial.print("Timer values (minutes and seconds): ");
            Drv7Seg.output_all();
            Serial.print(CurrentTime.raw_minutes / 10);
            Drv7Seg.output_all();
            Serial.print(CurrentTime.raw_minutes % 10);
            Drv7Seg.output_all();
            Serial.print(":");
            Drv7Seg.output_all();
            Serial.print(CurrentTime.raw_seconds / 10);
            Drv7Seg.output_all();
            Serial.println(CurrentTime.raw_seconds % 10);
        #endif

        update_due = false;
    }

    // Basic call spot (it's not the only one).
    Drv7Seg.output_all();


    /*--- Counter and value update trigger, continued ---*/

    if (current_millis - previous_millis >= INTERVAL) {
        update_due = true;
        previous_millis = current_millis;
    }


    /*--- Time setting mode control ---*/

    static bool time_setting_mode_flag = false;

    if (btn_1.tick()) {
        if (btn_1.press()) {
            time_setting_mode_flag = true;
        }
    }

    if (time_setting_mode_flag) {
        time_setting_mode(time_setting_mode_flag, RTC, CurrentTime);
    }
}

void get_time(DS3231& RTC, current_time_t& CurrentTime)
{
    read_time(RTC, CurrentTime);
    decompose_time(CurrentTime);
}

void read_time(DS3231& RTC, current_time_t& CurrentTime)
{
    //DateTime RTCCurrentTime = RTClib::now();
    //CurrentTime.raw_hours   = RTCCurrentTime.hour();
    //CurrentTime.raw_minutes = RTCCurrentTime.minute();
    //CurrentTime.raw_seconds = RTCCurrentTime.second();

    static bool h12_flag = false;
    static bool pm_time_flag = false;

    CurrentTime.raw_hours   = RTC.getHour(h12_flag, pm_time_flag);
    Drv7Seg.output_all();
    CurrentTime.raw_minutes = RTC.getMinute();
    Drv7Seg.output_all();
    CurrentTime.raw_seconds = RTC.getSecond();
}

void decompose_time(current_time_t& CurrentTime)
{
    CurrentTime.hours.tens = CurrentTime.raw_hours / 10;
    CurrentTime.hours.ones = CurrentTime.raw_hours % 10;
    
    CurrentTime.minutes.tens = CurrentTime.raw_minutes / 10;
    CurrentTime.minutes.ones = CurrentTime.raw_minutes % 10;
    
    CurrentTime.seconds.tens = CurrentTime.raw_seconds / 10;
    CurrentTime.seconds.ones = CurrentTime.raw_seconds % 10;
}

void set_time(DS3231& RTC, current_time_t& CurrentTime)
{
    RTC.setHour(CurrentTime.raw_hours);
    RTC.setMinute(CurrentTime.raw_minutes);
    RTC.setSecond(CurrentTime.raw_seconds);
}

void time_setting_mode(bool& time_setting_mode_flag, DS3231& RTC, current_time_t& CurrentTime)
{
    // TODO
    while (time_setting_mode_flag) {
        /*
        if (btn_1.tick()) {
            if (btn_1.press()) {
                time_setting_mode_flag = false;
            }
        }
        */
    }
}
