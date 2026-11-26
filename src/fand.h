#ifndef FAND_H
#define FAND_H

#include <stdbool.h>

enum fan_level {
    FAN_LOW,
    FAN_MEDIUM_LOW,
    FAN_MEDIUM,
    FAN_MEDIUM_HIGH,
    FAN_HIGH,
    FAN_FULL,
};

struct fan_demand {
    enum fan_level rise;
    enum fan_level hold;
};

struct fan_demand cpu_demand(int temperature);
struct fan_demand nvme_demand(int temperature, int warning);
void merge_demand(struct fan_demand *total, struct fan_demand sensor);
enum fan_level select_level(struct fan_demand demand, enum fan_level previous);
int level_pwm(enum fan_level level);

int open_cpu_sensor(int hwmon);
int read_cpu_temperature(int sensor, int *value);
int open_fan(int sysfs, int hwmon);
int apply_fan_level(int fan, enum fan_level level);
int restore_auto(int fan);
bool sample_nvme(int sysfs, struct fan_demand *demand, bool verbose,
                 bool report_failure);

#endif
