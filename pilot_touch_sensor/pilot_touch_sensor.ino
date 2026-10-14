/*
  Pilot: Non-Contact Water Level Sensor via Nickel Strip
  Phase 2: Variable Measurement 0-100% + LED Brightness + Serial Command
  
  Pin Mapping:
    GPIO11 = HCl       → LED Merah
    GPIO12 = H2O2      → LED Ungu
    GPIO13 = Air       → LED Biru
    GPIO14 = Air Sabun → LED Hijau
    GPIO3  = Limbah    → LED Putih
  
  LED RGB NeoPixel: GPIO48 (1 LED onboard ESP32-S3 DevKitC-1)
  
  Serial Commands:
    a = tampilkan semua sensor
    1 = HCl only
    2 = H2O2 only
    3 = Air only
    4 = Sabun only
    5 = Limbah only
    c = enter/exit mode kalibrasi
    e = set 0% (kosong) — di mode kalibrasi
    f = set 100% (penuh) — di mode kalibrasi
  
  Alur kalibrasi per jerican:
    Ketik 1 → c → kosongkan → e → isi penuh → f → c → ketik 2 (next)
  
  Alur kalibrasi semua sekaligus:
    Ketik a → c → kosongkan SEMUA → e → isi SEMUA → f → c
*/

#include <Adafruit_NeoPixel.h>

// ================= KONFIGURASI =================
#define PIN_NEOPIXEL    48
#define NUM_PIXELS      1
#define BRIGHTNESS_MAX  255

// Pin sensor
const int PIN_SENSOR[5] = { 11, 12, 13, 14, 3 };
const char* NAMA_SENSOR[5] = { "HCl", "H2O2", "Air", "Sabun", "Limbah" };

// Warna LED per sensor (R, G, B)
const uint32_t WARNA_SENSOR[5] = {
  0xFF0000,  // Merah
  0x8000FF,  // Ungu
  0x0000FF,  // Biru
  0x00FF00,  // Hijau
  0xFFFFFF   // Putih
};

// Kalibrasi: nilai touch saat 0% (kosong) dan 100% (penuh)
int kalibKosong[5] = { 113000, 113000, 113000, 116000, 110000 };
int kalibPenuh[5]  = { 156000, 161000, 161000, 157000, 152000 };

Adafruit_NeoPixel pixels(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

int sensorPilih = 0;  // 0 = semua, 1-5 = sensor tertentu
bool modeKalibrasi = false;

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear();
  pixels.show();
  
  Serial.println(F("=== Pilot Touch Sensor Phase 2 ==="));
  Serial.println(F(""));
  printMenu();
}

void printMenu() {
  Serial.println(F("--- Menu ---"));
  Serial.println(F("a = tampilkan semua sensor"));
  Serial.println(F("1 = HCl    2 = H2O2   3 = Air"));
  Serial.println(F("4 = Sabun  5 = Limbah"));
  Serial.println(F("c = enter/exit kalibrasi (sensor terpilih / semua)"));
  Serial.println(F("e = set 0% (kosong) — di mode kalibrasi"));
  Serial.println(F("f = set 100% (penuh) — di mode kalibrasi"));
  Serial.println(F("-------------"));
}

// ================= KONVERSI =================
int kePersen(int nilai, int idx) {
  if (kalibPenuh[idx] <= kalibKosong[idx]) return 0;
  long persen = ((long)(nilai - kalibKosong[idx]) * 100) / (kalibPenuh[idx] - kalibKosong[idx]);
  if (persen < 0) persen = 0;
  if (persen > 100) persen = 100;
  return (int)persen;
}

