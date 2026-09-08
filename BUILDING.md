# Building and flashing the firmware

The sketch in `TeensySPIFlasher/TeensySPIFlasher.ino` builds for four boards:

| Board                   | Arduino FQBN                          | Board package                      |
|-------------------------|---------------------------------------|------------------------------------|
| Teensy 4.0              | `teensy:avr:teensy40`                 | Teensy (PJRC)                      |
| Teensy 4.1              | `teensy:avr:teensy41`                 | Teensy (PJRC)                      |
| Waveshare RP2040-Zero   | `rp2040:rp2040:waveshare_rp2040_zero` | arduino-pico (Earle F. Philhower)  |
| Raspberry Pi Pico       | `rp2040:rp2040:rpipico`               | arduino-pico (Earle F. Philhower)  |

Pin assignments for each board are in the "Board configuration" block at the top
of the sketch. Any other board fails to compile with an "Unsupported board"
error until a pin mapping is added there.

All four boards present a USB CDC serial port, so the Java client works
unchanged with any of them. The baud rate it sets is ignored.

The RP2040-Zero and the Pico share one pin mapping, so the same wiring works
on either. Build for the board you actually have: the board entry also selects
the USB product name and the flash bootloader stage tuned to that board's
flash chip, so firmware built for one should not be loaded onto the other.

## Installing board support

Board packages are shared between the Arduino IDE 2.x and `arduino-cli`. Both
read and write the same data directory:

| OS      | Arduino data directory        |
|---------|-------------------------------|
| Linux   | `~/.arduino15`                |
| macOS   | `~/Library/Arduino15`         |
| Windows | `%LOCALAPPDATA%\Arduino15`    |

A package installed from one tool is available to the other. The additional
board manager URLs are also shared, in `arduino-cli.yaml` inside that
directory.

The two package index URLs are:

```
https://www.pjrc.com/teensy/package_teensy_index.json
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

Do not use Arduino's own "Arduino Mbed OS RP2040 Boards" package for either
RP2040 board. The sketch is written against arduino-pico.

### Arduino IDE 2.x

1. Open File > Preferences (Arduino IDE > Settings on macOS).
2. Paste both URLs above into "Additional boards manager URLs", one per line.
3. Open Tools > Board > Boards Manager.
4. Search for "teensy" and install "Teensy (for Arduino IDE 2.0.4 or later)".
5. Search for "pico" and install "Raspberry Pi Pico/RP2040/RP2350" by
   Earle F. Philhower, III.
6. Select the board under Tools > Board: "Teensy 4.0" under Teensy, or
   "Waveshare RP2040 Zero" or "Raspberry Pi Pico" under Raspberry Pi
   Pico/RP2040.

For the RP2040 boards, leave Tools > USB Stack at its default "Pico SDK". That
is the stack that provides the CDC `Serial` the sketch uses.

### arduino-cli

Install `arduino-cli` from https://arduino.github.io/arduino-cli/ (Homebrew,
a release tarball, or the install script). Then register the URLs and install
both cores:

```
arduino-cli config add board_manager.additional_urls \
    https://www.pjrc.com/teensy/package_teensy_index.json \
    https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install teensy:avr
