const int led = 0;
const int sleep = 1000;

// the setup routine runs once when you press reset:
void setup() {
  // initialize the digital pin as an output.
  bitSet(DDRB, led);
}

// the loop routine runs over and over again forever
void loop() {
  bitSet(PINB, led);
  delay(sleep);
}