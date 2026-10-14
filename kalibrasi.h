#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "data.h"
#include "storage.h"     // prefs, hitungCrc, PREF_NS
#include "motor.h"
#include "dcmotor.h"

// ================= KALIBRASI: logika halaman detail =================
// Item menuKalibrasi (data.h):
//   0 Sensor Level (halaman uji touch, HAL_LEVEL)   1 Sensor Suhu   2 Sensor Udara
//   3 Motor HCL  4 Motor H2O2  5 Motor Air (stepper)
//   6 Motor Air Sabun  7 Motor Limbah  8 Motor Agitasi  9 Exhaust Fan (DC)

#define KAL_SUHU_MAKS10   50      // offset suhu maksimal +/-5.0 C (satuan 0.1 C)
#define KAL_UDARA_MAKS    4095    // nilai ADC maksimal (ESP32-S3 default 12 bit)
#define KAL_SUHU_TUNGGU   900     // ms tunggu konversi DS18B20 (12 bit = 750 ms)
#define KAL_UDARA_MS      200     // interval baca MQ135
#define KAL_STEPPER_ML    1       // volume uji pompa stepper

int  kalibItem = 0;               // item yang sedang dibuka

// --- Sensor suhu ---
OneWire           kalibOneWire(PIN_DS18B20);
DallasTemperature kalibSuhu(&kalibOneWire);
int   suhuOffset10   = 0;         // offset kalibrasi, satuan 0.1 C
float suhuMentah     = 0;         // hasil baca sensor (tanpa offset)
bool  suhuValid      = false;
bool  suhuMinta      = false;     // true = konversi sedang berjalan
unsigned long suhuMintaMs = 0;

// --- Sensor udara (MQ135) ---
int udaraAnalog = 0;              // rata-rata pembacaan terakhir
int udaraNol    = -1;             // titik nol (udara bersih); -1 = belum diatur
unsigned long udaraMs = 0;

// --- Aktuator uji ---
bool kalibSabunOn = false, kalibLimbahOn = false, kalibAgitasiOn = false, kalibFanOn = false;
int  kalibAgitasiSpeed = 3;       // 1-5

// ================= PENYIMPANAN (NVS, blob "sens") =================
#define FORMAT_VERSI_KALIB 1
struct DataKalibSensor {
  uint32_t versi;
  int32_t  suhuOffset10;
  int32_t  udaraNol;             // -1 = belum diatur
  uint32_t crc;                  // harus field terakhir
};

bool simpanKalibSensor() {
  DataKalibSensor d;
  memset(&d, 0, sizeof(d));
  d.versi        = FORMAT_VERSI_KALIB;
  d.suhuOffset10 = suhuOffset10;
  d.udaraNol     = udaraNol;
  d.crc = hitungCrc((const uint8_t*)&d, sizeof(d) - sizeof(d.crc));
  bool ok = false;
  if (prefs.begin(PREF_NS, false)) {
    ok = (prefs.putBytes("sens", &d, sizeof(d)) == sizeof(d));
    prefs.end();
  }
  dbg.println(ok ? F("[STORE] Simpan kalibrasi sensor: OK") : F("[STORE] Simpan kalibrasi sensor: GAGAL"));
  return ok;
}

void muatKalibSensor() {
  DataKalibSensor d;
  bool valid = false;
  if (prefs.begin(PREF_NS, false)) {
    if (prefs.getBytesLength("sens") == sizeof(d)) {
      prefs.getBytes("sens", &d, sizeof(d));
      valid = (d.versi == FORMAT_VERSI_KALIB) &&
              (d.crc == hitungCrc((const uint8_t*)&d, sizeof(d) - sizeof(d.crc))) &&
              (d.suhuOffset10 >= -KAL_SUHU_MAKS10 && d.suhuOffset10 <= KAL_SUHU_MAKS10) &&
              (d.udaraNol >= -1 && d.udaraNol <= KAL_UDARA_MAKS);
    }
    prefs.end();
  }
  suhuOffset10 = valid ? d.suhuOffset10 : 0;
  udaraNol     = valid ? d.udaraNol : -1;
  dbg.println(valid ? F("[STORE] Kalibrasi sensor dimuat") : F("[STORE] Kalibrasi sensor belum ada (offset 0)"));
}

