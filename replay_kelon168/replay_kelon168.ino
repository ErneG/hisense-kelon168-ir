#define SEND_KELON168 true

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

const uint16_t kSendPin = 20;
const uint32_t kBaudRate = 115200;

IRsend irsend(kSendPin);

uint8_t kelonState[kKelon168StateLength] = {0};
bool hasKelonState = false;

int8_t hexValue(const char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

void printHexByte(const uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printState() {
  for (uint16_t i = 0; i < kKelon168StateLength; i++) {
    if (i) Serial.print(' ');
    printHexByte(kelonState[i]);
  }
  Serial.println();
}

bool parseKelonState(const String &input, uint8_t output[]) {
  uint8_t parsed[kKelon168StateLength] = {0};
  uint16_t nibbleCount = 0;

  for (uint16_t i = 0; i < input.length(); i++) {
    const int8_t nibble = hexValue(input[i]);
    if (nibble < 0) continue;
    if (nibbleCount >= kKelon168StateLength * 2) return false;

    const uint16_t byteIndex = nibbleCount / 2;
    if (nibbleCount % 2 == 0) {
      parsed[byteIndex] = nibble << 4;
    } else {
      parsed[byteIndex] |= nibble;
    }
    nibbleCount++;
  }

  if (nibbleCount != kKelon168StateLength * 2) return false;
  memcpy(output, parsed, kKelon168StateLength);
  return true;
}

void printHelp() {
  Serial.println();
  Serial.println(F("Hisense/Kelon KELON168 replay"));
  Serial.print(F("Transmitter GPIO: "));
  Serial.println(kSendPin);
  Serial.println(F("Paste 42 hex digits from KELON168_STATE_HEX, then Enter."));
  Serial.println(F("Commands: send, show, help"));
}

void sendKelonState() {
  if (!hasKelonState) {
    Serial.println(F("No KELON168 state loaded yet."));
    return;
  }

  Serial.print(F("Sending KELON168: "));
  printState();
  irsend.sendKelon168(kelonState, kKelon168StateLength);
  Serial.println(F("Sent."));
}

void handleLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (line.equalsIgnoreCase(F("help"))) {
    printHelp();
    return;
  }
  if (line.equalsIgnoreCase(F("show"))) {
    if (!hasKelonState) {
      Serial.println(F("No KELON168 state loaded yet."));
    } else {
      printState();
    }
    return;
  }
  if (line.equalsIgnoreCase(F("send"))) {
    sendKelonState();
    return;
  }

  uint8_t parsed[kKelon168StateLength] = {0};
  if (!parseKelonState(line, parsed)) {
    Serial.println(F("Expected exactly 42 hex digits for a 21-byte KELON168 state."));
    Serial.println(F("Example: 8306037000008D1700000080187120000000380018"));
    return;
  }

  memcpy(kelonState, parsed, kKelon168StateLength);
  hasKelonState = true;
  Serial.print(F("Loaded KELON168 state: "));
  printState();
  Serial.println(F("Type 'send' to transmit it."));
}

void setup() {
  Serial.begin(kBaudRate);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(50);
  }

  irsend.begin();
  printHelp();
}

void loop() {
  if (!Serial.available()) return;
  const String line = Serial.readStringUntil('\n');
  handleLine(line);
}
