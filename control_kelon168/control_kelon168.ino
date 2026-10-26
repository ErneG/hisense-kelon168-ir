#define SEND_KELON168 true

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <ir_Kelon.h>

const uint16_t kSendPin = 20;
const uint32_t kBaudRate = 115200;

IRKelon168Ac ac(kSendPin);

void printHexByte(const uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printRawState() {
  uint8_t *state = ac.getRaw(true);
  for (uint16_t i = 0; i < kKelon168StateLength; i++) {
    if (i) Serial.print(' ');
    printHexByte(state[i]);
  }
  Serial.println();
}

void printStatus() {
  Serial.print(F("State: "));
  Serial.println(ac.toString());
  Serial.print(F("Raw:   "));
  printRawState();
}

void printHelp() {
  Serial.println();
  Serial.println(F("Hisense/Kelon KELON168 control"));
  Serial.print(F("Transmitter GPIO: "));
  Serial.println(kSendPin);
  Serial.println(F("Commands:"));
  Serial.println(F("  on"));
  Serial.println(F("  off"));
  Serial.println(F("  cool 22"));
  Serial.println(F("  heat 21"));
  Serial.println(F("  temp 23"));
  Serial.println(F("  fan auto|min|low|med|high|max"));
  Serial.println(F("  swing on|off"));
  Serial.println(F("  light on|off"));
  Serial.println(F("  raw"));
  Serial.println(F("  help"));
}

int parseTemp(const String &line, const int defaultTemp) {
  const int space = line.indexOf(' ');
  if (space < 0) return defaultTemp;
  const int value = line.substring(space + 1).toInt();
  if (value < kKelon168MinTemp || value > kKelon168MaxTemp) return defaultTemp;
  return value;
}

bool parseOnOff(const String &line, bool *value) {
  if (line.endsWith(F(" on"))) {
    *value = true;
    return true;
  }
  if (line.endsWith(F(" off"))) {
    *value = false;
    return true;
  }
  return false;
}

void sendCurrentState() {
  printStatus();
  ac.send();
  Serial.println(F("Sent."));
}

void handleFan(const String &line) {
  if (line.endsWith(F(" auto"))) ac.setFan(kKelon168FanAuto);
  else if (line.endsWith(F(" min"))) ac.setFan(kKelon168FanMin);
  else if (line.endsWith(F(" low"))) ac.setFan(kKelon168FanLow);
  else if (line.endsWith(F(" med"))) ac.setFan(kKelon168FanMedium);
  else if (line.endsWith(F(" medium"))) ac.setFan(kKelon168FanMedium);
  else if (line.endsWith(F(" high"))) ac.setFan(kKelon168FanHigh);
  else if (line.endsWith(F(" max"))) ac.setFan(kKelon168FanMax);
  else {
    Serial.println(F("Expected: fan auto|min|low|med|high|max"));
    return;
  }
  sendCurrentState();
}

void handleLine(String line) {
  line.trim();
  line.toLowerCase();
  if (!line.length()) return;

  if (line == F("help")) {
    printHelp();
    return;
  }
  if (line == F("raw")) {
    printStatus();
    return;
  }
  if (line == F("on")) {
    ac.setPower(true);
    sendCurrentState();
    return;
  }
  if (line == F("off")) {
    ac.setPower(false);
    sendCurrentState();
    return;
  }
  if (line.startsWith(F("cool"))) {
    ac.setPower(true);
    ac.setMode(kKelon168Cool);
    ac.setTemp(parseTemp(line, ac.getTemp()));
    sendCurrentState();
    return;
  }
  if (line.startsWith(F("heat"))) {
    ac.setPower(true);
    ac.setMode(kKelon168Heat);
    ac.setTemp(parseTemp(line, ac.getTemp()));
    sendCurrentState();
    return;
  }
  if (line.startsWith(F("temp"))) {
    ac.setTemp(parseTemp(line, ac.getTemp()));
    sendCurrentState();
    return;
  }
  if (line.startsWith(F("fan"))) {
    handleFan(line);
    return;
  }
  if (line.startsWith(F("swing"))) {
    bool on = false;
    if (!parseOnOff(line, &on)) {
      Serial.println(F("Expected: swing on|off"));
      return;
    }
    ac.setSwing(on);
    sendCurrentState();
    return;
  }
  if (line.startsWith(F("light"))) {
    bool on = false;
    if (!parseOnOff(line, &on)) {
      Serial.println(F("Expected: light on|off"));
      return;
    }
    ac.setLight(on);
    sendCurrentState();
    return;
  }

  Serial.println(F("Unknown command. Type 'help'."));
}

void setup() {
  Serial.begin(kBaudRate);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(50);
  }

  ac.begin();
  ac.setModel(kelon168_ac_remote_model_t::DG11R201);
  ac.setPower(true);
  ac.setMode(kKelon168Cool);
  ac.setTemp(22);
  ac.setFan(kKelon168FanAuto);
  printHelp();
  printStatus();
}

void loop() {
  if (!Serial.available()) return;
  handleLine(Serial.readStringUntil('\n'));
}
