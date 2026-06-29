// --- io_manager.c ---
#include "io_manager.h"
#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "cJSON.h"
#include <math.h>
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "malloc.h"
#include "pico/cyw43_arch.h"

bool get_pin(int pin)
{
    if (pin < 4 ){
        return cyw43_arch_gpio_get(pin);
    }
    else {
        return gpio_get(pin);
    }
}

bool toggle_pin(int pin)
{
    int is_out = gpio_get_dir(pin);
    int value = !gpio_get(pin);

    if (is_out)
    {
        if (pin < 4)
        {
            cyw43_arch_gpio_put(pin, value);
        }
        else
        {
            gpio_put(pin, value);
        }
        return true;
    }
    else
    {
        return false;
    }
}

void io_init_all(cJSON *channels)
{
    adc_init();
    adc_set_temp_sensor_enabled(true);

    if (!channels)
        return;

    cJSON *item = NULL;
    printf("Initializing pins:\nPin#\tType\tDirection\n");

    cJSON_ArrayForEach(item, channels)
    {
        // Safely extract integers and strict booleans
        cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
        if (!pin_obj)
            continue;

        int pin = pin_obj->valueint;
        bool is_digital = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_digital"));
        bool is_out = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_output"));

        printf("%d\t%s\t%s\n", pin, is_digital ? "digital" : "analog", is_out ? "output" : "input");

        // Use the is_digital flag instead of assuming pin >= 26 is always analog
        if (!is_digital && pin >= 26 && pin <= 29)
        {
            adc_gpio_init(pin);
        }
        else
        {
            gpio_init(pin);
            gpio_set_dir(pin, is_out);
            if (!is_out)
                gpio_pull_up(pin);
        }
    }
}

cJSON *channel_updates(cJSON *channels)
{
    // 1. Create an OBJECT instead of an Array
    cJSON *updates = cJSON_CreateObject();
    if (!channels)
        return updates;

    cJSON *item = NULL;
    bool is_empty = true;
    cJSON_ArrayForEach(item, channels)
    {
        cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
        if (!pin_obj)
            continue;

        int pin = pin_obj->valueint;
        bool is_digital = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_digital"));

        cJSON *ptr_value = cJSON_GetObjectItem(item, "value");
        double old_value = ptr_value ? cJSON_GetNumberValue(ptr_value) : -999.0;

        float new_value = 0.0f;

        if (is_digital)
        {
            new_value = gpio_get(pin) ? 1.0f : 0.0f;
        }
        else if (pin >= 26 && pin <= 29)
        {
            cJSON *ratio_obj = cJSON_GetObjectItem(item, "ratio");
            cJSON *offset_obj = cJSON_GetObjectItem(item, "offset");

            float ratio = ratio_obj ? (float)cJSON_GetNumberValue(ratio_obj) : 1.0f;
            float offset = offset_obj ? (float)cJSON_GetNumberValue(offset_obj) : 0.0f;

            adc_select_input(pin - 26);
            float raw_adc = (float)adc_read() * (3.3f / 4096.0f);

            new_value = (raw_adc * ratio) + offset;
        }

        if (fabs(old_value - new_value) > 0.005)
        {
            is_empty = false;

            // 2. Convert the integer pin to a string for the JSON key
            char pin_key[16];
            snprintf(pin_key, sizeof(pin_key), "%d", pin);

            // 3. Add the value directly to the root updates object
            // This creates the clean format: { "28": 0.517 }
            cJSON_AddNumberToObject(updates, pin_key, new_value);

            // Commit the new value back to the tree
            if (ptr_value)
            {
                cJSON_SetNumberValue(ptr_value, new_value);
            }
            else
            {
                cJSON_AddNumberToObject(item, "value", new_value);
            }
        }
    }
    if (is_empty)
    {
        return NULL;
    }
    else
    {
        return updates;
    }
}

bool __no_inline_not_in_flash_func(poll_bootsel_button)()
{
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i)
        ;

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
#if PICO_RP2040
#define CS_BIT (1u << 1)
#else
#define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

float ChipTemp()
{
    // 1. Initialize the ADC hardware (if not done already in main)
    // adc_init();

    // 2. CRITICAL: Enable the internal temperature sensor
    adc_set_temp_sensor_enabled(true);

    // 3. Select ADC input 4 (Internal Temp Sensor for Pico 1 & Pico 2)
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);

    // 4. Read raw value
    uint16_t raw_result = adc_read();

    // 5. Convert to voltage
    const float conversion_factor = 3.3f / (1 << 12);
    float voltage = raw_result * conversion_factor;

    // 6. Calculate Temperature (Formula is valid for RP2040 & RP2350)
    float temperature_celsius = 27.0f - (voltage - 0.706f) / 0.001721f;

    return 9 / 5 * temperature_celsius + 32;
}

// These are defined by the linker script
extern char __flash_binary_start;
extern char __flash_binary_end;

cJSON *system_channel_update()
{
    uint32_t ram_used, ram_total, flash_used, flash_total;

    // --- RAM (HEAP) USAGE ---
    // mallinfo() is a standard C library function to report heap status
    struct mallinfo m = mallinfo();

    // uordblks = Total allocated space (used)
    // fordblks = Total free space
    ram_used = m.uordblks;
    ram_total = m.uordblks + m.fordblks; // Total Heap available to your app

    // --- FLASH USAGE ---
    // Calculate the size of the binary by subtracting the end address from the start
    flash_used = (uint32_t)(&__flash_binary_end - &__flash_binary_start);

    // The Pico 2 W typically has 4MB of Flash (check your specific board specs)
    // You can also use PICO_FLASH_SIZE_BYTES if defined in board header
    flash_total = PICO_FLASH_SIZE_BYTES; // 4 * 1024 * 1024;

    cJSON *updates = cJSON_CreateObject();
    if (!updates)
        return updates;

    float ram_pct = (float)ram_used / ram_total * 100.0f;
    float flash_pct = (float)flash_used / flash_total * 100.0f;

    cJSON_AddNumberToObject(updates, "ram_used", ram_used);
    cJSON_AddNumberToObject(updates, "ram_total", ram_total);
    cJSON_AddNumberToObject(updates, "ram_pct", ram_pct);
    cJSON_AddNumberToObject(updates, "flash_pct", flash_pct);
    cJSON_AddNumberToObject(updates, "temp_f", ChipTemp());
    return updates;
}