/*
  Pilot: Level Cairan (uji touch) - pratinjau halaman "Level Cairan"
  Non-Contact Water Level Sensor via Nickel Strip (5 kanal)

  Pin Mapping (dari config.h):
    GPIO11 = HCl       GPIO12 = H2O2      GPIO13 = Air (H2O)
    GPIO14 = Air Sabun GPIO3  = Limbah

  Tampilan: OLED SSD1306 128x64 (I2C SDA4/SCL5, 0x3C) + 6 tombol PCF8574 (0x20),
  sama seperti firmware: nama kiri, persen kanan, bar progres di bawahnya.

  Cara pakai:
    1. Upload sketch ini
    2. Buka Serial Monitor 115200 -> nilai raw & baseline per sensor (untuk tuning)
    3. Wadah KERING saat boot (baseline dicari dari sampel awal)
    4. ATAS/BAWAH pilih bar, KIRI = kalibrasi ulang (wadah kosong), ENTER = reset
       hitungan sentuh, BACK = toggle tampilan persen/raw

  Catatan: logika baseline/ambang disalin dari sensor.h agar hasilnya sama
  dengan firmware utama.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= KONFIGURASI (salinan config.h) =================
#define SDA_PIN   4
#define SCL_PIN   5
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define UI_Y_OFFSET   15          // baris atas panel rusak; 0 bila OLED normal

#define PCF8574_ADDR 0x20
#define JUMLAH_TOMBOL 6
#define DEBOUNCE_MS   40
#define TIDAK_ADA     (-1)

#define P_ATAS 0
#define P_BAWAH 1
#define P_KIRI 3
#define P_KANAN 2
#define P_ENTER 4
#define P_BACK 5

#define JUMLAH_SENSOR 5
const int PIN_SENSOR[JUMLAH_SENSOR] = { 11, 12, 13, 14, 3 };
const char* NAMA_SENSOR[JUMLAH_SENSOR] = { "HCl", "H2O2", "Air", "Sabun", "Limbah" };

#define INTERVAL_SENSOR_MS  50
#define TOUCH_DEBOUNCE      3
#define TOUCH_DELTA_PERSEN  6
#define TOUCH_DELTA_MIN     300
#define TOUCH_ADAPT_CEPAT   8
#define TOUCH_ADAPT_LAMBAT  512
#define TOUTH_KALIB_SAMPEL  40

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= STATE SENSOR =================
int  raw[JUMLAH_SENSOR];
int  baseline[JUMLAH_SENSOR];
int  delta[JUMLAH_SENSOR];
bool aktif[JUMLAH_SENSOR];
int  cnt[JUMLAH_SENSOR];
unsigned long sentuh[JUMLAH_SENSOR];
bool kalibrasi = true;
unsigned long sensorTerakhirMs = 0;
int  sel = 0;
bool tampilRaw = false;

int levelPersen(int i) {
  // TODO: sensor hanya touch ON/OFF -> 100% saat terdeteksi, 0% saat tidak
  if (i < 0 || i >= JUMLAH_SENSOR) return 0;
  return aktif[i] ? 100 : 0;
}

// ================= STATE TOMBOL =================
const char* NAMA_TOMBOL[JUMLAH_TOMBOL];
bool stabil[JUMLAH_TOMBOL], dibaca[JUMLAH_TOMBOL];
unsigned long waktuBerubah[JUMLAH_TOMBOL];
int tombolAktif = TIDAK_ADA;

bool bacaPCF8574(uint8_t &data) {
  Wire.requestFrom(PCF8574_ADDR, 1);
  if (Wire.available()) { data = Wire.read(); return true; }
  return false;
}

void inputInit() {
  NAMA_TOMBOL[P_ATAS] = "ATAS";  NAMA_TOMBOL[P_BAWAH] = "BAWAH";
  NAMA_TOMBOL[P_KIRI] = "KIRI";  NAMA_TOMBOL[P_KANAN] = "KANAN";
  NAMA_TOMBOL[P_ENTER] = "ENTER"; NAMA_TOMBOL[P_BACK] = "BACK";
  uint8_t data = 0xFF;
  bacaPCF8574(data);
  for (int i = 0; i < JUMLAH_TOMBOL; i++) {
    bool t = !bitRead(data, i);
    stabil[i] = t; dibaca[i] = t; waktuBerubah[i] = millis();
  }
}

int bacaTombolBaru() {
  uint8_t data;
  if (!bacaPCF8574(data)) return TIDAK_ADA;
  int hasil = TIDAK_ADA;
  unsigned long skrg = millis();
  for (int i = 0; i < JUMLAH_TOMBOL; i++) {
    bool mentah = !bitRead(data, i);
    if (mentah != dibaca[i]) { waktuBerubah[i] = skrg; dibaca[i] = mentah; }
    if ((skrg - waktuBerubah[i]) > DEBOUNCE_MS && mentah != stabil[i]) {
      stabil[i] = mentah;
      if (stabil[i]) {
        if (tombolAktif == TIDAK_ADA) { tombolAktif = i; hasil = i; }
      } else if (tombolAktif == i) tombolAktif = TIDAK_ADA;
    }
  }
  return hasil;
}

// ================= LOGIKA SENSOR (dari sensor.h) =================
int sensorBacaRaw(int i) { return touchRead(PIN_SENSOR[i]); }

void sensorAdaptBaseline(int i, int v) {
  if (kalibrasi) {
    if (baseline[i] == 0) baseline[i] = v;
    else if (v < baseline[i]) baseline[i] = v;   // cari nilai saat kering
    return;
  }
  long d = (long)v - baseline[i];
  int ambangAbs = (int)max((long)TOUCH_DELTA_MIN, (long)baseline[i] * TOUCH_DELTA_PERSEN / 100);
  int k = (abs(d) > ambangAbs) ? TOUCH_ADAPT_LAMBAT : TOUCH_ADAPT_CEPAT;
  if (k < 2) k = 2;
  baseline[i] = (int)(baseline[i] + d / k);
}

void sensorUpdate() {
  unsigned long now = millis();
  if (now - sensorTerakhirMs < INTERVAL_SENSOR_MS) return;
  sensorTerakhirMs = now;

  static int sisaKalib = TOUTH_KALIB_SAMPEL;
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    int v = sensorBacaRaw(i);
    raw[i] = v;
    sensorAdaptBaseline(i, v);
    int ambangAbs = (int)max((long)TOUCH_DELTA_MIN, (long)baseline[i] * TOUCH_DELTA_PERSEN / 100);
    delta[i] = v - baseline[i];
    bool on = !kalibrasi && abs(delta[i]) >= ambangAbs;

    if (on != aktif[i]) {
      if (++cnt[i] >= TOUCH_DEBOUNCE) {
        aktif[i] = on;
        cnt[i] = 0;
        if (on) sentuh[i]++;
      }
    } else {
      cnt[i] = 0;
    }
  }
  if (kalibrasi && --sisaKalib <= 0) kalibrasi = false;
}

// Kalibrasi ulang: wadah harus kosong/kering
void kalibrasiUlang() {
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    baseline[i] = 0; aktif[i] = false; cnt[i] = 0;
  }
  kalibrasi = true;
}

// ================= UI (dari ui.h) =================
const int AREA_BAWAH = SCREEN_HEIGHT - UI_Y_OFFSET;
const int HEADER_H = 12;
const int BLOCK_H = 14;

void geserGambar(int dy) {
  if (dy <= 0) return;
  if (dy >= SCREEN_HEIGHT) { oled.clearDisplay(); return; }
  uint8_t* buf = oled.getBuffer();
  int hlm = SCREEN_HEIGHT / 8, geserHlm = dy / 8, geserBit = dy % 8;
  for (int p = hlm - 1; p >= 0; p--) {
    int s = p - geserHlm;
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      uint8_t v = 0;
      if (s >= 0) {
        v = (uint8_t)(buf[s * SCREEN_WIDTH + x] << geserBit);
        if (geserBit && s - 1 >= 0)
          v |= (uint8_t)(buf[(s - 1) * SCREEN_WIDTH + x] >> (8 - geserBit));
      }
      buf[p * SCREEN_WIDTH + x] = v;
    }
  }
}

void gambarHeader(const char* judul) {
  oled.fillRect(0, 0, SCREEN_WIDTH, HEADER_H, WHITE);
  oled.setTextColor(BLACK);
  oled.setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  oled.getTextBounds(judul, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((SCREEN_WIDTH - w) / 2, 2);
  oled.print(judul);
  oled.setTextColor(WHITE);
}

void teksKanan(const char* s, int xKanan, int y) {
  int16_t x1, y1; uint16_t w, h;
  oled.setTextSize(1);
  oled.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor(xKanan - w, y);
  oled.print(s);
}

bool terlihat(int y, int tinggi) {
  return (y + tinggi > HEADER_H) && (y < AREA_BAWAH);
}

void gambarLevel() {
  char judul[16];
  snprintf(judul, sizeof(judul), "%s", kalibrasi ? "Level Cairan (KAL)" : "Level Cairan");
  gambarHeader(judul);

  oled.setTextSize(1);
  int y = HEADER_H + 3;
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    if (terlihat(y, 7)) {
      bool s = (i == sel);
      if (s) oled.fillRect(0, y - 1, SCREEN_WIDTH, 7, WHITE);
      oled.setTextColor(s ? BLACK : WHITE);
      oled.setCursor(2, y);
      oled.print(NAMA_SENSOR[i]);

      char buf[12];
      if (tampilRaw) sprintf(buf, "r%d b%d", raw[i], baseline[i]);
      else           sprintf(buf, "%d%%", levelPersen(i));
      teksKanan(buf, SCREEN_WIDTH - 2, y);
    }
    oled.setTextColor(WHITE);

    int barY = y + 8, barH = 4, barW = SCREEN_WIDTH - 8;
    if (terlihat(barY, barH)) {
      oled.drawRect(4, barY, barW, barH, WHITE);
      int isi = barW * levelPersen(i) / 100;
      if (isi > 0) oled.fillRect(4, barY, isi, barH, WHITE);
    }
    y += BLOCK_H;
  }
  oled.setTextSize(1);
  oled.setCursor(0, AREA_BAWAH - 8);
  oled.print(F("[<>] pilih [L] kalib"));
}

void gambarLayar() {
  oled.clearDisplay();
  gambarLevel();
  geserGambar(UI_Y_OFFSET);
  oled.display();
}

// ================= LOG SERIAL (tuning) =================
unsigned long logMs = 0;
void logPeriodik() {
  unsigned long now = millis();
  if (now - logMs < 1000) return;
  logMs = now;
  Serial.print(F("[RAW]"));
  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    Serial.print(' '); Serial.print(NAMA_SENSOR[i]);
    Serial.print('='); Serial.print(raw[i]);
    Serial.print('/'); Serial.print(baseline[i]);
    Serial.print(aktif[i] ? "(ON)" : "(off)");
  }
  Serial.println();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("=== Pilot Level Cairan (touch) ==="));
  Serial.println(F("Format: nama=raw/baseline(ON|off)"));

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  uint8_t cek = 0xFF;
  if (!bacaPCF8574(cek))
    Serial.println(F("!! PCF8574 tidak terdeteksi (tombol mati)"));
  inputInit();

  pinMode(0, OUTPUT);                            // LED onboard sebagai indikator hidup
  digitalWrite(0, HIGH);

  for (int i = 0; i < JUMLAH_SENSOR; i++) {
    pinMode(PIN_SENSOR[i], INPUT);
    raw[i] = baseline[i] = delta[i] = cnt[i] = 0;
    aktif[i] = false; sentuh[i] = 0;
  }
  kalibrasi = true;

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    Serial.println(F("!! OLED tidak terdeteksi di 0x3C"));
  else {
    oled.setRotation(0);
    gambarLayar();
  }
  delay(2000);   // biarkan baseline terkumpul saat wadah kosong
}

// ================= LOOP =================
void loop() {
  int t = bacaTombolBaru();
  if (t != TIDAK_ADA) {
    if (t == P_ATAS)  sel = max(0, sel - 1);
    if (t == P_BAWAH) sel = min(JUMLAH_SENSOR - 1, sel + 1);
    if (t == P_KIRI)  { kalibrasiUlang(); Serial.println(F("[KAL] Ulang (wadah kosong)")); }
    if (t == P_ENTER) { for (int i = 0; i < JUMLAH_SENSOR; i++) sentuh[i] = 0; Serial.println(F("[KAL] Counter direset")); }
    if (t == P_BACK)  tampilRaw = !tampilRaw;
    gambarLayar();
  }

  sensorUpdate();
  logPeriodik();
  delay(2);
}