#pragma once
#include <Arduino.h>
#include "config.h"
#include "data.h"

// ================= SENSOR LEVEL CAIRAN (uji touch) =================
// Strip nikel + kabel langsung ke pin sentuh ESP32-S3 (GPIO 1-14).
// Mode sentuh: touchReadRaw() -> nilai turun saat disentuh / ada cairan.
// Auto-kalibrasi: baseline diambil dari N sampel awal (wadah kosong/kering),
// lalu dilacak perlahan. Terisi bila |raw - baseline| > ambang relatif.
// Mode digital (fallback): digitalRead(), aktif saat LOW (SENSOR_AKTIF_LOW).
// BELUM dipakai untuk menentukan level cairan - hanya memverifikasi sensor.

const int sensorPin[JUMLAH_SENSOR] = {
  PIN_LEVEL_HCL, PIN_LEVEL_H2O2, PIN_LEVEL_H2O, PIN_LEVEL_SABUN, PIN_LEVEL_LIMBAH
};

const char* namaSensor[JUMLAH_SENSOR] = {
  "HCl", "H2O2", "H2O", "Sabun", "Limbah"
};

bool sensorOk = true;
int  sensorRaw[JUMLAH_SENSOR];          // nilai mentah terakhir (touch: 0-255)
int  sensorBaseline[JUMLAH_SENSOR];     // baseline saat tidak terisi
int  sensorDelta[JUMLAH_SENSOR];        // baseline - raw
bool sensorAktif[JUMLAH_SENSOR];        // state setelah debounce (true = terisi)
unsigned long sensorSentuh[JUMLAH_SENSOR];  // jumlah perubahan 0->1
int  sensorCnt[JUMLAH_SENSOR];          // counter debounce
unsigned long sensorTerakhirMs = 0;
int  levelSel = 0;
bool sensorKalibrasi = true;            // true = masih mencari baseline

// Variabel level lama ( dipakai minimal agar UI kompatibel )
int  ambang[JUMLAH_SENSOR]        = { 0, 0, 0, 0, 0 };
int  ambangCadangan[JUMLAH_SENSOR];
bool ambangBerubah = false;
bool sensorIsi[JUMLAH_SENSOR];

bool sensorInit() {
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    pinMode(sensorPin[i], INPUT);
    sensorRaw[i]      = 0;
    sensorBaseline[i] = 0;
    sensorDelta[i]    = 0;
    sensorAktif[i]    = false;
    sensorCnt[i]      = 0;
    sensorSentuh[i]   = 0;
    sensorIsi[i]      = false;
  }
  sensorKalibrasi = true;
  sensorOk = true;
  dbg.println(SENSOR_TOUCH_READ ? F("[SENSOR] Mode TOUCH (strip nikel -> pin sentuh)")
                                   : F("[SENSOR] Mode DIGITAL"));
  dbg.print(F("  TOUCH_DELTA="));
  dbg.print(TOUCH_DELTA_PERSEN);
  dbg.print(F("% (min "));
  dbg.print(TOUCH_DELTA_MIN);
  dbg.print(F("), kalibrasi awal="));
  dbg.print(TOUTH_KALIB_SAMPEL);
  dbg.println(F(" sampel"));
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    dbg.print(F("  "));
    dbg.print(namaSensor[i]);
    dbg.print(F(": GPIO"));
    dbg.print(sensorPin[i]);
    dbg.println(SENSOR_TOUCH_READ ? F(" (touchRead raw)") : F(" (digitalRead)"));
  }
  return true;
}

int sensorBacaRaw(int i) {
#if SENSOR_TOUCH_READ
  // Arduino ESP32 core 2.x: touchRead() mengembalikan nilai RAW hasil
  // touch_pad_read_raw_data(), yaitu menurun saat disentuh / ada cairan.
  return touchRead(sensorPin[i]);
#else
  int v = digitalRead(sensorPin[i]);
  return SENSOR_AKTIF_LOW ? (v == LOW ? 1 : 0) : (v == HIGH ? 1 : 0);
#endif
}

// Baseline adaptif: bergerak cepat saat nilai dekat (drift kecil),
// sangat lambat saat nilai jauh agar sentuhan/cairan tidak "diserap".
// Tidak ada asumsi arah (nilai bisa naik ATAU turun).
void sensorAdaptBaseline(int i, int raw) {
  if (sensorKalibrasi) {
    if (sensorBaseline[i] == 0) sensorBaseline[i] = raw;
    else if (raw < sensorBaseline[i]) sensorBaseline[i] = raw;  // cari nilai saat kering
    return;
  }
  long d = (long)raw - sensorBaseline[i];
  int ambangAbs = (int)max((long)TOUCH_DELTA_MIN, (long)sensorBaseline[i] * TOUCH_DELTA_PERSEN / 100);
  int k = (abs(d) > ambangAbs) ? TOUCH_ADAPT_LAMBAT : TOUCH_ADAPT_CEPAT;
  if (k < 2) k = 2;
  sensorBaseline[i] = (int)(sensorBaseline[i] + d / k);
}

