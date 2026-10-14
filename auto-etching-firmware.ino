/*
  Menu OLED 6 Tombol - v0.5.0
  Perubahan dari v0.4.1:
    - sensor.h: sensor level sekarang baca GPIO langsung (ganti MPR121 I2C)
    - motor.h (baru): driver stepper DRV8825 x3, non-blocking, ml -> langkah
    - dcmotor.h (baru): PWM TB6612FNG x2 (agitasi 15, sabun 16, limbah 17, fan 18)
    - process.h: fase DOSING/ETCHING/DRAINING/RINSING kini menggerakkan aktuator
    - E-stop (GPIO8): hentikan semua aktuator & batalkan proses
  Perubahan dari v0.3:
    - Tombol KIRI/KANAN ditukar (config.h)
    - sensor.h: baca 6 sensor level cairan (MPR121), rata-rata + histeresis
    - Menu "Level Cairan": nilai, ambang, status ISI/KSG per sensor;
      ENTER = set ambang dari nilai sekarang (tangki KOSONG), KIRI/KANAN = fine tune,
      BACK = simpan/buang ambang (NVS)
  Catatan v0.3:
    - storage.h: preset disimpan di NVS (Preferences) + versi + checksum;
      data korup/versi beda -> otomatis kembali ke default
    - presetops.h: tambah / duplikat / hapus / edit preset + validasi
    - Menu "Konfigurasi Preset": List -> Aksi (Edit/Duplikat/Hapus) -> Edit
      Edit: ATAS/BAWAH pilih field, KIRI/KANAN ubah nilai, ENTER simpan
      %H2O dan semua ml dihitung otomatis dari %HCl, %H2O2, dan Volume
  Library: Wire, Adafruit_GFX, Adafruit_SSD1306, Preferences (ESP32)
*/

#include <Wire.h>
#include "dbg.h"
#include "config.h"
#include "input.h"
#include "data.h"
#include "presetops.h"
#include "sensor.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "storage.h"
#include "buzzer.h"
#include "process.h"
#include "ui.h"

unsigned long waktuRenderTerakhir = 0;

OneWire ds18b20Wire(PIN_DS18B20);
DallasTemperature ds18b20(&ds18b20Wire);

#ifdef DEBUG_RENDER
unsigned long dbgWaktu = 0;
int dbgJumlahRender = 0;
#endif

// ================= HELPER KONFIGURASI =================
void simpanDanToast(const char* ok) {
  tampilToast(simpanPreset() ? ok : "Gagal simpan!");
}

void editGeserField(int delta) {
  editField = constrain(editField + delta, 0, JUMLAH_FIELD - 1);
  if (editField < editScroll) editScroll = editField;
  if (editField >= editScroll + EDIT_VISIBLE) editScroll = editField - EDIT_VISIBLE + 1;
}

void mulaiEdit() {
  presetEdit = daftarPreset[cfgIdx];
  editBerubah = false;
  editField = 0;
  editScroll = 0;
  dbg.print(F("[CFG] Edit: "));
  dbg.println(presetEdit.nama);
  pindahHalaman(HAL_CFG_EDIT);
}

void simpanEdit() {
  daftarPreset[cfgIdx] = presetEdit;
  editBerubah = false;
}