void kalibSensorInit() {            // panggil sekali di setup()
  kalibSuhu.begin();
  kalibSuhu.setWaitForConversion(false);   // non-blocking
  pinMode(PIN_MQ135, INPUT);
}

// ================= AKTUATOR =================
// Matikan semua aktuator uji. Return true bila ada yang tadinya aktif.
bool kalibStopAktuator() {
  bool ada = kalibSabunOn || kalibLimbahOn || kalibAgitasiOn || kalibFanOn ||
             pompaJalan[0] || pompaJalan[1] || pompaJalan[2];
  kalibSabunOn = kalibLimbahOn = kalibAgitasiOn = kalibFanOn = false;
  pompaStopAll();
  dcmotorStopAll();
  return ada;
}

void kalibMasuk(int item) {
  kalibStopAktuator();
  kalibItem = item;
  suhuMinta = false;
  suhuValid = false;
  udaraMs = 0;
  if (item == 1) {                  // mulai baca suhu langsung
    kalibSuhu.requestTemperatures();
    suhuMinta = true;
    suhuMintaMs = millis();
  }
  dbg.print(F("[KALIB] Masuk item "));
  dbg.println(item);
}

void kalibKeluar() {
  kalibStopAktuator();
  suhuMinta = false;
}

float suhuAkhir() { return suhuMentah + suhuOffset10 / 10.0f; }

// Dipanggil dari loop(): baca sensor yang sedang ditampilkan, tanpa blocking.
void kalibUpdate() {
  if (halaman != HAL_KALIBRASI_DETAIL) return;
  unsigned long now = millis();

  if (kalibItem == 1) {
    if (suhuMinta && (now - suhuMintaMs >= KAL_SUHU_TUNGGU)) {
      float c = kalibSuhu.getTempCByIndex(0);
      bool ok = (c != DEVICE_DISCONNECTED_C);
      if (ok != suhuValid || (ok && c != suhuMentah)) perluGambar = true;
      suhuValid = ok;
      if (ok) suhuMentah = c;
      kalibSuhu.requestTemperatures();
      suhuMintaMs = now;
    }
  } else if (kalibItem == 2) {
    if (now - udaraMs >= KAL_UDARA_MS) {
      udaraMs = now;
      long jml = 0;
      for (int i = 0; i < 8; i++) jml += analogRead(PIN_MQ135);
      int v = (int)(jml / 8);
      if (v != udaraAnalog) perluGambar = true;
      udaraAnalog = v;
    }
  } else if (kalibItem >= 3 && kalibItem <= 5) {
    static bool jalanLalu = false;      // status pompa berubah -> gambar ulang
    bool j = pompaJalan[kalibItem - 3];
    if (j != jalanLalu) { jalanLalu = j; perluGambar = true; }
  }
}

