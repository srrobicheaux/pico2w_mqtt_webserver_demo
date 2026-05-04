#ifndef analogS_H
#define analogS_H

typedef struct {
    bool enabled;
    char name[12];
    bool is_out;
} pio_t;

typedef struct {
    bool enabled;
    char name[12];
    float value;
    float ratio;
    float offset;
} analog_t;

typedef struct {
    size_t pio_count;
    pio_t pios[20];
    size_t analog_count;
    analog_t analogs[3];    
} io_t;

typedef struct {
    uint16_t version;      
    uint64_t timestamp_us;
    float analog_value[3];
} analog_log_t;

bool IO_init(io_t *IOs);
// Call this periodically from main loop
void IOs_JSON(char *payload, size_t len);
int IOs_settings_JSON(char *payload, size_t len);

#endif