// ================= NAVIGASI =================
void tanganiTombol(int t) {
  dbg.print(F("[TOMBOL] "));
  dbg.println(namaTombol[t]);
  buzzerKlik();                 // feedback bunyi saat tombol ditekan
  perluGambar = true;

  switch (halaman) {

    case HAL_MENU_UTAMA:
      if (t == P_ATAS)  listGeser(L_UTAMA, -1, JUMLAH_MENU);
      if (t == P_BAWAH) listGeser(L_UTAMA, +1, JUMLAH_MENU);
      if (t == P_ENTER) {
        if (lsSel[L_UTAMA] == 0) {
          listReset(L_PRESET);
          pindahHalaman(HAL_PRESET_LIST);
        } else if (lsSel[L_UTAMA] == 1) {
          listReset(L_CFG);
          pindahHalaman(HAL_CFG_LIST);
        } else if (lsSel[L_UTAMA] == 2) {
          levelCairanScroll = 0;
          pindahHalaman(HAL_LEVEL_CAIRAN);
        } else if (lsSel[L_UTAMA] == 3) {
          listReset(L_KALIBRASI);
          pindahHalaman(HAL_KALIBRASI);
        } else {
          pindahHalaman(HAL_PLACEHOLDER);
        }
      }
      break;

    case HAL_PRESET_LIST:
      if (jumlahPreset > 0) {
        if (t == P_ATAS)  listGeser(L_PRESET, -1, jumlahPreset);
        if (t == P_BAWAH) listGeser(L_PRESET, +1, jumlahPreset);
        if (t == P_ENTER) {
          presetTerpilih = lsSel[L_PRESET];
          presetDetailScroll = 0;
          pindahHalaman(HAL_PRESET_DETAIL);
        }
      }
      if (t == P_BACK) pindahHalaman(HAL_MENU_UTAMA);
      break;

    case HAL_PRESET_DETAIL: {
      int maxScroll = max(0, DETAIL_CONTENT_H - (SCREEN_HEIGHT - HEADER_H));
      if (t == P_ATAS)  presetDetailScroll = max(0, presetDetailScroll - DETAIL_SCROLL_STEP);
      if (t == P_BAWAH) presetDetailScroll = min(maxScroll, presetDetailScroll + DETAIL_SCROLL_STEP);
      if (t == P_ENTER) { prosesMulai(); pindahHalaman(HAL_PROSES); }
      if (t == P_BACK)  pindahHalaman(HAL_PRESET_LIST);
      break;
    }

    case HAL_PROSES:
      if (dialogBatal) {
        if (t == P_ENTER) {
          prosesBatal();
          pindahHalaman(HAL_PRESET_DETAIL);
        } else if (t == P_BACK) {
          dialogBatal = false;
          dbg.println(F("[PROSES] Batal dibatalkan (lanjut)"));
        }
      } else if (prosesSelesai()) {
        if (t == P_ENTER || t == P_BACK) {
          prosesKeluarSelesai();
          pindahHalaman(HAL_PRESET_DETAIL);
        }
      } else {
        if (t == P_ENTER) prosesToggleJeda();
        if (t == P_BACK) {
          dialogBatal = true;
          dbg.println(F("[PROSES] Konfirmasi batal ditampilkan"));
        }
      }
      break;

    case HAL_PLACEHOLDER:
      if (t == P_BACK) pindahHalaman(HAL_MENU_UTAMA);
      break;

    // ---------- Level Cairan ----------
    case HAL_LEVEL:
      if (dialogCfg == DLG_SIMPAN) {
        if (t == P_ENTER) {
          dialogCfg = DLG_NONE;
          ambangBerubah = false;
          pindahHalaman(HAL_MENU_UTAMA);
          tampilToast(simpanAmbang() ? "Ambang tersimpan" : "Gagal simpan!");
        } else if (t == P_BACK) {
          dbg.println(F("[LEVEL] Perubahan ambang dibuang"));
          dialogCfg = DLG_NONE;
          levelBuang();
          pindahHalaman(HAL_MENU_UTAMA);
        }
      } else {
        if (t == P_ATAS)  levelSel = max(0, levelSel - 1);
        if (t == P_BAWAH) levelSel = min(JUMLAH_SENSOR - 1, levelSel + 1);
        if (sensorOk) {
          if (t == P_KIRI) {
            // kalibrasi ulang: wadah harus kosong/kering saat ENTER berikutnya
            for (int i = 0; i < JUMLAH_SENSOR; i++) {
              sensorBaseline[i] = 0;
              sensorAktif[i] = false;
              sensorCnt[i] = 0;
            }
            sensorKalibrasi = true;
            tampilToast("Kalibrasi ulang");
          }
          if (t == P_KANAN) dbg.println(F("[TOUCH] Kanan tidak dipakai (mode uji)"));
          if (t == P_ENTER) {
            for (int i = 0; i < JUMLAH_SENSOR; i++) sensorSentuh[i] = 0;
            tampilToast("Counter direset");
          }
        }
        if (t == P_BACK) {
          if (ambangBerubah) dialogCfg = DLG_SIMPAN;
          else pindahHalaman(HAL_MENU_UTAMA);
        }
      }
      break;

    // ---------- Level Cairan (bar persentase) ----------
    case HAL_LEVEL_CAIRAN:
      if (t == P_ATAS)  levelCairanScroll = max(0, levelCairanScroll - LEVEL_CAIRAN_STEP);
      if (t == P_BAWAH) {
        int mx = max(0, LEVEL_CAIRAN_CONTENT_H - (AREA_BAWAH - HEADER_H));
        levelCairanScroll = min(mx, levelCairanScroll + LEVEL_CAIRAN_STEP);
      }
      if (t == P_BACK) pindahHalaman(HAL_MENU_UTAMA);
      break;

    // ---------- Kalibrasi: menu ----------
    case HAL_KALIBRASI:
      if (t == P_ATAS)  listGeser(L_KALIBRASI, -1, JUMLAH_KALIBRASI);
      if (t == P_BAWAH) listGeser(L_KALIBRASI, +1, JUMLAH_KALIBRASI);
      if (t == P_ENTER) {
        int ki = lsSel[L_KALIBRASI];
        if (ki == 0) {
          levelMasuk();
          pindahHalaman(HAL_LEVEL);
        } else {
          agitasiSet(0);
          pompaSabunSet(false);
          pompaLimbahSet(false);
          fanSet(false);
          pompaStopAll();
          kalibSabunOn = false;
          kalibLimbahOn = false;
          kalibAgitasiOn = false;
          kalibFanOn = false;
          kalibrasiItem = ki;
          pindahHalaman(HAL_KALIBRASI_DETAIL);
        }
      }
      if (t == P_BACK) {
        agitasiSet(0);
        pompaSabunSet(false);
        pompaLimbahSet(false);
        fanSet(false);
        pompaStopAll();
        kalibSabunOn = false;
        kalibLimbahOn = false;
        kalibAgitasiOn = false;
        kalibFanOn = false;
        pindahHalaman(HAL_MENU_UTAMA);
      }
      break;

    // ---------- Kalibrasi: per item ----------
    case HAL_KALIBRASI_DETAIL:
      if (t == P_ENTER) {
        if (kalibrasiItem >= 3 && kalibrasiItem <= 5) {
          int idx = kalibrasiItem - 3;
          pompaMulai(idx, 1);
        } else if (kalibrasiItem == 6) {
          kalibSabunOn = !kalibSabunOn;
          pompaSabunSet(kalibSabunOn);
        } else if (kalibrasiItem == 7) {
          kalibLimbahOn = !kalibLimbahOn;
          pompaLimbahSet(kalibLimbahOn);
        } else if (kalibrasiItem == 8) {
          kalibAgitasiOn = !kalibAgitasiOn;
          agitasiSet(kalibAgitasiOn ? 3 : 0);
        } else if (kalibrasiItem == 9) {
          kalibFanOn = !kalibFanOn;
          fanSet(kalibFanOn);
        }
        perluGambar = true;
      }
      if (t == P_BACK) {
        agitasiSet(0);
        pompaSabunSet(false);
        pompaLimbahSet(false);
        fanSet(false);
        pompaStopAll();
        kalibSabunOn = false;
        kalibLimbahOn = false;
        kalibAgitasiOn = false;
        kalibFanOn = false;
        pindahHalaman(HAL_KALIBRASI);
      }
      break;

    // ---------- Konfigurasi: daftar preset + "+ Tambah" ----------
    case HAL_CFG_LIST: {
      int n = jumlahItemCfg();
      if (t == P_ATAS)  listGeser(L_CFG, -1, n);
      if (t == P_BAWAH) listGeser(L_CFG, +1, n);
      if (t == P_ENTER) {
        int idx = lsSel[L_CFG];
        if (idx < jumlahPreset) {
          cfgIdx = idx;
          listReset(L_AKSI);
          pindahHalaman(HAL_CFG_AKSI);
        } else if (tambahPreset()) {
          dbg.print(F("[CFG] Tambah: "));
          dbg.println(daftarPreset[jumlahPreset - 1].nama);
          listSet(L_CFG, jumlahPreset - 1);
          simpanDanToast("Preset ditambah");
        }
      }
      if (t == P_BACK) pindahHalaman(HAL_MENU_UTAMA);
      break;
    }

    // ---------- Konfigurasi: Edit / Duplikat / Hapus ----------
    case HAL_CFG_AKSI:
      if (dialogCfg == DLG_HAPUS) {
        if (t == P_ENTER) {
          dbg.print(F("[CFG] Hapus: "));
          dbg.println(daftarPreset[cfgIdx].nama);
          hapusPreset(cfgIdx);
          dialogCfg = DLG_NONE;
          listSet(L_CFG, min(cfgIdx, jumlahItemCfg() - 1));
          pindahHalaman(HAL_CFG_LIST);
          simpanDanToast("Preset dihapus");
        } else if (t == P_BACK) {
          dialogCfg = DLG_NONE;
        }
      } else {
        if (t == P_ATAS)  listGeser(L_AKSI, -1, 3);
        if (t == P_BAWAH) listGeser(L_AKSI, +1, 3);
        if (t == P_ENTER) {
          int a = lsSel[L_AKSI];
          if (a == 0) {
            mulaiEdit();
          } else if (a == 1) {
            if (duplikatPreset(cfgIdx)) {
              dbg.print(F("[CFG] Duplikat -> "));
              dbg.println(daftarPreset[jumlahPreset - 1].nama);
              listSet(L_CFG, jumlahPreset - 1);
              pindahHalaman(HAL_CFG_LIST);
              simpanDanToast("Preset diduplikat");
            } else {
              tampilToast("Penuh (maks 10)");
            }
          } else {
            dialogCfg = DLG_HAPUS;
          }
        }
        if (t == P_BACK) pindahHalaman(HAL_CFG_LIST);
      }
      break;

    // ---------- Konfigurasi: Edit field ----------
    case HAL_CFG_EDIT:
      if (dialogCfg == DLG_SIMPAN) {
        if (t == P_ENTER) {
          simpanEdit();
          dialogCfg = DLG_NONE;
          pindahHalaman(HAL_CFG_AKSI);
          simpanDanToast("Tersimpan");
        } else if (t == P_BACK) {
          dbg.println(F("[CFG] Perubahan dibuang"));
          dialogCfg = DLG_NONE;
          editBerubah = false;
          pindahHalaman(HAL_CFG_AKSI);
        }
      } else {
        if (t == P_ATAS)  editGeserField(-1);
        if (t == P_BAWAH) editGeserField(+1);
        if (t == P_KIRI || t == P_KANAN) {
          Preset sebelum = presetEdit;
          ubahField(presetEdit, editField, (t == P_KANAN) ? +1 : -1);
          if (memcmp(&sebelum, &presetEdit, sizeof(Preset)) != 0) editBerubah = true;
        }
        if (t == P_ENTER) {
          simpanEdit();
          simpanDanToast("Tersimpan");
        }
        if (t == P_BACK) {
          if (editBerubah) dialogCfg = DLG_SIMPAN;
          else pindahHalaman(HAL_CFG_AKSI);
        }
      }
      break;
  }
}

