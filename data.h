#pragma once
#include <Arduino.h>

// ================= DATA PRESET =================
// Catatan: semua field int + char[12] -> tanpa padding (aman untuk disimpan sebagai blob)
struct Preset {
  char  nama[12];
  int   pcbPanjang;      // mm
  int   pcbLebar;        // mm
  int   persenHCl;       // %
  int   persenH2O2;      // %
  int   persenH2O;       // % (otomatis = 100 - HCl - H2O2)
  int   mlHCl;
  int   mlH2O2;
  int   mlH2O;
  int   agitasiDetik;    // durasi agitasi per siklus
  int   agitasiSpeed;    // level kecepatan 1-5
  int   totalDetik;      // estimasi lama total proses
};

#define MAKS_PRESET    10
#define JUMLAH_DEFAULT 5

// Data default (dipakai saat pertama kali / data korup)
const Preset presetDefault[JUMLAH_DEFAULT] = {
  { "Preset 1", 100, 80, 33, 17, 50, 120,  60, 180,  5, 3, 270 },
  { "Preset 2",  80, 60, 40, 20, 40, 100,  50, 100,  8, 4, 330 },
  { "Preset 3", 150,100, 25, 25, 50, 150, 150, 300,  4, 2, 420 },
  { "Preset 4",  60, 40, 50, 10, 40,  90,  18,  72, 10, 5, 210 },
  { "Preset 5", 120, 90, 30, 20, 50, 135,  90, 225,  6, 3, 360 }
};

Preset daftarPreset[MAKS_PRESET];   // data kerja (dimuat dari NVS saat boot)
int jumlahPreset = 0;

// ================= MENU UTAMA =================
#define JUMLAH_MENU 4
const char* menuUtama[JUMLAH_MENU] = {
  "Pilih Preset",
  "Konfigurasi Preset",
  "Level Cairan",
  "Kalibrasi"
};

#define JUMLAH_KALIBRASI 10
const char* menuKalibrasi[JUMLAH_KALIBRASI] = {
  "Sensor Level",
  "Sensor Suhu",
  "Sensor Udara",
  "Motor HCL",
  "Motor H2O2",
  "Motor Air",
  "Motor Air Sabun",
  "Motor Limbah",
  "Motor Agitasi",
  "Exhaust Fan"
};

// ================= HALAMAN & STATE =================
enum Halaman {
  HAL_MENU_UTAMA,
  HAL_PRESET_LIST,
  HAL_PRESET_DETAIL,
  HAL_PROSES,
  HAL_PLACEHOLDER,
  HAL_CFG_LIST,
  HAL_CFG_AKSI,
  HAL_CFG_EDIT,
  HAL_LEVEL,            // uji touch sensor level (kalibrasi)
  HAL_LEVEL_CAIRAN,     // halaman Level Cairan (bar persentase)
  HAL_KALIBRASI,        // menu Kalibrasi (scroll)
  HAL_KALIBRASI_DETAIL  // halaman kalibrasi per item
};
const char* namaHalaman[] = {
  "MENU_UTAMA", "PRESET_LIST", "PRESET_DETAIL", "PROSES", "PLACEHOLDER",
  "CFG_LIST", "CFG_AKSI", "CFG_EDIT", "LEVEL", "LEVEL_CAIRAN",
  "KALIBRASI", "KALIBRASI_DETAIL"
};

Halaman halaman = HAL_MENU_UTAMA;
int presetTerpilih = 0;

// Dirty flag: layar hanya digambar ulang kalau true
bool perluGambar = true;

// State scroll Detail Preset
int presetDetailScroll = 0;
const int DETAIL_SCROLL_STEP = 5;
const int DETAIL_CONTENT_H   = 90;   // tinggi total konten (virtual)

// State Level Cairan (bar persentase)
int levelCairanScroll = 0;
const int LEVEL_CAIRAN_STEP    = 5;
const int LEVEL_CAIRAN_BLOCK_H = 14;
const int LEVEL_CAIRAN_CONTENT_H = JUMLAH_SENSOR * LEVEL_CAIRAN_BLOCK_H;

// State menu Kalibrasi
int kalibrasiItem = 0;

// State kalibrasi sensor suhu (DS18B20)
float kalibrasiSuhuC = 0.0;
bool  kalibrasiSuhuValid = false;

// State kalibrasi aktuator DC (TB6612)
bool kalibSabunOn   = false;
bool kalibLimbahOn  = false;
bool kalibAgitasiOn = false;
bool kalibFanOn     = false;

// State Konfigurasi Preset
enum DialogCfg { DLG_NONE, DLG_SIMPAN, DLG_HAPUS };
DialogCfg dialogCfg = DLG_NONE;
int    cfgIdx = 0;            // preset yang sedang di-edit/diaksi
Preset presetEdit;            // salinan kerja saat edit
bool   editBerubah = false;
int    editField = 0;
int    editScroll = 0;

void pindahHalaman(Halaman h) {
  dbg.print(F("[NAV] "));
  dbg.print(namaHalaman[halaman]);
  dbg.print(F(" -> "));
  dbg.println(namaHalaman[h]);
  halaman = h;
  perluGambar = true;
}
