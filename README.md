![My GIF](assets/robo.gif)


## Wiring

* **Display → ESP32-S3**

  | ESP32-S3 Pin | TFT Pin   |
  | ------------ | --------- |
  | 3V3          | VCC / 3V3 |
  | GND          | GND       |
  | GPIO14       | CS        |
  | GPIO13       | RST       |
  | GPIO21       | D/C       |
  | GPIO47       | DIN       |
  | GPIO48       | CLK       |
  | 3V3          | BL        |
---
* **Touch Sensor → ESP32-S3**

  | ESP32-S3 Pin | Touch Pin |
  | ------------ | --------- |
  | 3V3          | VCC       |
  | GND          | GND       |
  | GPIO15       | SIG       |
---
* **Vibration Sensor → ESP32-S3**

  | ESP32-S3 Pin | Touch Pin |
  | ------------ | --------- |
  | 3V3          | VCC       |
  | GND          | GND       |
  | GPIO18       | DO        |
