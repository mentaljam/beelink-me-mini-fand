#include "fand.h"

#include <stddef.h>

enum {
    HYSTERESIS = 3000,
    NVME_WARNING_MARGIN = 5000,
};

static struct fan_demand curve_demand(int temperature, const int *thresholds,
                                      int shift)
{
    struct fan_demand demand = {FAN_AUTO, FAN_AUTO};

    for (enum fan_level level = FAN_LOW; level <= FAN_FULL; ++level) {
        const int threshold = thresholds[level] - shift;

        if (temperature >= threshold) {
            demand.rise = level;
        }
        if (temperature >= threshold - HYSTERESIS) {
            demand.hold = level;
        }
    }
    return demand;
}

struct fan_demand cpu_demand(int temperature)
{
    static const int thresholds[] = {
        [FAN_LOW] = 45000, [FAN_MEDIUM_LOW] = 50000, [FAN_MEDIUM] = 55000,
        [FAN_MEDIUM_HIGH] = 60000, [FAN_HIGH] = 65000, [FAN_FULL] = 70000,
    };

    return curve_demand(temperature, thresholds, 0);
}

struct fan_demand nvme_demand(int temperature, int warning)
{
    static const int thresholds[] = {
        [FAN_LOW] = 45000, [FAN_MEDIUM_LOW] = 50000, [FAN_MEDIUM] = 55000,
        [FAN_MEDIUM_HIGH] = 60000, [FAN_HIGH] = 63000, [FAN_FULL] = 65000,
    };
    int shift = 0;

    if (warning > NVME_WARNING_MARGIN &&
        warning < thresholds[FAN_FULL] + NVME_WARNING_MARGIN) {
        shift = thresholds[FAN_FULL] + NVME_WARNING_MARGIN - warning;
    }
    return curve_demand(temperature, thresholds, shift);
}

void merge_demand(struct fan_demand *total, struct fan_demand sensor)
{
    if (sensor.rise > total->rise) {
        total->rise = sensor.rise;
    }
    if (sensor.hold > total->hold) {
        total->hold = sensor.hold;
    }
}

enum fan_level select_level(struct fan_demand demand, enum fan_level previous)
{
    if (demand.rise >= previous) {
        return demand.rise;
    }
    return demand.hold < previous ? demand.hold : previous;
}

int level_pwm(enum fan_level level)
{
    static const int pwm[] = {
        [FAN_LOW] = 110, [FAN_MEDIUM_LOW] = 145, [FAN_MEDIUM] = 180,
        [FAN_MEDIUM_HIGH] = 215, [FAN_HIGH] = 240, [FAN_FULL] = 255,
    };

    return pwm[level];
}
