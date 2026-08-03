// --- io_manager.c ---
#include "io_manager.h"
#include <stdio.h>
#include <math.h>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "malloc.h"
#include "pico/cyw43_arch.h"

float ChipTemp(void)
{
    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);

    uint16_t raw_result = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float voltage = raw_result * conversion_factor;

    float temperature_celsius = 27.0f - (voltage - 0.706f) / 0.001721f;

    // Fixed: 9.0f / 5.0f ensures floating point multiplication (1.8f)
    return (1.8f * temperature_celsius) + 32.0f;
}

extern char __flash_binary_start;
extern char __flash_binary_end;

bool __no_inline_not_in_flash_func(poll_bootsel_button)(void)
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

extern char __StackLimit, __bss_end__;

uint32_t get_total_heap(void)
{
    return (uint32_t)(&__StackLimit - &__bss_end__);
}

float ram(void)
{
    struct mallinfo m = mallinfo();
    return m.uordblks;
}

float flash(void)
{
    return (float)(&__flash_binary_end - &__flash_binary_start);
}

uint get_pin(uint pin)
{
    if (pin > 3)
        return gpio_get(pin) > 0 ? 1 : 0; // Directly queries SIO driven state
    else
        return cyw43_arch_gpio_get(pin) ? 1 : 0; // Queries CYW43 driven state
}

uint toggle_pin(uint pin)
{
    uint new_val = !get_pin(pin);
    if (pin > 3)
        gpio_put(pin, new_val);
    else
        cyw43_arch_gpio_put(pin, new_val);

    return get_pin(pin); // Immediate return without blocking main loop
}

void io_init_all(cJSON *channels)
{
    adc_init();
    adc_set_temp_sensor_enabled(true);

    if (!channels)
        return;

    cJSON *item = NULL;
    printf("Initializing pins:\nPin#\tType\tName\t\tDirection\n");

    cJSON_ArrayForEach(item, channels)
    {
        int type = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "type"));
        uint pin = -type;

        if (type == DIGITAL || type == ANALOG)
        {
            cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
            pin = (uint)cJSON_GetNumberValue(pin_obj);

            if (type == DIGITAL)
            {
                if (pin > 3)
                {
                    gpio_init(pin);
                    bool is_out = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_output"));
                    if (is_out)
                    {
                        gpio_set_dir(pin, GPIO_OUT);
                        gpio_put(pin, false);
                    }
                    else
                    {
                        gpio_set_dir(pin, GPIO_IN);
                        gpio_pull_down(pin);
                    }
                }
                else if (pin >= 0)
                {
                    // Scope restricted strictly to CYW43 Wi-Fi/LED pins (0..3)
                    cyw43_arch_gpio_put(pin, false);
                }
            }
            else if (type == ANALOG)
            {
                if (pin >= 26 && pin <= 29)
                {
                    adc_gpio_init(pin);
                }
            }
        }

        printf("%d\t%d\t%s\t\t%s\n", pin, type,
               cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(item, "ha"), "name")),
               type == DIGITAL ? "Digital" : type == ANALOG ? "Analog"
                                         : type == RAM      ? "RAM"
                                         : type == FLASH    ? "Flash"
                                         : type == TEMP     ? "Temp"
                                         : type == UPTIME   ? "Uptime"
                                                            : "Unknown");
    }
}

// Steps through channels and updates active values
bool channel_updates(cJSON *channels)
{
    if (!channels)
        return false;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, channels)
    {
        channel_type_t type = (channel_type_t)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "type"));

        cJSON *ptr_value = cJSON_GetObjectItem(item, "value");
        if (!ptr_value)
        {
            ptr_value = cJSON_AddNumberToObject(item, "value", 0);
            printf("Added missing 'value' field to channel type %d\n", type);
        }

        switch (type)
        {
        case DIGITAL:
        {
            cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
            uint pin = (uint)cJSON_GetNumberValue(pin_obj);
            cJSON *tg = cJSON_GetObjectItem(item, "toggle");

            bool curr = get_pin(pin);

            if (tg && cJSON_IsTrue(tg))
            {
                bool previous = curr;   // Store previous state for logging
                curr = toggle_pin(pin); // Fixed: assigns directly to outer 'curr' variable
                printf("States pin %d : Previous:%d\t current:%d\n", pin, previous, curr);
                cJSON_SetBoolValue(tg, 0); // Clear toggle flag
            }

            cJSON_SetNumberValue(ptr_value, curr ? 1 : 0); // Ensure numeric representation for MQTT
            break;
        }

        case ANALOG:
        {
            cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
            uint pin = (uint)cJSON_GetNumberValue(pin_obj);

            if (pin >= 26 && pin <= 29)
            {
                adc_select_input(pin - 26);
                float raw_volts = (float)adc_read() * (3.3f / 4096.0f);

                cJSON *ratio_obj = cJSON_GetObjectItem(item, "ratio");
                cJSON *offset_obj = cJSON_GetObjectItem(item, "offset");
                float ratio = ratio_obj ? (float)cJSON_GetNumberValue(ratio_obj) : 1.0f;
                float offset = offset_obj ? (float)cJSON_GetNumberValue(offset_obj) : 0.0f;

                cJSON_SetNumberValue(ptr_value, (raw_volts * ratio) + offset);
            }
            break;
        }

        case RAM:
            cJSON_SetNumberValue(ptr_value, ram());
            break;

        case FLASH:
            cJSON_SetNumberValue(ptr_value, flash());
            break;

        case TEMP:
            cJSON_SetNumberValue(ptr_value, ChipTemp());
            break;

        case UPTIME:
            cJSON_SetNumberValue(ptr_value, time_us_64() / 1000000.0f);
            break;

        default:
            cJSON_SetNumberValue(ptr_value, NAN);
            break;
        }
    }
    return true;
}