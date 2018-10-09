# Nokia 5110 Beagle Bone Black Driver

This driver provides an interface to a Nokia 5110 LCD on a Beaglebone Black.  This driver currently only provides character support.

This driver creates a nokia_5110 class with an attached nokia0 device.  Open and write to the device through the nokia device.

### Hookup Details:

The Nokia 5110 breakout is supplied by [Sparkfun](https://www.sparkfun.com/products/10168). This driver does not use the SPI MOSI or SCLK.  Instead it bitbangs out the data.  This gives the user more freedome in choosing connections for the breakout board.  The current wire setup is:

1. Data or Command Line (D/C)   - GPIO 44
2. Reset (RST)                  - GPIO 68
3. Chip (SCE)                   - GPIO 67
4. Data Out (MOSI)              - GPIO 26
5. Clock out (SCLKD)            - GPIO 46