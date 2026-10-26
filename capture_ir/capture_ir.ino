#define DECODE_KELON168 true
#define DECODE_HASH true
#define DECODE_AC true

#include <Arduino.h>
#include <IRac.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>

const uint16_t kRecvPin = 20;
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = 35;

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

void printHexByte(const uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printKelon168State(const decode_results &decoded) {
  if (decoded.decode_type != KELON168 || decoded.bits != kKelon168Bits) return;

  Serial.print(F("KELON168_STATE_HEX="));
  for (uint16_t i = 0; i < kKelon168StateLength; i++) {
    printHexByte(decoded.state[i]);
  }
  Serial.println();

  Serial.print(F("KELON168_STATE_BYTES="));
  for (uint16_t i = 0; i < kKelon168StateLength; i++) {
    if (i) Serial.print(' ');
    printHexByte(decoded.state[i]);
  }
  Serial.println();
}

uint32_t rawUsec(const decode_results &decoded, const uint16_t rawIndex) {
  return decoded.rawbuf[rawIndex + kStartOffset] * kRawTick;
}

bool looksLikeKelon168(const decode_results &decoded) {
  if (decoded.rawlen < 320 || decoded.rawlen > 350) return false;
  const uint32_t headerMark = rawUsec(decoded, 0);
  const uint32_t headerSpace = rawUsec(decoded, 1);
  return headerMark > 7000 && headerMark < 11000 &&
         headerSpace > 3000 && headerSpace < 6000;
}

int8_t classifyKelonSpace(const uint32_t usec) {
  if (usec > 1200 && usec < 2300) return 1;
  if (usec > 300 && usec < 1000) return 0;
  return -1;
}

uint16_t parseKelonBits(const decode_results &decoded, uint16_t rawIndex,
                        const uint16_t bitCount, uint8_t state[],
                        uint16_t *parsedBits) {
  for (uint16_t bit = 0; bit < bitCount; bit++) {
    if (rawIndex + 1 >= decoded.rawlen - kStartOffset) return rawIndex;
    const int8_t value = classifyKelonSpace(rawUsec(decoded, rawIndex + 1));
    if (value < 0) return rawIndex;

    if (value) {
      const uint16_t globalBit = *parsedBits;
      state[globalBit / 8] |= 1 << (globalBit % 8);
    }
    (*parsedBits)++;
    rawIndex += 2;
  }
  return rawIndex;
}

void printFallbackKelon168(const decode_results &decoded) {
  if (decoded.decode_type == KELON168 || !looksLikeKelon168(decoded)) return;

  uint8_t state[kKelon168StateLength] = {0};
  uint16_t rawIndex = 2;
  uint16_t parsedBits = 0;

  rawIndex = parseKelonBits(decoded, rawIndex, 48, state, &parsedBits);
  rawIndex += 2;
  rawIndex = parseKelonBits(decoded, rawIndex, 64, state, &parsedBits);
  rawIndex += 2;
  rawIndex = parseKelonBits(decoded, rawIndex, 56, state, &parsedBits);

  Serial.print(F("KELON168_FALLBACK_BITS="));
  Serial.println(parsedBits);
  Serial.print(F("KELON168_FALLBACK_HEX="));
  for (uint16_t i = 0; i < kKelon168StateLength; i++) {
    printHexByte(state[i]);
  }
  Serial.println();
  Serial.print(F("KELON168_FALLBACK_BYTES="));
  for (uint16_t i = 0; i < kKelon168StateLength; i++) {
    if (i) Serial.print(' ');
    printHexByte(state[i]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(kBaudRate);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(50);
  }

  Serial.println();
  Serial.println(F("Hisense/Kelon IR capture"));
  Serial.print(F("Receiver GPIO: "));
  Serial.println(kRecvPin);
  Serial.print(F("Baud: "));
  Serial.println(kBaudRate);

  const int8_t sanity = irutils::lowLevelSanityCheck();
  if (sanity != 0) {
    Serial.print(F("IRremoteESP8266 sanity check failed: "));
    Serial.println(sanity);
  }

#if DECODE_HASH
  irrecv.setUnknownThreshold(kMinUnknownSize);
#endif
  irrecv.setTolerance(kTolerancePercentage);
  irrecv.enableIRIn(true);

  Serial.println(F("Ready. Press a remote button and watch for KELON168."));
}

void loop() {
  if (!irrecv.decode(&results)) return;

  const uint32_t now = millis();
  Serial.println();
  Serial.print(F("Timestamp : "));
  Serial.print(now / 1000);
  Serial.print('.');
  if (now % 1000 < 100) Serial.print('0');
  if (now % 1000 < 10) Serial.print('0');
  Serial.println(now % 1000);

  if (results.overflow) {
    Serial.print(F("WARNING: capture buffer full. Size: "));
    Serial.println(kCaptureBufferSize);
  }

  Serial.print(F("Library   : IRremoteESP8266 "));
  Serial.println(_IRREMOTEESP8266_VERSION_STR);
  Serial.print(F("Protocol  : "));
  Serial.println(typeToString(results.decode_type));
  Serial.print(F("Bits      : "));
  Serial.println(results.bits);
  Serial.print(F("Raw length: "));
  Serial.println(results.rawlen);

  Serial.print(resultToHumanReadableBasic(&results));
  printKelon168State(results);
  printFallbackKelon168(results);

  const String description = IRAcUtils::resultAcToString(&results);
  if (description.length()) {
    Serial.print(F("A/C desc  : "));
    Serial.println(description);
  }

  Serial.println(resultToSourceCode(&results));
  Serial.println(F("---"));
  yield();
}