// Tangani tombol di halaman detail (BACK ditangani di .ino).
// Return pesan toast atau nullptr.
const char* kalibTombol(int t) {
  perluGambar = true;
  switch (kalibItem) {
    case 1:   // ---- suhu: KIRI/KANAN offset, ENTER simpan
      if (t == P_KIRI)  suhuOffset10 = max(-KAL_SUHU_MAKS10, suhuOffset10 - 1);
      if (t == P_KANAN) suhuOffset10 = min( KAL_SUHU_MAKS10, suhuOffset10 + 1);
      if (t == P_ENTER) return simpanKalibSensor() ? "Kalibrasi disimpan" : "Gagal simpan!";
      break;

    case 2:   // ---- udara: KIRI/KANAN titik nol, ENTER simpan
      if (t == P_KIRI || t == P_KANAN) {
        if (udaraNol < 0) udaraNol = udaraAnalog;        // pertama kali: mulai dari nilai sekarang
        else udaraNol = constrain(udaraNol + (t == P_KANAN ? 1 : -1) * 5, 0, KAL_UDARA_MAKS);
      }
      if (t == P_ENTER) {
        if (udaraNol < 0) return "Atur nol dulu";
        return simpanKalibSensor() ? "Kalibrasi disimpan" : "Gagal simpan!";
      }
      break;

    case 3: case 4: case 5:   // ---- stepper: ENTER pompa 1 ml, KIRI stop
      if (t == P_ENTER && !pompaJalan[kalibItem - 3]) pompaMulai(kalibItem - 3, KAL_STEPPER_ML);
      if (t == P_KIRI) pompaStopAll();
      break;

    case 6:  if (t == P_ENTER) { kalibSabunOn  = !kalibSabunOn;  pompaSabunSet(kalibSabunOn); }   break;
    case 7:  if (t == P_ENTER) { kalibLimbahOn = !kalibLimbahOn; pompaLimbahSet(kalibLimbahOn); } break;
    case 9:  if (t == P_ENTER) { kalibFanOn    = !kalibFanOn;    fanSet(kalibFanOn); }            break;
    case 8:   // ---- agitasi: ENTER nyala/mati, KIRI/KANAN kecepatan 1-5
      if (t == P_ENTER) { kalibAgitasiOn = !kalibAgitasiOn; agitasiSet(kalibAgitasiOn ? kalibAgitasiSpeed : 0); }
      if (t == P_KIRI)  kalibAgitasiSpeed = max(1, kalibAgitasiSpeed - 1);
      if (t == P_KANAN) kalibAgitasiSpeed = min(5, kalibAgitasiSpeed + 1);
      if (kalibAgitasiOn && (t == P_KIRI || t == P_KANAN)) agitasiSet(kalibAgitasiSpeed);
      break;
  }
  return nullptr;
}
#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "data.h"
#include "storage.h"     // prefs, hitungCrc, PREF_NS
#include "motor.h"
#include "dcmotor.h"

// ================= KALIBRASI: logika halaman detail =================
// Item menuKalibrasi (data.h):
//   0 Sensor Level (halaman uji touch, HAL_LEVEL)   1 Sensor Suhu   2 Sensor Udara
//   3 Motor HCL  4 Motor H2O2  5 Motor Air (stepper)
//   6 Motor Air Sabun  7 Motor Limbah  8 Motor Agitasi  9 Exhaust Fan (DC)

#define KAL_SUHU_MAKS10   50      // offset suhu maksimal +/-5.0 C (satuan 0.1 C)
#define KAL_UDARA_MAKS    4095    // nilai ADC maksimal (ESP32-S3 default 12 bit)
#define KAL_SUHU_TUNGGU   900     // ms tunggu konversi DS18B20 (12 bit = 750 ms)
#define KAL_UDARA_MS      200     // interval baca MQ135
#define KAL_STEPPER_ML    1       // volume uji pompa stepper

int  kalibItem = 0;               // item yang sedang dibuka

// --- Sensor suhu ---
OneWire           kalibOneWire(PIN_DS18B20);
DallasTemperature kalibSuhu(&kalibOneWire);
int   suhuOffset10   = 0;         // offset kalibrasi, satuan 0.1 C
float suhuMentah     = 0;         // hasil baca sensor (tanpa offset)
bool  suhuValid      = false;
bool  suhuMinta      = false;     // true = konversi sedang berjalan
unsigned long suhuMintaMs = 0;

// --- Sensor udara (MQ135) ---
int udaraAnalog = 0;              // rata-rata pembacaan terakhir
int udaraNol    = -1;             // titik nol (udara bersih); -1 = belum diatur
unsigned long udaraMs = 0;