arduino-cli core install rp2040:rp2040
```

If no config file exists yet, run `arduino-cli config init` first. Do not pass
`--overwrite` on a machine that also has the Arduino IDE installed, since that
replaces the IDE's shared config file.

To compile, run from the repository root:

```
arduino-cli compile --fqbn teensy:avr:teensy40 --output-dir build/teensy40 TeensySPIFlasher
arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero --output-dir build/rp2040-zero TeensySPIFlasher
arduino-cli compile --fqbn rp2040:rp2040:rpipico --output-dir build/rpipico TeensySPIFlasher
```

`--output-dir` copies the finished firmware to a known location. The Teensy
build produces `TeensySPIFlasher.ino.hex`; the RP2040 build produces
`TeensySPIFlasher.ino.uf2` along with `.elf` and `.bin`.

The arduino-pico core runs several Python 3 helper scripts during the build.
On macOS and Windows it ships its own interpreter. On Linux it calls whatever
`python3` is on `PATH`, so a Python 3 must be installed.

### Linux udev rules

On Linux, USB access to both bootloaders needs udev rules.

- Teensy: download https://www.pjrc.com/teensy/00-teensy.rules and copy it to
  `/etc/udev/rules.d/`.
- RP2040: the picotool repository ships `udev/99-picotool.rules`; copy it to
  `/etc/udev/rules.d/`. Alternatively, mounting the RPI-RP2 drive needs no
  rules at all.

Reload with `sudo udevadm control --reload-rules && sudo udevadm trigger`, or
replug the board.

## Flashing a Teensy 4.0 or 4.1 from the command line

The Teensy Loader GUI is not required. Three options exist.

### teensy_loader_cli (PJRC)

Source and prebuilt notes: https://github.com/PaulStoffregen/teensy_loader_cli.
Homebrew has it as `teensy_loader_cli`.

```
teensy_loader_cli --mcu=TEENSY40 -w -v build/teensy40/TeensySPIFlasher.ino.hex
```

Use `--mcu=TEENSY41` for a Teensy 4.1. Flags:

- `-w` waits for the board to enter bootloader mode. Press the button on the
  Teensy after starting the command.
- `-s` requests a soft reboot into the bootloader through the running
  firmware's USB serial port, so no button press is needed. It works with this
  sketch because it enumerates as a CDC device. If the request fails, the tool
  falls back to waiting, so `-s -w` together is a good default.
- `-v` prints progress.

### tycmd (tytools)

https://github.com/Koromix/tytools. Homebrew and most Linux distributions
package it as `tytools`.

```
tycmd upload build/teensy40/TeensySPIFlasher.ino.hex
```

`tycmd` finds the board, reboots it into the bootloader itself, uploads, and
restarts it. `tycmd list` shows connected Teensys, and `--board` selects one
when several are attached.

### arduino-cli upload

```
arduino-cli upload --fqbn teensy:avr:teensy40 TeensySPIFlasher
```

This runs PJRC's `teensy_post_compile` from the core package, which launches
the Teensy Loader GUI in the background to perform the transfer. It works from
a terminal but does spawn the GUI process, so it is not suitable for a headless
machine.

## Flashing an RP2040 board from the command line

The RP2040 has a USB mass storage bootloader in ROM, so no loader software is
strictly needed. Three options exist. The examples use the RP2040-Zero paths;
substitute `build/rpipico` and `rp2040:rp2040:rpipico` for a Pico.

### Copy the UF2 to the RPI-RP2 drive

1. Hold the BOOT button (BOOTSEL on the Pico) while plugging the board in, or
   hold it and tap RESET.
2. A drive named `RPI-RP2` appears.
3. Copy `build/rp2040-zero/TeensySPIFlasher.ino.uf2` onto it.

The board reboots into the new firmware as soon as the copy finishes. This
works on any OS with no drivers or udev rules.

### picotool

https://github.com/raspberrypi/picotool. Homebrew packages it as `picotool`.
The arduino-pico core also bundles a copy at
`<Arduino data directory>/packages/rp2040/tools/pqt-picotool/<version>/picotool`.

```
picotool load -f -x build/rp2040-zero/TeensySPIFlasher.ino.uf2
```

Flags:

- `-f` asks the running firmware to reboot into the bootloader over USB, so no
  button press is needed. arduino-pico firmware includes the reset interface
  this relies on. If the board is already in bootloader mode, omit it.
- `-x` resets the board into the new firmware after loading.

`picotool info -a` reports what is on a board that is in bootloader mode,
including the arduino-pico build details.

### arduino-cli upload

```
arduino-cli upload --fqbn rp2040:rp2040:waveshare_rp2040_zero TeensySPIFlasher
```

The core's default upload tool is the bundled picotool, invoked with the same
`-f -x` flags as above, so this reboots a running board automatically. A board
that has never been flashed has no serial port for arduino-cli to find; put it
in bootloader mode with the BOOT or BOOTSEL button first, and the upload
proceeds against the bootloader.

## Verifying a board after flashing

Once flashed, the board enumerates as a USB serial port. Confirm the firmware
responds with the Java client:

```
cd javaClient
./gradlew run --args="list-ports"
./gradlew run --args="--port <port> info"
```

The `--port` option belongs to the top-level command and comes before the
subcommand. `info` reads the firmware version and the flash chip's JEDEC ID.
