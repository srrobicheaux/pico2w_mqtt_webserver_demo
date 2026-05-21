#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/adc.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"

#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "system_info.h"
#include <string.h>


#include "lwip/apps/sntp.h"
#include "pico/time.h"
#include "pico/stdlib.h"
#include <time.h>



// how often to measure our temperature
#define TEMP_WORKER_TIME_S 1
// Temperature
#ifndef TEMPERATURE_UNITS
#define TEMPERATURE_UNITS 'F' // Set to 'F' for Fahrenheit
#endif

/* References for this implementation:
 * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
 * pico-examples/adc/adc_console/adc_console.c */
static float read_onboard_temperature(const char unit)
{

    /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
    const float conversionFactor = 3.3f / (1 << 12);
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    float adc = (float)adc_read() * conversionFactor;

    float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

    if (unit == 'C' || unit != 'F')
    {
        return tempC;
    }
    else if (unit == 'F')
    {
        return tempC * 9 / 5 + 32;
    }

    return -1.0f;
}

void load_status_JSON(char *payload)
{
    float temperature = read_onboard_temperature(TEMPERATURE_UNITS);
    snprintf(payload, 256,
             "{\"uptime\":%lu,\"temperature\":%.2f}", (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000), temperature);
}

bool get_bootsel_button()
{
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i)
        ;

#if PICO_RP2040
#define CS_BIT (1u << 1)
#else
#define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return button_state;
}

// Callback for the non-blocking "Press" (Active Low: 1 is OFF)
static int64_t pin_off_callback(alarm_id_t id, void *user_data)
{
    int pio = (int)(uintptr_t)user_data;
    gpio_put(pio, 0);
    return 0;
}

bool pin_action(int pio, gpio_action_t action)
{
    if (pio < 0 || pio > 29)
        return false;

    // Check if the pin is already configured as an output
    bool is_output = gpio_get_dir(pio);

    // 1. Handle Initialization (Only if NOT a READ and NOT already an output)
    if (action != GPIO_ACTION_READ && !is_output)
    {
        gpio_init(pio);
        // CRITICAL: Set HIGH first so it doesn't click ON when dir changes
        gpio_put(pio, 0);
        gpio_set_dir(pio, GPIO_OUT);
        is_output = gpio_get_dir(pio);
    }

    // 2. Perform the Action
    switch (action)
    {
    case GPIO_ACTION_TOGGLE:
        gpio_put(pio, !gpio_get(pio));
        break;

    case GPIO_ACTION_PRESS:
        gpio_put(pio, 1);
        add_alarm_in_ms(1000, pin_off_callback, (void *)(uintptr_t)pio, true);
        break;

    case GPIO_ACTION_READ:
    default:
        // Do nothing to the hardware, just read the state below
        break;
    }

    // 3. Return the state (Inverted for Active Low: Low/0 = True/ON)
    // If it's not an output yet, we'll just be reading the floating/pulled-high input
    return gpio_get(pio);
}

void reset()
{
    watchdog_enable(10, true);
    while (1)
        ;
}

// You will need to define this helper in time.c:
void sntp_set_system_time_us(uint32_t sec, uint32_t us) {
    struct timeval tv = { .tv_sec = sec, .tv_usec = us };
    settimeofday(&tv, NULL);
}

// Set your timezone offset (e.g., -5 for EST, -6 for CST)
#define UTC_OFFSET_HOURS -6

void time_manager_init() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Use a generic pool so it works anywhere
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    
    sntp_init();
    printf("NTP Time Sync Initialized...\n");
}

// Get the current formatted time
void get_current_time_str(char *buffer, size_t max_len) {
    time_t now = time(NULL);
    struct tm timeinfo;
    
    // Apply timezone offset
    now += (UTC_OFFSET_HOURS * 3600);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2024 - 1900)) {
        snprintf(buffer, max_len, "Syncing...");
    } else {
        strftime(buffer, max_len, "%H:%M:%S", &timeinfo);
    }
}

// Get full ISO timestamp for MQTT (e.g., 2024-05-24T12:00:00)
void get_iso_timestamp(char *buffer, size_t max_len) {
    time_t now = time(NULL);
    now += (UTC_OFFSET_HOURS * 3600);
    struct tm ti;
    localtime_r(&now, &ti);
    
    strftime(buffer, max_len, "%Y-%m-%dT%H:%M:%S", &ti);
}