// --- Aktuator uji ---
bool kalibSabunOn = false, kalibLimbahOn = false, kalibAgitasiOn = false, kalibFanOn = false;
int  kalibAgitasiSpeed = 3;       // 1-5

// ================= PENYIMPANAN (NVS, blob "sens") =================
#define FORMAT_VERSI_KALIB 1
struct DataKalibSensor {
  uint32_t versi;
  int32_t  suhuOffset10;
  int32_t  udaraNol;             // -1 = belum diatur
  uint32_t crc;                  // harus field terakhir
};

bool simpanKalibSensor() {
  DataKalibSensor d;
  memset(&d, 0, sizeof(d));
  d.versi        = FORMAT_VERSI_KALIB;
  d.suhuOffset10 = suhuOffset10;
  d.udaraNol     = udaraNol;
  d.crc = hitungCrc((const uint8_t*)&d, sizeof(d) - sizeof(d.crc));
  bool ok = false;
  if (prefs.begin(PREF_NS, false)) {
    ok = (prefs.putBytes("sens", &d, sizeof(d)) == sizeof(d));
    prefs.end();
  }
  dbg.println(ok ? F("[STORE] Simpan kalibrasi sensor: OK") : F("[STORE] Simpan kalibrasi sensor: GAGAL"));
  return ok;
}

void muatKalibSensor() {
  DataKalibSensor d;
  bool valid = false;
  if (prefs.begin(PREF_NS, false)) {
    if (prefs.getBytesLength("sens") == sizeof(d)) {
      prefs.getBytes("sens", &d, sizeof(d));
      valid = (d.versi == FORMAT_VERSI_KALIB) &&
              (d.crc == hitungCrc((const uint8_t*)&d, sizeof(d) - sizeof(d.crc))) &&
              (d.suhuOffset10 >= -KAL_SUHU_MAKS10 && d.suhuOffset10 <= KAL_SUHU_MAKS10) &&
              (d.udaraNol >= -1 && d.udaraNol <= KAL_UDARA_MAKS);
    }
    prefs.end();
  }
  suhuOffset10 = valid ? d.suhuOffset10 : 0;
  udaraNol     = valid ? d.udaraNol : -1;
  dbg.println(valid ? F("[STORE] Kalibrasi sensor dimuat") : F("[STORE] Kalibrasi sensor belum ada (offset 0)"));
}

void kalibSensorInit() {            // panggil sekali di setup()
  kalibSuhu.begin();
  kalibSuhu.setWaitForConversion(false);   // non-blocking
  pinMode(PIN_MQ135, INPUT);
}

// ================= AKTUATOR =================
// Matikan semua aktuator uji. Return true bila ada yang tadinya aktif.
bool kalibStopAktuator() {
  bool ada = kalibSabunOn || kalibLimbahOn || kalibAgitasiOn || kalibFanOn ||
             pompaJalan[0] || pompaJalan[1] || pompaJalan[2];
  kalibSabunOn = kalibLimbahOn = kalibAgitasiOn = kalibFanOn = false;
  pompaStopAll();
  dcmotorStopAll();
  return ada;
}

void kalibMasuk(int item) {
  kalibStopAktuator();
  kalibItem = item;
  suhuMinta = false;
  suhuValid = false;
  udaraMs = 0;
  if (item == 1) {                  // mulai baca suhu langsung
    kalibSuhu.requestTemperatures();
    suhuMinta = true;
    suhuMintaMs = millis();
  }
  dbg.print(F("[KALIB] Masuk item "));
  dbg.println(item);
}

void kalibKeluar() {
  kalibStopAktuator();
  suhuMinta = false;
}

float suhuAkhir() { return suhuMentah + suhuOffset10 / 10.0f; }

