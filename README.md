# Teensy 4.0 and 4.1 SPI Flasher by Jak Atackka
This is a basic utility for reading and writing to flash memory chips that use the SPI interface. This was originally written for flashing the NOR chip on a PS4, but it can be adapted to a variety of devices.

It is partially based on hjudge's [SPIway utility](https://github.com/hjudges/NORway/tree/master), which was originally written for the Teensy 2.0++.

# Disclaimer
**WARNING**: Use this software at your own risk. The author accepts no responsibility for any consequences of using this software.

# Supported Hardware
- Teensy 4.0 and 4.1. This can likely be adapted to other Arduino controllers as well
- Only the **Macronix MX25L25635F** and **Spansion S25FL256L** are supported. Code changes are required to support the other chips.

This has been tested with the NOR chip still attached to the motherboard.

# Requirements
## Client Setup
Requirements:
- A JDK. The client is built and tested with Java 25.

The client lives in `javaClient` and builds with the included Gradle wrapper:

> `cd javaClient`
> `./gradlew installDist`

This produces `app/build/install/spi-flasher/bin/spi-flasher` (`spi-flasher.bat` on Windows). With GraalVM installed, `./gradlew nativeCompile` builds a standalone native executable instead.

## Teensy Software Setup
I am not distributing a `.hex` file because I haven't added a way to adjust the read/write clock speeds. You may need to adjust them to find values that are stable for your chip.

Follow [this guide](https://www.pjrc.com/teensy/td_download.html) to install the Arduino IDE and add the Teensy boards to the board manager.

To compile the code, simply open `TeensySPIFlasher/TeensySPIFlasher.ino` in the Arduino IDE and click "Verify". To deploy the code, you can click the "Upload" button from within the Arduino IDE, or you can open the build folder and deploy the `.hex` file manually.

The sketch also builds for the Waveshare RP2040-Zero. See [BUILDING.md](BUILDING.md) for installing board support for both boards and for flashing either one from the command line.

## Teensy Hardware Setup
Follow the [MODDED WARFARE guide](https://www.youtube.com/watch?v=JxeSP1PJtEs) for installing a Teensy to quickly revert the PS4's hardware.

For the Teensy 4.0 or 4.1, use the following pins:
- **CS#**: Pin 10
- **SI/SIO0**: Pin 11
- **SO/SIO1**: Pin 12
- **SCLK**: Pin 13
- **WP#/SIO2**: Pin 14
- **HOLD#/RESET#**: Pin 15

# Usage
This is designed to work much like `SPIway` (see the [SPIway README](https://github.com/hjudges/NORway/blob/master/SPIway_README.txt)). After compiling and deploying the code to your board, connect your PC to the board over USB. List the serial ports to find which one is assigned to it:

> `spi-flasher list-ports`

First, confirm that the board can read from the NOR chip and recognizes it:

> `spi-flasher --port COMx info`

Dump the ROM contents (the dump is read back and verified by default):

> `spi-flasher --port COMx read filename`

Write a new ROM to the chip and verify the contents are correct:

> `spi-flasher --port COMx write filename`

Verify the chip against a file without writing:

> `spi-flasher --port COMx verify filename`

Erase the chip:

> `spi-flasher --port COMx erase-chip`
