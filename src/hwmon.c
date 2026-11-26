#define _GNU_SOURCE

#include "fand.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

enum {
    ATTRIBUTE_SIZE = 128,
    ZERO_KELVIN = -273150,
};

static int close_fd(int fd, int result)
{
    const int error = errno;

    close(fd);
    errno = error;
    return result;
}

static DIR *open_directory(int parent, const char *path)
{
    const int fd = openat(parent, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if (fd < 0) {
        return NULL;
    }
    DIR *directory = fdopendir(fd);
    if (directory == NULL) {
        close_fd(fd, -1);
    }
    return directory;
}

static struct dirent *next_entry(DIR *directory)
{
    errno = 0;
    return readdir(directory);
}

static bool numbered(const char *name, const char *prefix)
{
    const size_t prefix_size = strlen(prefix);

    if (strncmp(name, prefix, prefix_size) != 0) {
        return false;
    }
    const char *digits = name + prefix_size;
    const size_t length = strspn(digits, "0123456789");

    return length > 0 && digits[length] == '\0';
}

static int read_value(int fd, char value[static ATTRIBUTE_SIZE])
{
    ssize_t length = pread(fd, value, ATTRIBUTE_SIZE - 1, 0);

    if (length < 0) {
        return -1;
    }
    if (length == ATTRIBUTE_SIZE - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    while (length > 0 && isspace((unsigned char)value[length - 1])) {
        --length;
    }
    value[length] = '\0';
    return 0;
}

static int read_attribute(int parent, const char *name,
                          char value[static ATTRIBUTE_SIZE])
{
    const int fd = openat(parent, name, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return -1;
    }
    return close_fd(fd, read_value(fd, value));
}

static bool attribute_matches(int parent, const char *name, const char *expected)
{
    char value[ATTRIBUTE_SIZE];

    return read_attribute(parent, name, value) == 0 &&
           strcmp(value, expected) == 0;
}

static int parse_number(const char *text, int *value)
{
    char *end;

    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno != 0 || parsed < INT_MIN || parsed > INT_MAX) {
        errno = ERANGE;
        return -1;
    }
    if (end == text || *end != '\0') {
        errno = EINVAL;
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int read_number(int parent, const char *name, int *value)
{
    char text[ATTRIBUTE_SIZE];

    if (read_attribute(parent, name, text) < 0) {
        return -1;
    }
    return parse_number(text, value);
}

static int find_hwmon(int parent, const char *name, const char *temp1_label)
{
    DIR *directory = open_directory(parent, ".");
    if (directory == NULL) {
        return -1;
    }

    int found = -1;
    int error = 0;
    for (struct dirent *entry; (entry = next_entry(directory)) != NULL;) {
        if (!numbered(entry->d_name, "hwmon")) {
            continue;
        }
        const int device = openat(dirfd(directory), entry->d_name,
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (device < 0) {
            continue;
        }
        if (!attribute_matches(device, "name", name) ||
            (temp1_label != NULL &&
             !attribute_matches(device, "temp1_label", temp1_label))) {
            close(device);
            continue;
        }
        if (found >= 0) {
            close(device);
            error = ENOTUNIQ;
            break;
        }
        found = device;
    }
    if (error == 0) {
        error = errno;
    }
    closedir(directory);
    if (error != 0 || found < 0) {
        if (found >= 0) {
            close(found);
        }
        errno = error != 0 ? error : ENODEV;
        return -1;
    }
    return found;
}

int open_cpu_sensor(int hwmon)
{
    // coretemp exposes the package temperature as temp1 on each package.
    const int device = find_hwmon(hwmon, "coretemp", "Package id 0");

    if (device < 0) {
        return -1;
    }
    return close_fd(device, openat(device, "temp1_input", O_RDONLY | O_CLOEXEC));
}

int read_cpu_temperature(int sensor, int *value)
{
    char text[ATTRIBUTE_SIZE];

    if (read_value(sensor, text) < 0) {
        return -1;
    }
    return parse_number(text, value);
}

int open_fan(int sysfs, int hwmon)
{
    char product[ATTRIBUTE_SIZE];

    if (read_attribute(sysfs, "class/dmi/id/product_name", product) < 0) {
        return -1;
    }
    if (strcmp(product, "ME mini") != 0) {
        errno = ENODEV;
        return -1;
    }

    const int fan = find_hwmon(hwmon, "it8613", NULL);
    if (fan < 0) {
        return -1;
    }
    if (flock(fan, LOCK_EX | LOCK_NB) < 0) {
        return close_fd(fan, -1);
    }
    int rpm;
    if (read_number(fan, "fan2_input", &rpm) < 0) {
        return close_fd(fan, -1);
    }
    const int pwm = openat(fan, "pwm2", O_WRONLY | O_CLOEXEC);
    if (pwm < 0) {
        return close_fd(fan, -1);
    }
    close(pwm);
    const int mode = openat(fan, "pwm2_enable", O_WRONLY | O_CLOEXEC);
    if (mode < 0) {
        return close_fd(fan, -1);
    }
    close(mode);
    return fan;
}

static int write_number(int parent, const char *name, int value)
{
    char text[ATTRIBUTE_SIZE];
    const int length = snprintf(text, sizeof(text), "%d\n", value);
    const int fd = openat(parent, name, O_WRONLY | O_CLOEXEC);

    if (fd < 0) {
        return -1;
    }
    const ssize_t written = write(fd, text, (size_t)length);
    if (written >= 0 && written != length) {
        errno = EIO;
    }
    return close_fd(fd, written == length ? 0 : -1);
}

int restore_auto(int fan)
{
    int mode;

    if (read_number(fan, "pwm2_enable", &mode) == 0 && mode == 2) {
        return 0;
    }
    return write_number(fan, "pwm2_enable", 2);
}

int apply_fan_level(int fan, enum fan_level level)
{
    int mode;

    if (read_number(fan, "pwm2_enable", &mode) < 0) {
        return -1;
    }
    if (mode != 1) {
        // The IT8613E rejects writes to pwm2 while in auto mode.
        if (write_number(fan, "pwm2_enable", 1) < 0) {
            return -1;
        }
    } else {
        int pwm;
        if (read_number(fan, "pwm2", &pwm) < 0) {
            return -1;
        }
        if (pwm == level_pwm(level)) {
            return 0;
        }
    }
    return write_number(fan, "pwm2", level_pwm(level));
}

static int sample_controller(int controller, const char *name,
                             struct fan_demand *demand, bool verbose)
{
    char transport[ATTRIBUTE_SIZE];
    if (read_attribute(controller, "transport", transport) < 0) {
        return -1;
    }
    if (strcmp(transport, "pcie") != 0) {
        return 0;
    }

    const int hwmon = find_hwmon(controller, "nvme", NULL);
    if (hwmon < 0) {
        return -1;
    }
    int temperature;
    if (read_number(hwmon, "temp1_input", &temperature) < 0) {
        return close_fd(hwmon, -1);
    }
    if (temperature <= ZERO_KELVIN) {
        errno = ENODATA;
        return close_fd(hwmon, -1);
    }
    int warning = 0;
    int alarm = 0;
    // NVMe hwmon exposes the current over-temperature threshold as temp1_max.
    read_number(hwmon, "temp1_max", &warning);
    read_number(hwmon, "temp1_alarm", &alarm);
    close(hwmon);

    const struct fan_demand sensor = alarm > 0
        ? (struct fan_demand){FAN_FULL, FAN_FULL}
        : nvme_demand(temperature, warning);
    merge_demand(demand, sensor);
    if (verbose) {
        printf("%s: composite=%.3f C", name, temperature / 1000.0);
        if (warning > 0) {
            printf(", warning=%.3f C", warning / 1000.0);
        }
        if (alarm > 0) {
            printf(", temperature alarm");
        }
        putchar('\n');
    }
    return 0;
}

bool sample_nvme(int sysfs, struct fan_demand *demand, bool verbose,
                 bool report_failure)
{
    DIR *controllers = open_directory(sysfs, "class/nvme");
    if (controllers == NULL) {
        if (errno == ENOENT) {
            return true;
        }
        if (report_failure) {
            fprintf(stderr, "Cannot enumerate NVMe controllers: %s\n",
                    strerror(errno));
        }
        return false;
    }

    bool complete = true;
    for (struct dirent *entry; (entry = next_entry(controllers)) != NULL;) {
        if (!numbered(entry->d_name, "nvme")) {
            continue;
        }
        const int controller = openat(dirfd(controllers), entry->d_name,
                                      O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        const int result = controller < 0 ? -1 : close_fd(controller,
            sample_controller(controller, entry->d_name, demand, verbose));
        if (result < 0) {
            complete = false;
            if (report_failure) {
                fprintf(stderr, "%s: cannot sample NVMe temperature: %s\n",
                        entry->d_name, strerror(errno));
            }
        }
    }
    if (errno != 0) {
        complete = false;
        if (report_failure) {
            fprintf(stderr, "Cannot finish enumerating NVMe controllers: %s\n",
                    strerror(errno));
        }
    }
    closedir(controllers);
    return complete;
}