// ================= SETUP =================
void setup() {
  dbgInit();
  delay(300);
  dbg.print(F("[BOOT] Menu_OLED_6Button "));
  dbg.println(F(FW_VERSI));

  muatPreset();
  muatAmbang();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK);

  if (!tulisPCF8574(0xFF))
    dbg.println(F("!! PCF8574 tidak terdeteksi. Cek wiring/alamat I2C."));
  inputInit();

  if (!sensorInit())
    dbg.println(F("!! Sensor level tidak siap. Cek wiring GPIO."));

  motorInit();
  dcmotorInit();
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(false);
  buzzerInit();
  pinMode(PIN_ESTOP, INPUT_PULLUP);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    dbg.println(F("!! OLED tidak terdeteksi di 0x3C. Cek wiring/alamat I2C."));
  } else {
    oled.setRotation(0);
    gambarLayar();
    perluGambar = false;
  }

  // Port USB baru terhubung setelah host membuka monitor; cetak ulang banner
  // agar pesan boot tidak hilang.
  delay(2000);
  dbgInit();
  dbg.print(F("[BOOT] Menu_OLED_6Button "));
  dbg.println(F(FW_VERSI));

  // Nada boot diputar di akhir setup(): kalau dipanggil lebih awal, melodi
  // akan tertahan oleh delay() di atas (buzzerUpdate baru jalan di loop).
  buzzerBoot();
}