// Dipanggil dari loop(): baca sensor yang sedang ditampilkan, tanpa blocking.
void kalibUpdate() {
  if (halaman != HAL_KALIBRASI_DETAIL) return;
  unsigned long now = millis();

  if (kalibItem == 1) {
    if (suhuMinta && (now - suhuMintaMs >= KAL_SUHU_TUNGGU)) {
      float c = kalibSuhu.getTempCByIndex(0);
      bool ok = (c != DEVICE_DISCONNECTED_C);
      if (ok != suhuValid || (ok && c != suhuMentah)) perluGambar = true;
      suhuValid = ok;
      if (ok) suhuMentah = c;
      kalibSuhu.requestTemperatures();
      suhuMintaMs = now;
    }
  } else if (kalibItem == 2) {
    if (now - udaraMs >= KAL_UDARA_MS) {
      udaraMs = now;
      long jml = 0;
      for (int i = 0; i < 8; i++) jml += analogRead(PIN_MQ135);
      int v = (int)(jml / 8);
      if (v != udaraAnalog) perluGambar = true;
      udaraAnalog = v;
    }
  } else if (kalibItem >= 3 && kalibItem <= 5) {
    static bool jalanLalu = false;      // status pompa berubah -> gambar ulang
    bool j = pompaJalan[kalibItem - 3];
    if (j != jalanLalu) { jalanLalu = j; perluGambar = true; }
  }
}

// Tangani tombol di halaman detail (BACK ditangani di .ino).
// Return pesan toast atau nullptr.
const char* kalibTombol(int t) {
  perluGambar = true;
  switch (kalibItem) {
    case 1:   // ---- suhu: KIRI/KANAN offset, ENTER simpan
      if (t == P_KIRI)  suhuOffset10 = max(-KAL_SUHU_MAKS10, suhuOffset10 - 1);
      if (t == P_KANAN) suhuOffset10 = min( KAL_SUHU_MAKS10, suhuOffset10 + 1);
      if (t == P_ENTER) return simpanKalibSensor() ? "Kalibrasi disimpan" : "Gagal simpan!";
      break;

    case 2:   // ---- udara: KIRI/KANAN titik nol, ENTER simpan
      if (t == P_KIRI || t == P_KANAN) {
        if (udaraNol < 0) udaraNol = udaraAnalog;        // pertama kali: mulai dari nilai sekarang
        else udaraNol = constrain(udaraNol + (t == P_KANAN ? 1 : -1) * 5, 0, KAL_UDARA_MAKS);
      }
      if (t == P_ENTER) {
        if (udaraNol < 0) return "Atur nol dulu";
        return simpanKalibSensor() ? "Kalibrasi disimpan" : "Gagal simpan!";
      }
      break;

    case 3: case 4: case 5:   // ---- stepper: ENTER pompa 1 ml, KIRI stop
      if (t == P_ENTER && !pompaJalan[kalibItem - 3]) pompaMulai(kalibItem - 3, KAL_STEPPER_ML);
      if (t == P_KIRI) pompaStopAll();
      break;

    case 6:  if (t == P_ENTER) { kalibSabunOn  = !kalibSabunOn;  pompaSabunSet(kalibSabunOn); }   break;
    case 7:  if (t == P_ENTER) { kalibLimbahOn = !kalibLimbahOn; pompaLimbahSet(kalibLimbahOn); } break;
    case 9:  if (t == P_ENTER) { kalibFanOn    = !kalibFanOn;    fanSet(kalibFanOn); }            break;
    case 8:   // ---- agitasi: ENTER nyala/mati, KIRI/KANAN kecepatan 1-5
      if (t == P_ENTER) { kalibAgitasiOn = !kalibAgitasiOn; agitasiSet(kalibAgitasiOn ? kalibAgitasiSpeed : 0); }
      if (t == P_KIRI)  kalibAgitasiSpeed = max(1, kalibAgitasiSpeed - 1);
      if (t == P_KANAN) kalibAgitasiSpeed = min(5, kalibAgitasiSpeed + 1);
      if (kalibAgitasiOn && (t == P_KIRI || t == P_KANAN)) agitasiSet(kalibAgitasiSpeed);
      break;
  }
  return nullptr;
}