unsigned long sensorLogMs = 0;

// Log periodik: pastikan nilai raw selalu terlihat di Serial (untuk tuning)
void sensorLogPeriodik() {
#if SENSOR_LOG_PERIOD_MS
  unsigned long now = millis();
  if (now - sensorLogMs < SENSOR_LOG_PERIOD_MS) return;
  sensorLogMs = now;
  dbg.print(F("[RAW]"));
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    dbg.print(' ');
    dbg.print(namaSensor[i]);
    dbg.print('=');
    dbg.print(sensorRaw[i]);
    dbg.print('/');
    dbg.print(sensorBaseline[i]);
    dbg.print(sensorAktif[i] ? F("(ON)") : F("(off)"));
  }
  dbg.println();
#endif
#if SENSOR_TOUCH_READ
  bool semuaNol = true;
  for (int i = 0; i < JUMLAH_SENSOR; i++) if (sensorRaw[i] > 0) semuaNol = false;
  static bool sudahPeringat = false;
  if (semuaNol && !sudahPeringat) {
    sudahPeringat = true;
    dbg.println(F("!! [SENSOR] Semua channel touch = 0. Cek wiring strip->pin, "
                   "atau set SENSOR_TOUCH_READ=0 bila pakai output digital."));
  }
#endif
}

void sensorUpdate() {
  unsigned long now = millis();
  if (now - sensorTerakhirMs < INTERVAL_SENSOR_MS) return;
  sensorTerakhirMs = now;
  if (!sensorOk) return;

  static int sisaKalib = TOUTH_KALIB_SAMPEL;
  bool berubah = false;

  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    int raw = sensorBacaRaw(i);
    sensorRaw[i] = raw;
    sensorAdaptBaseline(i, raw);

    bool aktif;
#if SENSOR_TOUCH_READ
    int ambangAbs = (int)max((long)TOUCH_DELTA_MIN, (long)sensorBaseline[i] * TOUCH_DELTA_PERSEN / 100);
    sensorDelta[i] = raw - sensorBaseline[i];            // bertanda: bisa +/-
    aktif = !sensorKalibrasi && abs(sensorDelta[i]) >= ambangAbs;
#else
    sensorDelta[i] = raw;
    aktif = !sensorKalibrasi && (raw == 1);
#endif

    // Debounce: hitung sampel berturut-turut saat kondisi BERUBAH dari state
    if (aktif != sensorAktif[i]) {
      if (++sensorCnt[i] >= TOUCH_DEBOUNCE) {
        sensorAktif[i] = aktif;
        sensorIsi[i] = aktif;
        sensorCnt[i] = 0;
        berubah = true;
        if (aktif) sensorSentuh[i]++;   // hitung setiap perubahan 0->1
        dbg.print(F("[TOUCH] "));
        dbg.print(namaSensor[i]);
        dbg.print(aktif ? F(": AKTIF") : F(": NONAKTIF"));
        dbg.print(F(" raw="));
        dbg.print(raw);
        dbg.print(F(" base="));
        dbg.print(sensorBaseline[i]);
        dbg.print(F(" d="));
        dbg.print(sensorDelta[i]);
        dbg.print(F(" ambang="));
        dbg.print(aktif ? ambangAbs : (int)max((long)TOUCH_DELTA_MIN, (long)sensorBaseline[i] * TOUCH_DELTA_PERSEN / 100));
        dbg.print(F(" jumlah="));
        dbg.println(sensorSentuh[i]);
      }
    } else {
      sensorCnt[i] = 0;
    }
  }

  if (sensorKalibrasi && --sisaKalib <= 0) {
    sensorKalibrasi = false;
    dbg.println(F("[SENSOR] Kalibrasi selesai, siap disentuh"));
    dbg.print(F("[SENSOR] Baseline:"));
    for (int i = 0; i < JUMLAH_SENSOR; i++) {
      dbg.print(' ');
      dbg.print(namaSensor[i]);
      dbg.print('=');
      dbg.print(sensorBaseline[i]);
    }
    dbg.println();
    berubah = true;
  }
  sensorLogPeriodik();
  if (berubah && (halaman == HAL_LEVEL || halaman == HAL_LEVEL_CAIRAN)) perluGambar = true;
}

// ----- helper halaman Level Cairan (mode uji) -----
void levelMasuk() {}

void levelBuang() {}

void levelSetAmbangDariNilai(int i) {
  dbg.println(F("[LEVEL] Ambang belum dipakai (mode uji touch)"));
}

void levelUbahAmbang(int i, int arah) {}

// Persentase level cairan per kanal (0..JUMLAH_SENSOR-1).
// TODO: sensor sekarang hanya touch ON/OFF, jadi 100% saat terdeteksi, 0% saat tidak.
int levelPersen(int i) {
  if (i < 0 || i >= JUMLAH_SENSOR) return 0;
  return sensorAktif[i] ? 100 : 0;
}