// ================= LOOP =================
void loop() {
  int t = bacaTombolBaru();
  if (t != TIDAK_ADA) tanganiTombol(t);

  prosesUpdate();                            // logika proses, tidak tergantung render
  motorUpdate();                             // stepper non-blocking
  sensorUpdate();                            // baca sensor level cairan
  buzzerUpdate();                            // pola nada non-blocking

  // DS18B20: pembacaan non-blocking saat halaman kalibrasi suhu aktif
  if (halaman == HAL_KALIBRASI_DETAIL && kalibrasiItem == 1) {
    static unsigned long suhuReqMs = 0;
    static bool suhuMenunggu = false;
    unsigned long now = millis();
    if (!suhuMenunggu || (now - suhuReqMs >= 1000)) {
      ds18b20.requestTemperatures();
      suhuReqMs = now;
      suhuMenunggu = true;
    } else if (suhuMenunggu && (now - suhuReqMs >= 900)) {
      float tc = ds18b20.getTempCByIndex(0);
      if (tc != DEVICE_DISCONNECTED_C) {
        kalibrasiSuhuC = tc;
        kalibrasiSuhuValid = true;
      } else {
        kalibrasiSuhuValid = false;
      }
      suhuMenunggu = false;
    }
  }

  // E-stop: hentikan semua aktuator & batalkan proses
  if (digitalRead(PIN_ESTOP) == LOW) {
    if (prosesJalan) {
      prosesBatal();
      tampilToast("E-STOP!");
    }
    agitasiSet(0);
    pompaLimbahSet(false);
    pompaSabunSet(false);
    fanSet(false);
    pompaStopAll();
    kalibSabunOn = false;
    kalibLimbahOn = false;
    kalibAgitasiOn = false;
    kalibFanOn = false;
  }

  bool animasiBerjalan = false;
  for (int L = 0; L < JUMLAH_LIST; L++) {
    if (lsAnimasi[L]) animasiBerjalan = true;   // termasuk frame terakhir animasi
    listUpdateAnimasi(L);
  }
  if (animasiBerjalan) perluGambar = true;

  if (toastAktif && (long)(millis() - toastSampai) >= 0) {
    toastAktif = false;
    perluGambar = true;
  }

  unsigned long sekarang = millis();
  if (perluGambar && (sekarang - waktuRenderTerakhir >= INTERVAL_RENDER_MS)) {
    perluGambar = false;                     // sebelum gambar, agar tombol saat display() tidak hilang
    gambarLayar();
    waktuRenderTerakhir = sekarang;
#ifdef DEBUG_RENDER
    dbgJumlahRender++;
#endif
#ifdef DEBUG_LAMBAT_RENDER_MS
    delay(DEBUG_LAMBAT_RENDER_MS);
#endif

    // Baca ulang tombol setelah oled.display() (bagian paling lama memblokir I2C)
    int t2 = bacaTombolBaru();
    if (t2 != TIDAK_ADA) tanganiTombol(t2);
  }

#ifdef DEBUG_RENDER
  if (sekarang - dbgWaktu >= 1000) {
    dbg.print(F("[DBG] render/detik: "));
    dbg.println(dbgJumlahRender);
    dbgJumlahRender = 0;
    dbgWaktu = sekarang;
  }
#endif

  // Render periodik (live) untuk halaman Level Cairan & Kalibrasi Detail
  if ((halaman == HAL_LEVEL_CAIRAN || halaman == HAL_KALIBRASI_DETAIL) &&
      (millis() - waktuRenderTerakhir >= 200)) {
    perluGambar = true;
  }

  delay(2);
}
