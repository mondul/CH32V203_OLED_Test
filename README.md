# CH32V203 OLED Test

PlatformIO test project using the [WeAct Studio BluePill+ CH32V203C8T6](https://www.aliexpress.com/item/1005006117720765.html) with the [ch32fun](https://github.com/cnlohr/ch32fun) development environment.

A [WCH LinkE](https://www.aliexpress.com/item/1005005983875152.html) debugger/programmer is preferred for uploading code to the microcontroller.

This test uses the internal RTC and shows the preconfigured epoch timestamp as a readable date and time in a [SSD1306 based 128×64 OLED display](https://www.aliexpress.com/item/1005004355547926.html) connected via I<sup>2</sup>C. Also toggles a [relay module](https://www.aliexpress.com/item/1005003750654499.html) when pressing the board's KEY push button.

#### Board connections:

| Board pin | Connected to            |
|-----------|-------------------------|
| SW GND    | LinkE's GND             |
| SW SCK    | LinkE's SWCLK           |
| SW DIO    | LinkE's SWDIO           |
| SW 3V3    | LinkE's 3V3             |
| 3V3       | OLED's Vᴅᴅ, relay's Vᴄᴄ |
| G         | OLED's GND, relay's Gɴᴅ |
| A9        | LinkE's RX              |
| A10       | LinkE's TX              |
| B6        | OLED's SCK[^1]          |
| B7        | OLED's SDA[^1]          |
| B12       | Relay's Iɴ1             |

[^1]: A pull-up resistor (1KΩ or higher) might be needed.