// ================= LOOP =================
void loop() {
  // Baca semua sensor
  int nilaiTouch[5];
  for (int i = 0; i < 5; i++) {
    nilaiTouch[i] = touchRead(PIN_SENSOR[i]);
  }
  
  // Handle serial command
  if (Serial.available()) {
    char cmd = Serial.read();
    
    if (cmd == 'a' || cmd == 'A') {
      sensorPilih = 0;
      modeKalibrasi = false;
      Serial.println(F("→ Tampilkan semua sensor"));
    }
    else if (cmd >= '1' && cmd <= '5') {
      sensorPilih = cmd - '0';
      modeKalibrasi = false;
      int idx = sensorPilih - 1;
      Serial.print(F("→ Sensor: "));
      Serial.print(NAMA_SENSOR[idx]);
      Serial.print(F("  [0%="));
      Serial.print(kalibKosong[idx]);
      Serial.print(F("  100%="));
      Serial.print(kalibPenuh[idx]);
      Serial.println(F("]"));
    }
    else if (cmd == 'c' || cmd == 'C') {
      if (!modeKalibrasi) {
        modeKalibrasi = true;
        if (sensorPilih == 0) {
          Serial.println(F("→ Mode kalibrasi: SEMUA sensor"));
        } else {
          Serial.print(F("→ Mode kalibrasi: "));
          Serial.println(NAMA_SENSOR[sensorPilih - 1]);
        }
        Serial.println(F("  Kosongkan jerican → tekan 'e' untuk set 0%"));
      } else {
        modeKalibrasi = false;
        Serial.println(F("→ Exit mode kalibrasi"));
      }
    }
    else if (cmd == 'e' || cmd == 'E') {
      if (modeKalibrasi) {
        if (sensorPilih == 0) {
          // Set 0% untuk semua
          for (int i = 0; i < 5; i++) kalibKosong[i] = nilaiTouch[i];
          Serial.println(F("✓ 0% tersimpan untuk SEMUA sensor:"));
          for (int i = 0; i < 5; i++) {
            Serial.print(F("  "));
            Serial.print(NAMA_SENSOR[i]);
            Serial.print(F(": "));
            Serial.println(kalibKosong[i]);
          }
        } else {
          int idx = sensorPilih - 1;
          kalibKosong[idx] = nilaiTouch[idx];
          Serial.print(F("✓ 0% tersimpan: "));
          Serial.print(NAMA_SENSOR[idx]);
          Serial.print(F(" = "));
          Serial.println(kalibKosong[idx]);
        }
        Serial.println(F("  Isi jerican penuh → tekan 'f' untuk set 100%"));
      }
    }
    else if (cmd == 'f' || cmd == 'F') {
      if (modeKalibrasi) {
        if (sensorPilih == 0) {
          // Set 100% untuk semua
          for (int i = 0; i < 5; i++) kalibPenuh[i] = nilaiTouch[i];
          Serial.println(F("✓ 100% tersimpan untuk SEMUA sensor:"));
          for (int i = 0; i < 5; i++) {
            Serial.print(F("  "));
            Serial.print(NAMA_SENSOR[i]);
            Serial.print(F(": "));
            Serial.println(kalibPenuh[i]);
          }
        } else {
          int idx = sensorPilih - 1;
          kalibPenuh[idx] = nilaiTouch[idx];
          Serial.print(F("✓ 100% tersimpan: "));
          Serial.print(NAMA_SENSOR[idx]);
          Serial.print(F(" = "));
          Serial.println(kalibPenuh[idx]);
        }
        Serial.println(F("  Kalibrasi selesai! Tekan 'c' untuk exit"));
      }
    }
  }
  
  // Mode kalibrasi: LED putih berkedip
  if (modeKalibrasi) {
    uint32_t warna = (millis() / 500) % 2 == 0 ? 0xFFFFFF : 0x000000;
    pixels.setPixelColor(0, warna);
    pixels.show();
    
    // Print nilai live
    if (sensorPilih == 0) {
      Serial.print(F("[LIVE] "));
      for (int i = 0; i < 5; i++) {
        Serial.print(NAMA_SENSOR[i]);
        Serial.print(F("="));
        Serial.print(nilaiTouch[i]);
        if (i < 4) Serial.print(F(" "));
      }
      Serial.println();
    } else {
      int idx = sensorPilih - 1;
      Serial.print(F("[LIVE] "));
      Serial.print(NAMA_SENSOR[idx]);
      Serial.print(F("="));
      Serial.print(nilaiTouch[idx]);
      Serial.print(F("  [0%="));
      Serial.print(kalibKosong[idx]);
      Serial.print(F("  100%="));
      Serial.print(kalibPenuh[idx]);
      Serial.println(F("]"));
    }
    delay(200);
    return;
  }
  
  // Tampilkan & update LED
  if (sensorPilih == 0) {
    // Semua sensor
    for (int i = 0; i < 5; i++) {
      int persen = kePersen(nilaiTouch[i], i);
      Serial.print(NAMA_SENSOR[i]);
      Serial.print(F(": "));
      Serial.print(nilaiTouch[i]);
      Serial.print(F(" ("));
      Serial.print(persen);
      Serial.println(F("%)"));
    }
    Serial.println(F("---"));
  } else {
    // Sensor tertentu
    int idx = sensorPilih - 1;
    int persen = kePersen(nilaiTouch[idx], idx);
    Serial.print(NAMA_SENSOR[idx]);
    Serial.print(F(": "));
    Serial.print(nilaiTouch[idx]);
    Serial.print(F(" ("));
    Serial.print(persen);
    Serial.println(F("%)"));
    
    // LED: warna sensor, brightness = persen
    uint32_t warna = WARNA_SENSOR[idx];
    uint8_t r = ((warna >> 16) & 0xFF) * persen / 100;
    uint8_t g = ((warna >> 8) & 0xFF) * persen / 100;
    uint8_t b = (warna & 0xFF) * persen / 100;
    pixels.setPixelColor(0, pixels.Color(r, g, b));
    pixels.show();
  }
  
  delay(100);
}
