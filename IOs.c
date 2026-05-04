#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "flash.h"
#include <stdio.h>
#include <string.h>

#define ADC_VREF 3.3f

static bool fast_mode = false;
io_t *IOs;

// Removed the parameter here, using the global 'notify' instead
// void send_analog_event(void)
void IOs_JSON(char *payload, size_t len)
{
    static const float cf = ADC_VREF / (1 << 12);
    char *pos = payload;
    char *max_pos = payload + len;
    float v;

    pos = pos + snprintf(pos, max_pos - pos, "{\"analog\":[");

    for (size_t i = 0; i < IOs->analog_count; i++)
    {
        adc_select_input(i);
        v = adc_read() * cf * IOs->analogs[i].ratio + IOs->analogs[i].offset;
        pos = pos + snprintf(pos, len, "%.2f,", v);
    }
    pos[-1] = ']';

    uint32_t pins = gpio_get_all();
    pos = pos + snprintf(pos, len, ",\"pio\":[");
    for (int i = 0; i < IOs->pio_count; i++)
    {
        // Extract the bit (0 or 1) and convert to ASCII ('0' or '1')
        // ASCII '0' is 48, so we just add the bit to 48.
        *pos++ = ((pins >> i) & 1) + 48;

        // Add comma for all but the last
        if (i < IOs->pio_count - 1)
        {
            *pos++ = ',';
        }
        else
        {
            *pos++ = ']';
            *pos++ = '}';
        }
    }

    //    printf("Payload: %s\n", payload); // Only works on the newest compilers
}

bool IO_init(io_t *_IOs)
{
    IOs = _IOs;
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);
    adc_gpio_init(29);
    adc_set_temp_sensor_enabled(true);

    for (int pin = 0; pin < _IOs->analog_count; pin++)
    {
        if (IOs->analogs[pin].enabled)
        {
        }
        else
        {
            snprintf(_IOs->analogs[pin].name, sizeof(_IOs->analogs[pin].name), "Analog %d", pin);
            _IOs->analogs[pin].ratio = 1;
            _IOs->analogs[pin].offset = 0;
        }
    }
    for (int pin = 0; pin < _IOs->pio_count; pin++)
    {
        _IOs->pios[pin].enabled = true;

        if (_IOs->pios[pin].enabled)
        {
            gpio_init(pin);
            if (_IOs->pios[pin].is_out)
            {
                sleep_ms(10); // Sleep to ensure the gpio is set before unsubscribing
                gpio_set_dir(pin, GPIO_OUT);
            }
        }
        else
        {
            snprintf(_IOs->pios[pin].name, sizeof(_IOs->pios[pin].name), "Pin %d", pin);
            _IOs->pios[pin].is_out = 0;
        }
    }
    return true;
}

int IOs_settings_JSON(char *payload, size_t max)
{
    char *pos = payload;
    size_t len;
    size_t rem = max;
    len = snprintf(pos, rem, "\"IOs\":{\"analog_count\":%d,\"pio_count\":%d, \"analog\":[", IOs->analog_count, IOs->pio_count);
    pos += len;
    rem -= len;

    for (size_t i = 0; i < IOs->analog_count; i++)
    {
        len = snprintf(pos, rem, "{\"enabled\":%s,\"name\":\"%s\",\"ratio\":%.2f,\"offset\":%.2f},",
                       IOs->analogs[i].enabled ? "true" : "false",
                       IOs->analogs[i].name, IOs->analogs[i].ratio, IOs->analogs[i].offset);
        pos += len;
        rem -= len;
    }
    pos[-1] = ']';

    len = snprintf(pos, rem, ",\"pio\":[");
    pos += len;
    rem -= len;

    for (int i = 0; i < IOs->pio_count; i++)
    {

        // Only send if buffer permits; we can skip commas for the last item
        len = snprintf(pos, rem, "{\"enabled\":%s,\"name\":\"%s\",\"out\":%s},",
                       IOs->pios[i].enabled ? "true" : "false",
                       IOs->pios[i].name,
                       IOs->pios[i].is_out ? "true" : "false");
        pos += len;
        rem -= len;
    }
    pos[-1] = ']';
    len = snprintf(pos, rem, "}");
    pos += len;
    rem -= len;

    return max-rem;

    printf("Settings Payload: %s\n", payload); // Only works on the newest compilers
}
