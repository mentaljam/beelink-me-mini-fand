#define _GNU_SOURCE

#include "fand.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#ifndef SYSFS_ROOT
#define SYSFS_ROOT "/sys"
#endif

enum { SAMPLE_INTERVAL = 5 };

struct controller {
    int sysfs;
    int cpu_sensor;
    int fan;
    enum fan_level level;
    bool first_sample;
    bool nvme_complete;
    bool dry_run;
};

static int sample(struct controller *controller)
{
    int temperature;
    if (read_cpu_temperature(controller->cpu_sensor, &temperature) < 0) {
        perror("Cannot read CPU package temperature");
        return -1;
    }
    if (controller->dry_run) {
        printf("CPU package: %.3f C\n", temperature / 1000.0);
    }
    struct fan_demand demand = cpu_demand(temperature);
    const bool complete = sample_nvme(controller->sysfs, &demand,
        controller->dry_run, controller->dry_run || controller->nvme_complete);
    if (!complete) {
        demand = (struct fan_demand){FAN_FULL, FAN_FULL};
    } else if (!controller->nvme_complete) {
        fputs("NVMe temperature monitoring recovered\n", stderr);
    }
    controller->nvme_complete = complete;

    const enum fan_level level = select_level(demand, controller->level);
    if (!controller->dry_run && apply_fan_level(controller->fan, level) < 0) {
        perror("Cannot apply fan setting");
        return -1;
    }
    if (controller->dry_run || controller->first_sample ||
        level != controller->level) {
        FILE *log = controller->dry_run ? stdout : stderr;
        fprintf(log, "Fan: PWM %d\n", level_pwm(level));
        fflush(log);
    }
    controller->first_sample = false;
    controller->level = level;
    return 0;
}

static int open_signal_fd(void)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        return -1;
    }
    return signalfd(-1, &mask, SFD_CLOEXEC);
}

static int run(struct controller *controller, int timer, int signals)
{
    struct pollfd events[] = {
        {.fd = signals, .events = POLLIN},
        {.fd = timer, .events = POLLIN},
    };
    const struct itimerspec interval = {
        .it_value = {.tv_nsec = 1},
        .it_interval = {.tv_sec = SAMPLE_INTERVAL},
    };
    if (timerfd_settime(timer, 0, &interval, NULL) < 0) {
        perror("Cannot start sampling timer");
        return -1;
    }

    for (;;) {
        if (poll(events, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Cannot wait for timer or signal");
            return -1;
        }
        if (events[0].revents & POLLIN) {
            return 0;
        }
        if (!(events[1].revents & POLLIN)) {
            fputs("Unexpected timer or signal descriptor event\n", stderr);
            return -1;
        }
        uint64_t expirations;
        if (read(timer, &expirations, sizeof(expirations)) != sizeof(expirations)) {
            fputs("Cannot read sampling timer\n", stderr);
            return -1;
        }
        if (sample(controller) < 0) {
            return -1;
        }
    }
}

static void usage(FILE *output)
{
    fputs("Usage: beelink-me-mini-fand [--dry-run [--once]]\n"
          "  --dry-run  Display temperatures and fan demand without writing PWM\n"
          "  --once     Sample once in dry-run mode and exit\n", output);
}

int main(int argc, char **argv)
{
    struct controller controller = {
        .sysfs = -1, .cpu_sensor = -1, .fan = -1,
        .level = FAN_LOW, .first_sample = true, .nvme_complete = true,
    };
    bool once = false;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--dry-run") == 0) {
            controller.dry_run = true;
        } else if (strcmp(argv[index], "--once") == 0) {
            once = true;
        } else if (strcmp(argv[index], "--help") == 0) {
            usage(stdout);
            return EXIT_SUCCESS;
        } else {
            usage(stderr);
            return EXIT_FAILURE;
        }
    }
    if (once && !controller.dry_run) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    int status = EXIT_FAILURE;
    int signals = -1;
    int timer = -1;
    controller.sysfs = open(SYSFS_ROOT, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (controller.sysfs < 0) {
        perror("Cannot open sysfs");
        goto cleanup;
    }
    const int hwmon = openat(controller.sysfs, "class/hwmon",
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (hwmon < 0) {
        perror("Cannot open hwmon");
        goto cleanup;
    }
    controller.cpu_sensor = open_cpu_sensor(hwmon);
    if (controller.cpu_sensor < 0) {
        perror("Cannot find coretemp Package id 0");
        close(hwmon);
        goto cleanup;
    }
    if (!controller.dry_run) {
        controller.fan = open_fan(controller.sysfs, hwmon);
        if (controller.fan < 0) {
            perror("Cannot acquire the ME mini it8613 pwm2 controller");
            close(hwmon);
            goto cleanup;
        }
    }
    close(hwmon);

    if (once) {
        status = sample(&controller) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        goto cleanup;
    }
    signals = open_signal_fd();
    if (signals < 0) {
        perror("Cannot set up termination signals");
        goto cleanup;
    }
    timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (timer < 0) {
        perror("Cannot create sampling timer");
        goto cleanup;
    }
    status = run(&controller, timer, signals) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    if (controller.fan >= 0 && restore_auto(controller.fan) < 0) {
        perror("Cannot restore automatic fan control");
        status = EXIT_FAILURE;
    }

cleanup:
    if (timer >= 0) {
        close(timer);
    }
    if (signals >= 0) {
        close(signals);
    }
    if (controller.fan >= 0) {
        close(controller.fan);
    }
    if (controller.cpu_sensor >= 0) {
        close(controller.cpu_sensor);
    }
    if (controller.sysfs >= 0) {
        close(controller.sysfs);
    }
    return status;
}
