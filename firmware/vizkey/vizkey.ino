#include <BleKeyboard.h>

BleKeyboard bleKeyboard("VizKey", "Nini", 100);

void setup() {
  Serial.begin(115200);
  bleKeyboard.begin();
  Serial.println("VizKey starting...");
}

void loop() {
  if (bleKeyboard.isConnected()) {
    Serial.println("Connected — sending keypress");
    bleKeyboard.print("hello from vizkey");
    delay(2000);
  }
}