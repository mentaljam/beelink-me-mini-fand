# beelink-me-mini-fand

A C17 Linux daemon for the Beelink ME mini's single chassis fan. It reads the
CPU package and all local PCIe NVMe temperatures through Linux hwmon and applies
the highest PWM requested by those sensors.

The default CPU fan curve comes from the original
[Beelink ME mini Fan Control project](https://github.com/ylongw/beelink-me-mini-fan-control).

## Fan curve

| PWM | CPU package threshold | NVMe composite threshold |
| --- | --------------------: | -----------------------: |
| 110 |           Below 50 °C |              Below 50 °C |
| 145 |                 50 °C |                    50 °C |
| 180 |                 55 °C |                    55 °C |
| 215 |                 60 °C |                    60 °C |
| 240 |                 65 °C |                    63 °C |
| 255 |                 70 °C |                    65 °C |

The daemon samples temperatures every 5 seconds. It increases PWM as soon as a
sample reaches a higher threshold and applies 3 °C of hysteresis when reducing
PWM. The fan stays under manual control while the daemon is running,
with a minimum PWM of 110.

At each sample, the daemon rescans for NVMe controllers and reads each controller
once, regardless of how many namespaces it exposes. If a drive reports a warning
threshold through `temp1_max`, the daemon shifts that drive's curve down as
needed to reach PWM 255 at least 5 °C before the warning threshold. A temperature
alarm (`temp1_alarm`) or an unavailable temperature reading from a detected NVMe
controller triggers PWM 255. On systems without NVMe drives, the CPU curve alone
determines the fan speed.

The daemon attempts to restore automatic mode on a graceful stop, a CPU
temperature read error, or a PWM control error. systemd restarts the daemon
after a failure. `SIGKILL` prevents the process from restoring automatic mode
before it exits.

## Build and diagnostics

Building requires a C17 compiler, make, and Linux userspace headers. The daemon
uses the C standard library and Linux APIs. The executable and object files are
built in `build/`.

```sh
make
./build/beelink-me-mini-fand --dry-run --once
```

Dry-run mode prints sensor readings and the calculated fan setting without
writing to sysfs. Omit `--once` to sample every 5 seconds.

## Run on a Beelink ME mini

Fan control requires the DMI product name `ME mini`, a `coretemp` sensor labeled
`Package id 0`, and an `it87` driver that exposes the `it8613` hwmon device
with a readable `fan2_input` and writable `pwm2` and `pwm2_enable` attributes.
Install and load the driver separately. Stop any other service controlling
the same fan before starting this daemon.

Use either a release package or `make install` on a given system.
The two installation methods are incompatible.

### Install a release package

Download the package for your distribution from [GitHub Releases](https://github.com/mentaljam/beelink-me-mini-fand/releases).
Packages require an x86-64-v3 CPU, glibc 2.38 or newer, and systemd.

| Distribution         | Install the downloaded package           |
| -------------------- | ---------------------------------------- |
| Debian / Ubuntu      | `sudo apt install ./<package>.deb`       |
| Fedora               | `sudo dnf install ./<package>.rpm`       |
| Arch Linux / Manjaro | `sudo pacman -U ./<package>.pkg.tar.zst` |

Then run `sudo systemctl enable --now beelink-me-mini-fand.service`.

### Install from source

```sh
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now beelink-me-mini-fand.service
```

Use `sudo make install-strip` to strip symbols from the installed executable
while keeping the build output intact.

To stop the daemon, run `sudo systemctl stop beelink-me-mini-fand.service`.
