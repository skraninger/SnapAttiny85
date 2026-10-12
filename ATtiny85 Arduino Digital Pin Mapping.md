In the Arduino IDE, the ATtiny85 digital I/O pin numbers map to the chip's internal port pins (PB0 through PB5) as follows:

## ATtiny85 Arduino Digital Pin Mapping

- **D0** = PB0 (Physical Pin 5)
- **D1** = PB1 (Physical Pin 6)
- **D2** = PB2 (Physical Pin 7)
- **D3** = PB3 (Physical Pin 2)
- **D4** = PB4 (Physical Pin 3)
- **D5** = PB5 (Physical Pin 1 - typically used as RESET) [1] 

------

## Physical Pinout (Counter-Clockwise from Dot/Dimple)

- **Pin 1:** PB5 (PCINT5/RESET/ADC0) -> Arduino **D5** (or Reset)
- **Pin 2:** PB3 (PCINT3/XTAL1/CLKI/ADC3) -> Arduino **D3** (Analog input A3)
- **Pin 3:** PB4 (PCINT4/XTAL2/CLKO/ADC2) -> Arduino **D4** (Analog input A2)
- **Pin 4:** GND (Ground)
- **Pin 5:** PB0 (PCINT0/SDA/AIN0/OC0A/AREF) -> Arduino **D0** (MOSI, PWM)
- **Pin 6:** PB1 (PCINT1/SDO/AIN1/OC0B/OC1A) -> Arduino **D1** (MISO, PWM)
- **Pin 7:** PB2 (PCINT2/SCK/USCK/ADC1/T0/INT0) -> Arduino **D2** (SCK, Analog input A1)
- **Pin 8:** VCC (2.7V–5.5V Power) [1, 2, 3] 

------

## Special Functions & Analog Mapping

- **Analog Inputs (`analogRead`):** Referenced in code as `A1` (Pin 7 / PB2), `A2` (Pin 3 / PB4), and `A3` (Pin 2 / PB3).
- **PWM Outputs (`analogWrite`):** Available on **D0** (PB0) and **D1** (PB1). [2] 

------

## Arduino Uno ISP Wiring Reference

To program the ATtiny85 using an Arduino Uno as an ISP: [4] 

- **ATtiny85 Pin 8 (VCC)** → Arduino **5V**
- **ATtiny85 Pin 4 (GND)** → Arduino **GND**
- **ATtiny85 Pin 1 (Reset / D5)** → Arduino **Pin 10**
- **ATtiny85 Pin 5 (D0)** → Arduino **Pin 11 (MOSI)**
- **ATtiny85 Pin 6 (D1)** → Arduino **Pin 12 (MISO)**
- **ATtiny85 Pin 7 (D2)** → Arduino **Pin 13 (SCK)**
- **Capacitor:** Place a **10 µF capacitor** between the Arduino’s **RESET** and **GND** (positive lead to Reset). [4, 5] 

If you are working on a specific project, let me know:

- **What sensors or components** you are trying to connect
- **Which Arduino board** you are using as a programmer

I can help map out the exact wiring and code for your circuit.

[1] [https://zbotic.in](https://zbotic.in/attiny85-pinout-programming-projects-complete-guide/)

[2] [https://learn.sparkfun.com](https://learn.sparkfun.com/tutorials/tiny-avr-programmer-hookup-guide/attiny85-use-hints)

[3] [https://www.ic-components.com](https://www.ic-components.com/blog/Easy-Guide-to-ATTINY85-Pinout-and-How-It-s-Used.jsp)

[4] [https://raw.org](https://raw.org/electronics/microcontrollers/programming-the-attiny85-using-arduino/)

[5] [https://www.youtube.com](https://www.youtube.com/watch?v=sycSdI49hlY)