#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include <ModbusMaster.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <INA226_WE.h>

// =====================================================
// KONFIGURASI THINGSPEAK
// =====================================================

const char* THINGSPEAK_WRITE_KEY =
    "VIV896ZGWAGL5GZ4";

const char* THINGSPEAK_WRITE_KEY_PENDUKUNG = "JGXI77JIORMKCSVI";

const char* THINGSPEAK_URL =
    "https://api.thingspeak.com/update";

// Diubah jadi 5 menit sekali (5 x 60 x 1000 = 300000 milidetik)
const unsigned long INTERVAL_THINGSPEAK = 300000; 
// Jeda channel kedua dibiarkan 5 detik agar tidak tabrakan dengan channel 1
const unsigned long INTERVAL_THINGSPEAK_PENDUKUNG = 5000;

unsigned long waktuThingSpeakTerakhir =
    0;
unsigned long waktuThingSpeakPendukungTerakhir =
    0;
// =====================================================
// IDENTITAS PERANGKAT
// =====================================================

const char* DEVICE_ID =
    "ESP32-S3-AGRI-01";

// =====================================================
// KONFIGURASI HOTSPOT HP
// =====================================================

const char* WIFI_SSID =
    "PKMMARGOREJO";

const char* WIFI_PASSWORD =
    "ELINSSATU";

// Port penerima di laptop
const uint16_t UDP_DESTINATION_PORT =
    4210;

// Port lokal ESP32
const uint16_t UDP_LOCAL_PORT =
    4211;

// true = memakai broadcast
const bool UDP_USE_BROADCAST =
    true;

// Digunakan jika broadcast dimatikan
IPAddress UDP_LAPTOP_IP(
    192,
    168,
    1,
    100
);

// Interval pengiriman UDP
const unsigned long INTERVAL_UDP =
    1000;


/*
  PERBAIKAN:
  Interval reconnect diperbesar ke 10 detik agar
  tidak mengganggu proses reconnect yang sedang berjalan.
  Sebelumnya 1000 ms terlalu cepat.
*/
const unsigned long INTERVAL_WIFI_RECONNECT =
    10000;

// =====================================================
// KONFIGURASI GPIO ESP32-S3
// =====================================================

// LDR
#define LDR_PIN 4

// Relay
#define RELAY_AKTUATOR_PIN 5
#define RELAY_LAMPU_PIN 15
#define RELAY_KIPAS_PIN 39

// DHT22
#define DHT_LUAR_1_PIN 6
#define DHT_DALAM_PIN 7
#define DHT_LUAR_2_PIN 16

// RTC DS3231
#define SDA_RTC 8
#define SCL_RTC 9

// Sensor kelembapan tanah
#define SOIL_1_PIN 10
#define SOIL_2_PIN 11
#define SOIL_3_PIN 12

// MAX485
#define RS485_DIR_PIN 14
#define RS485_TX_PIN 17
#define RS485_RX_PIN 18

// =====================================================
// JENIS SENSOR DHT
// =====================================================

#define DHT_TYPE DHT22

// =====================================================
// PENGATURAN RTC
// =====================================================

#define SET_RTC_ONCE false

// =====================================================
// LOGIKA FAN
// =====================================================

/*
  Logika Fan:
  - Menyala saat suhu panel >= 34 C selama 5 menit, kemudian mati.
  - Menyala kembali saat suhu menyentuh 34 C lagi (setelah sempat turun di bawah 34 C).
*/
const float SUHU_KIPAS_ON =
    34.0;

// Durasi kipas menyala: 5 menit (5 x 60 x 1000 = 300000 ms)
const unsigned long DURASI_KIPAS_NYALA =
    300000;

// =====================================================
// JADWAL LAMPU
// =====================================================

const int JAM_LAMPU_ON =
    18;

const int JAM_LAMPU_OFF =
    6;

// =====================================================
// JADWAL VALVE (RTC)
// =====================================================

// Pagi: jam 7 tepat (07:00)
const int JAM_VALVE_PAGI =
    7;

const int MENIT_VALVE_PAGI =
    0;

// Sore: jam 4 lewat 45 menit (16:45)
const int JAM_VALVE_SORE =
    16;

const int MENIT_VALVE_SORE =
    45;

// Durasi valve menyala: 15 menit
const int MENIT_DURASI_VALVE =
    15;

// Proteksi batas maksimum valve terbuka sebagai cadangan fail-safe (20 menit)
const unsigned long MAKSIMUM_WAKTU_VALVE =
    20UL * 60UL * 1000UL;

// Jeda valve 1 menit
const unsigned long JEDA_MINIMUM_VALVE =
    60UL * 1000UL;

// =====================================================
// KALIBRASI SENSOR KELEMBAPAN TANAH
// =====================================================

/*
  KALIBRASI PER-SENSOR

  Setiap sensor kapasitif memiliki karakteristik
  ADC yang berbeda-beda. Oleh karena itu kalibrasi
  dilakukan secara individual.

  Hasil pengukuran nyata:

  Sensor 1 (GPIO10):
    Di udara       = 3639
    Di tanah kering = 3495 sampai 3524

  Sensor 2 (GPIO11):
    Di udara       = 4095
    Di tanah kering = 3911 sampai 3964

  Sensor 3 (GPIO12):
    Di udara       = 4095
    Di tanah kering = 3959 sampai 3977

  nilaiKering diatur sedikit di atas pembacaan
  tanah kering tertinggi. Nilai di atas nilaiKering
  akan dipetakan ke 0 persen dan difilter.

  nilaiBasah belum dikalibrasi basah, untuk
  sementara menggunakan estimasi 1250.
  Perbarui setelah pengujian di tanah basah.
*/

const int nilaiKering[3] = {
    3550,
    3550,
    3550
};

const int nilaiBasah[3] = {
    1250,
    1250,
    1250
};

/*
  Nilai yang terlalu dekat 0 atau 4095
  dianggap pembacaan tidak valid.
*/
bool pendukungMenunggu =
    false;

const int ADC_MIN_VALID =
    20;

const int ADC_MAX_VALID =
    4075;

/*
  Batas minimum persentase kelembapan.

  Jika hasil konversi <= 1 persen, sensor dianggap
  tidak valid (kemungkinan tidak terhubung dengan
  baik atau berada di udara terbuka).

  Sensor seperti ini tidak ikut dalam voting
  untuk membuka atau menutup valve.
*/

const float BATAS_PERSEN_MINIMUM =
    1.0;

/*
  PERBAIKAN:
  ESP32-S3 TIDAK memiliki konflik ADC2 vs WiFi
  seperti ESP32 klasik. Oleh karena itu WiFi
  TIDAK PERLU dihentikan saat membaca sensor tanah.

  Variabel HENTIKAN_WIFI_SAAT_BACA_SOIL dan fungsi
  hentikanWifiUntukSoil / aktifkanWifiSetelahSoil
  telah dihapus karena justru menyebabkan WiFi
  putus-sambung terus-menerus.
*/

/*
  Pembacaan tanah setiap 10 detik.
*/

const unsigned long INTERVAL_SOIL =
    10000;

// =====================================================
// KONFIGURASI MODBUS NPK
// =====================================================

const uint32_t NPK_BAUD_RATE =
    4800;

const uint8_t NPK_SLAVE_ID =
    1;

const uint16_t NPK_START_REGISTER =
    0x0000;

const uint8_t NPK_REGISTER_COUNT =
    7;

// false = function 03
// true  = function 04
const bool NPK_INPUT_REGISTER =
    false;

// =====================================================
// KONFIGURASI SENSOR DAYA INA226 (2 MODUL)
// =====================================================

/*
  Kedua modul INA226 menggunakan jalur I2C yang sama
  dengan RTC DS3231 (Pin 8 dan 9).

  Modul 1 (SCC ke Baterai):
    Pad A0 TIDAK disolder -> Alamat 0x44

  Modul 2 (SCC ke Load):
    Pad A0 DISOLDER ke VCC -> Alamat 0x45

  CATATAN KALIBRASI:
  - Nilai R_SHUNT, I_MAX, dan VOLT_KOREKSI masih
    sementara. Update setelah R010 (0.01 Ohm) datang
    dan dikalibrasi.
  - Jika R010 ditumpuk di atas R100 bawaan (paralel):
      R_SHUNT = 0.0091, I_MAX = 9.0
  - Jika R100 dicabut dan diganti R010:
      R_SHUNT = 0.01, I_MAX = 8.19
*/

#define ALAMAT_INA_BATERAI 0x44
#define ALAMAT_INA_LOAD    0x45

// --- Kalibrasi INA226 Modul 1 (SCC ke Baterai) ---
const float R_SHUNT_BATERAI =
    0.01;

const float I_MAX_BATERAI =
    8.19;

const float VOLT_KOREKSI_BATERAI =
    0.9464;

// --- Kalibrasi INA226 Modul 2 (SCC ke Load) ---
const float R_SHUNT_LOAD =
    0.01;

const float I_MAX_LOAD =
    8.19;

const float VOLT_KOREKSI_LOAD =
    0.9454;

// =====================================================
// INTERVAL SENSOR
// =====================================================

const unsigned long INTERVAL_DHT_DALAM =
    1000;

const unsigned long INTERVAL_RTC =
    1000;

const unsigned long INTERVAL_DHT_LUAR =
    1000;

const unsigned long INTERVAL_LDR =
    1000;

const unsigned long INTERVAL_NPK =
    1000;

const unsigned long INTERVAL_TAMPIL =
    1000;

const unsigned long INTERVAL_INA226 =
    1000;

// =====================================================
// OBJEK SENSOR
// =====================================================

RTC_DS3231 rtc;

DHT dhtLuar1(
    DHT_LUAR_1_PIN,
    DHT_TYPE
);

DHT dhtDalam(
    DHT_DALAM_PIN,
    DHT_TYPE
);

DHT dhtLuar2(
    DHT_LUAR_2_PIN,
    DHT_TYPE
);

HardwareSerial rs485Serial(1);

ModbusMaster npkNode;

WiFiUDP udp;

INA226_WE inaBaterai =
    INA226_WE(ALAMAT_INA_BATERAI);

INA226_WE inaLoad =
    INA226_WE(ALAMAT_INA_LOAD);

// =====================================================
// STATUS SISTEM
// =====================================================

bool rtcTerdeteksi =
    false;

bool udpSiap =
    false;

IPAddress alamatBroadcast;

// Status relay
bool kipasNyala =
    false;

// Waktu dan siklus kipas
unsigned long waktuKipasMulai =
    0;

bool kipasSelesaiSiklus =
    false;

bool lampuNyala =
    false;

bool valveTerbuka =
    false;

bool valveTimeout =
    false;

// Waktu valve
unsigned long waktuValveMulai =
    0;

unsigned long waktuValveBerhenti =
    0;

// =====================================================
// DATA DHT
// =====================================================

float suhuDalam =
    NAN;

float lembapDalam =
    NAN;

float suhuLuar1 =
    NAN;

float lembapLuar1 =
    NAN;

float suhuLuar2 =
    NAN;

float lembapLuar2 =
    NAN;

float suhuLuarRata =
    NAN;

float lembapLuarRata =
    NAN;

// =====================================================
// DATA LDR
// =====================================================

int nilaiLDR =
    0;

// =====================================================
// DATA SENSOR KELEMBAPAN TANAH
// =====================================================

int nilaiSoil[3] = {
  0,
  0,
  0
};

/*
  Jumlah soil probe analog yang BENAR-BENAR terpasang.
  Probe di atas angka ini tidak dibaca dan selalu
  ditandai tidak valid, sehingga pin ADC yang
  menggantung tidak menghasilkan nilai mengambang.

  Node 1 = 2 probe, Node 2 = 0 probe.
*/

const uint8_t JUMLAH_SOIL_AKTIF = 2;

float persenSoil[3] = {
  -1.0,
  -1.0,
  -1.0
};

bool soilValid[3] = {
  false,
  false,
  false
};

float rataRataKelembapanTanah =
    -1.0;

// =====================================================
// DATA SENSOR DAYA INA226
// =====================================================

bool inaBateraiValid =
    false;

float tegangan_baterai =
    0.0;

float arus_baterai =
    0.0;

float daya_baterai =
    0.0;

bool inaLoadValid =
    false;

float tegangan_load =
    0.0;

float arus_load =
    0.0;

float daya_load =
    0.0;

// =====================================================
// DATA NPK
// =====================================================

struct DataNPK {
  float kelembapan =
      NAN;

  float suhu =
      NAN;

  uint16_t ec =
      0;

  float ph =
      NAN;

  uint16_t nitrogen =
      0;

  uint16_t phosphorus =
      0;

  uint16_t potassium =
      0;

  bool valid =
      false;

  uint8_t kodeModbus =
      0xFF;
};

DataNPK dataNpk;

// =====================================================
// TIMER
// =====================================================

unsigned long waktuDhtDalamTerakhir =
    0;

unsigned long waktuRtcTerakhir =
    0;

unsigned long waktuDhtLuarTerakhir =
    0;

unsigned long waktuLdrTerakhir =
    0;

unsigned long waktuSoilTerakhir =
    0;

unsigned long waktuNpkTerakhir =
    0;

unsigned long waktuTampilTerakhir =
    0;

unsigned long waktuUdpTerakhir =
    0;

unsigned long waktuWifiTerakhir =
    0;

unsigned long waktuInaTerakhir =
    0;

uint32_t nomorPaketUdp =
    0;

// =====================================================
// FUNGSI DUA DIGIT
// =====================================================

void print2Digits(int angka) {
  if (angka < 10) {
    Serial.print("0");
  }

  Serial.print(angka);
}

// =====================================================
// KONDISI CAHAYA
// =====================================================

const char* kondisiCahaya(
    int nilai
) {
  if (nilai < 800) {
    return "Sangat Terang";
  }
  else if (nilai < 1600) {
    return "Terang";
  }
  else if (nilai < 2600) {
    return "Cukup Terang";
  }
  else if (nilai < 3400) {
    return "Gelap";
  }

  return "Sangat Gelap";
}

// =====================================================
// KONTROL RELAY ACTIVE HIGH
// =====================================================

void setRelay(
    uint8_t pinRelay,
    bool status
) {
  digitalWrite(
      pinRelay,
      status ? HIGH : LOW
  );
}

void setKipas(bool status) {
  kipasNyala =
      status;

  setRelay(
      RELAY_KIPAS_PIN,
      status
  );
}

void setLampu(bool status) {
  lampuNyala =
      status;

  setRelay(
      RELAY_LAMPU_PIN,
      status
  );
}

void setValve(bool status) {
  valveTerbuka =
      status;

  setRelay(
      RELAY_AKTUATOR_PIN,
      status
  );
}

// =====================================================
// JADWAL LAMPU
// =====================================================

bool jadwalLampuNyala(
    int jamSekarang
) {
  return (
      jamSekarang >=
      JAM_LAMPU_ON ||
      jamSekarang <
      JAM_LAMPU_OFF
  );
}

// =====================================================
// JADWAL VALVE
// =====================================================

bool jadwalValveNyala(
    int jamSekarang,
    int menitSekarang
) {
  // Pagi: 07:00 - 07:15
  if (
      jamSekarang == JAM_VALVE_PAGI &&
      menitSekarang >= MENIT_VALVE_PAGI &&
      menitSekarang < MENIT_VALVE_PAGI + MENIT_DURASI_VALVE
  ) {
    return true;
  }

  // Sore: 16:30 - 16:45
  if (
      jamSekarang == JAM_VALVE_SORE &&
      menitSekarang >= MENIT_VALVE_SORE &&
      menitSekarang < MENIT_VALVE_SORE + MENIT_DURASI_VALVE
  ) {
    return true;
  }

  return false;
}

// =====================================================
// PEMBACAAN ADC LDR
// =====================================================

int bacaAdcRataRata(
    uint8_t pin,
    uint8_t jumlahSampel = 16
) {
  uint32_t total =
      0;

  for (
      uint8_t i = 0;
      i < jumlahSampel;
      i++
  ) {
    total +=
        analogRead(pin);

    delayMicroseconds(250);
  }

  return static_cast<int>(
      total /
      jumlahSampel
  );
}

// =====================================================
// PEMBACAAN ADC SOIL STABIL
// =====================================================

int bacaAdcSoilStabil(
    uint8_t pin
) {
  const uint8_t JUMLAH_SAMPEL =
      15;

  uint16_t sampel[
      JUMLAH_SAMPEL
  ];

  /*
    Buang pembacaan pertama setelah
    berpindah kanal ADC.
  */

  for (
      uint8_t i = 0;
      i < 3;
      i++
  ) {
    analogRead(pin);

    delayMicroseconds(500);
  }

  /*
    Ambil 15 sampel.
  */

  for (
      uint8_t i = 0;
      i < JUMLAH_SAMPEL;
      i++
  ) {
    sampel[i] =
        analogRead(pin);

    delayMicroseconds(700);
  }

  /*
    Urutkan data.
  */

  for (
      uint8_t i = 1;
      i < JUMLAH_SAMPEL;
      i++
  ) {
    uint16_t nilaiSementara =
        sampel[i];

    int8_t j =
        i - 1;

    while (
        j >= 0 &&
        sampel[j] >
        nilaiSementara
    ) {
      sampel[j + 1] =
          sampel[j];

      j--;
    }

    sampel[j + 1] =
        nilaiSementara;
  }

  /*
    Gunakan nilai median.
  */

  return sampel[
      JUMLAH_SAMPEL / 2
  ];
}

// =====================================================
// KONVERSI ADC KE PERSENTASE
// =====================================================

float konversiKePersen(
    int nilaiAnalog,
    uint8_t indeksSensor
) {
  int nilaiDibatasi =
      constrain(
          nilaiAnalog,
          nilaiBasah[indeksSensor],
          nilaiKering[indeksSensor]
      );

  /*
    Rumus mapping per sensor:

    nilaiKering[i] = 0 persen
    nilaiBasah[i]  = 100 persen
  */

  float persen =
      (
        static_cast<float>(
            nilaiKering[indeksSensor] -
            nilaiDibatasi
        ) *
        100.0
      ) /
      static_cast<float>(
          nilaiKering[indeksSensor] -
          nilaiBasah[indeksSensor]
      );

  return constrain(
      persen,
      0.0,
      100.0
  );
}

bool nilaiSoilValid(
    int nilai
) {
  return (
      nilai >
      ADC_MIN_VALID &&
      nilai <
      ADC_MAX_VALID
  );
}

// =====================================================
// BACA SENSOR KELEMBAPAN TANAH
// =====================================================
void bacaSensorKelembapan() {
  // Hanya baca probe 1 (GPIO10) dan probe 2 (GPIO11)
  nilaiSoil[0] = bacaAdcSoilStabil(SOIL_1_PIN);
  delayMicroseconds(1000);
  nilaiSoil[1] = bacaAdcSoilStabil(SOIL_2_PIN);
  soilValid[0] = nilaiSoilValid(nilaiSoil[0]);
  soilValid[1] = nilaiSoilValid(nilaiSoil[1]);
  float totalPersen = 0.0;
  uint8_t jumlahValid = 0;
  // 1. Cek Soil 1
  if (soilValid[0]) {
    persenSoil[0] = konversiKePersen(nilaiSoil[0], 0);
    if (persenSoil[0] <= BATAS_PERSEN_MINIMUM) {
      soilValid[0] = false;
      persenSoil[0] = -1.0;
    } else {
      totalPersen += persenSoil[0];
      jumlahValid++;
    }
  } else {
    persenSoil[0] = -1.0;
  }
  // 2. Cek Soil 2
  if (soilValid[1]) {
    persenSoil[1] = konversiKePersen(nilaiSoil[1], 1);
    if (persenSoil[1] <= BATAS_PERSEN_MINIMUM) {
      soilValid[1] = false;
      persenSoil[1] = -1.0;
    } else {
      totalPersen += persenSoil[1];
      jumlahValid++;
    }
  } else {
    persenSoil[1] = -1.0;
  }
  // 3. Gabungkan Kelembapan dari Sensor NPK (jika valid)
  if (dataNpk.valid && !isnan(dataNpk.kelembapan) && dataNpk.kelembapan >= 0.0 && dataNpk.kelembapan <= 100.0) {
    totalPersen += dataNpk.kelembapan;
    jumlahValid++;
  }
  // Hitung rata-rata dari sensor yang valid (Soil 1 + Soil 2 + NPK)
  if (jumlahValid > 0) {
    rataRataKelembapanTanah = totalPersen / static_cast<float>(jumlahValid);
  } else {
    rataRataKelembapanTanah = -1.0;
  }
}
// =====================================================
// KONTROL VALVE (BERDASARKAN JADWAL RTC)
// =====================================================

void kontrolValve(unsigned long sekarang) {
  if (!rtcTerdeteksi) {
    if (valveTerbuka) {
      setValve(false);
      waktuValveBerhenti = sekarang;
    }
    return;
  }

  DateTime now = rtc.now();
  bool harusBuka = jadwalValveNyala(now.hour(), now.minute());

  if (harusBuka) {
    if (!valveTerbuka && !valveTimeout) {
      setValve(true);
      waktuValveMulai = sekarang;
    }
  } else {
    if (valveTerbuka) {
      setValve(false);
      waktuValveBerhenti = sekarang;
    }
    valveTimeout = false; // Reset proteksi timeout jika sudah di luar jadwal
  }
}

// =====================================================
// KONTROL ARAH MAX485
// =====================================================

void rs485PreTransmission() {
  digitalWrite(
      RS485_DIR_PIN,
      HIGH
  );

  delayMicroseconds(150);
}

void rs485PostTransmission() {
  rs485Serial.flush();

  delayMicroseconds(150);

  digitalWrite(
      RS485_DIR_PIN,
      LOW
  );
}

// =====================================================
// BACA SENSOR NPK
// =====================================================

void bacaSensorNpk() {
  uint8_t hasil;

  if (NPK_INPUT_REGISTER) {
    hasil =
        npkNode.readInputRegisters(
            NPK_START_REGISTER,
            NPK_REGISTER_COUNT
        );
  }
  else {
    hasil =
        npkNode.readHoldingRegisters(
            NPK_START_REGISTER,
            NPK_REGISTER_COUNT
        );
  }

  dataNpk.kodeModbus =
      hasil;

  if (
      hasil !=
      ModbusMaster::ku8MBSuccess
  ) {
    dataNpk.valid =
        false;

    return;
  }

  uint16_t rawKelembapan =
      npkNode.getResponseBuffer(0);

  int16_t rawSuhu =
      static_cast<int16_t>(
          npkNode.getResponseBuffer(1)
      );

  uint16_t rawEc =
      npkNode.getResponseBuffer(2);

  uint16_t rawPh =
      npkNode.getResponseBuffer(3);

  uint16_t rawNitrogen =
      npkNode.getResponseBuffer(4);

  uint16_t rawPhosphorus =
      npkNode.getResponseBuffer(5);

  uint16_t rawPotassium =
      npkNode.getResponseBuffer(6);

  dataNpk.kelembapan =
      rawKelembapan /
      10.0;

  dataNpk.suhu =
      rawSuhu /
      10.0;

  dataNpk.ec =
      rawEc;

  dataNpk.ph =
      rawPh /
      10.0;

  dataNpk.nitrogen =
      rawNitrogen;

  dataNpk.phosphorus =
      rawPhosphorus;

  dataNpk.potassium =
      rawPotassium;

  dataNpk.valid =
      true;
}

// =====================================================
// BACA SENSOR DAYA INA226 (2 MODUL)
// =====================================================

void bacaSensorDaya() {
  // --- Baca Modul 1: SCC ke Baterai ---
if (inaBateraiValid) {
    float rawV =
        inaBaterai.getBusVoltage_V();

    float rawI =
        inaBaterai.getCurrent_mA();

    tegangan_baterai =
        rawV *
        VOLT_KOREKSI_BATERAI;

    arus_baterai =
        -(rawI /      // <-- tambah tanda minus di sini
        1000.0);

    daya_baterai =
        tegangan_baterai *
        arus_baterai;
}

  // --- Baca Modul 2: SCC ke Load ---
  if (inaLoadValid) {
    float rawV =
        inaLoad.getBusVoltage_V();

    float rawI =
        inaLoad.getCurrent_mA();

    tegangan_load =
        rawV *
        VOLT_KOREKSI_LOAD;

    arus_load =
        rawI /
        1000.0;

    daya_load =
        tegangan_load *
        arus_load;
  }
}

// =====================================================
// ALAMAT BROADCAST
// =====================================================

IPAddress hitungAlamatBroadcast() {
  IPAddress ip =
      WiFi.localIP();

  IPAddress subnet =
      WiFi.subnetMask();

  IPAddress broadcast;

  for (
      uint8_t i = 0;
      i < 4;
      i++
  ) {
    broadcast[i] =
        ip[i] |
        static_cast<uint8_t>(
            ~subnet[i]
        );
  }

  return broadcast;
}

// =====================================================
// MEMULAI UDP
// =====================================================

void mulaiUdp() {
  udp.stop();

  udpSiap =
      udp.begin(
          UDP_LOCAL_PORT
      );

  if (udpSiap) {
    alamatBroadcast =
        hitungAlamatBroadcast();

    Serial.println(
        "UDP berhasil dimulai."
    );

    Serial.print(
        "Alamat broadcast: "
    );

    Serial.println(
        alamatBroadcast
    );

    Serial.print(
        "Port tujuan: "
    );

    Serial.println(
        UDP_DESTINATION_PORT
    );
  }
  else {
    Serial.println(
        "UDP gagal dimulai."
    );
  }
}

// =====================================================
// MULAI WIFI
// =====================================================

void mulaiWifi() {
  Serial.println();

  Serial.print(
      "Menghubungkan ke hotspot HP: "
  );

  Serial.println(
      WIFI_SSID
  );

  WiFi.mode(
      WIFI_STA
  );

  WiFi.setSleep(
      false
  );

  /*
    PERBAIKAN:
    Aktifkan auto-reconnect bawaan ESP32 agar
    driver WiFi menangani reconnect secara otomatis.
  */
  WiFi.setAutoReconnect(
      true
  );

  WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD
  );

  unsigned long waktuMulai =
      millis();

  while (
      WiFi.status() !=
      WL_CONNECTED &&
      millis() -
      waktuMulai <
      20000
  ) {
    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (
      WiFi.status() ==
      WL_CONNECTED
  ) {
    Serial.println(
        "Hotspot HP berhasil terhubung."
    );

    Serial.print(
        "IP ESP32: "
    );

    Serial.println(
        WiFi.localIP()
    );

    Serial.print(
        "Subnet mask: "
    );

    Serial.println(
        WiFi.subnetMask()
    );

    Serial.print(
        "Gateway HP: "
    );

    Serial.println(
        WiFi.gatewayIP()
    );

    Serial.print(
        "Kekuatan Wi-Fi: "
    );

    Serial.print(
        WiFi.RSSI()
    );

    Serial.println(
        " dBm"
    );

    mulaiUdp();
  }
  else {
    udpSiap =
        false;

    Serial.println(
        "ESP32 belum berhasil terhubung."
    );

    Serial.println(
        "Sensor dan relay tetap bekerja secara lokal."
    );
  }
}

// =====================================================
// RECONNECT WIFI
// =====================================================

/*
  PERBAIKAN:
  1. WiFi.disconnect() dihapus karena membatalkan
     proses reconnect yang sedang berjalan.
  2. Interval diperbesar ke 10 detik.
  3. WiFi.setAutoReconnect(true) sudah menangani
     reconnect di level driver, fungsi ini hanya
     sebagai backup.
*/

void periksaWifi(
    unsigned long sekarang
) {
  if (
      WiFi.status() ==
      WL_CONNECTED
  ) {
    if (!udpSiap) {
      mulaiUdp();
    }

    return;
  }

  udpSiap =
      false;

  if (
      sekarang -
      waktuWifiTerakhir <
      INTERVAL_WIFI_RECONNECT
  ) {
    return;
  }

  waktuWifiTerakhir =
      sekarang;

  Serial.println(
      "Wi-Fi terputus, mencoba kembali..."
  );

  WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD
  );
}

// =====================================================
// FORMAT JSON
// =====================================================

String floatKeJson(
    float nilai,
    unsigned int desimal = 1
) {
  if (isnan(nilai)) {
    return "null";
  }

  return String(
      nilai,
      desimal
  );
}

const char* boolKeJson(
    bool nilai
) {
  return nilai ?
      "true" :
      "false";
}

// =====================================================
// MEMBUAT PAYLOAD UDP
// =====================================================

String buatPayloadUdp() {
  nomorPaketUdp++;

  String payload;

  payload.reserve(1600);

  char waktuRtc[24];

  if (rtcTerdeteksi) {
    DateTime now =
        rtc.now();

    snprintf(
        waktuRtc,
        sizeof(waktuRtc),
        "%04d-%02d-%02d %02d:%02d:%02d",
        now.year(),
        now.month(),
        now.day(),
        now.hour(),
        now.minute(),
        now.second()
    );
  }
  else {
    snprintf(
        waktuRtc,
        sizeof(waktuRtc),
        "RTC_ERROR"
    );
  }

  payload += "{";

  payload += "\"device_id\":\"";
  payload += DEVICE_ID;
  payload += "\",";

  payload += "\"seq\":";
  payload += String(nomorPaketUdp);
  payload += ",";

  payload += "\"rtc\":\"";
  payload += waktuRtc;
  payload += "\",";

  payload += "\"esp_ip\":\"";
  payload += WiFi.localIP().toString();
  payload += "\",";

  payload += "\"wifi_rssi\":";
  payload += String(WiFi.RSSI());
  payload += ",";

  payload += "\"suhu_luar1\":";
  payload += floatKeJson(suhuLuar1);
  payload += ",";

  payload += "\"lembap_luar1\":";
  payload += floatKeJson(lembapLuar1);
  payload += ",";

  payload += "\"suhu_luar2\":";
  payload += floatKeJson(suhuLuar2);
  payload += ",";

  payload += "\"lembap_luar2\":";
  payload += floatKeJson(lembapLuar2);
  payload += ",";

  payload += "\"suhu_luar_rata\":";
  payload += floatKeJson(suhuLuarRata);
  payload += ",";

  payload += "\"lembap_luar_rata\":";
  payload += floatKeJson(lembapLuarRata);
  payload += ",";

  payload += "\"suhu_dalam\":";
  payload += floatKeJson(suhuDalam);
  payload += ",";

  payload += "\"lembap_dalam\":";
  payload += floatKeJson(lembapDalam);
  payload += ",";

  payload += "\"ldr_raw\":";
  payload += String(nilaiLDR);
  payload += ",";

  payload += "\"cahaya\":\"";
  payload += kondisiCahaya(nilaiLDR);
  payload += "\",";

  payload += "\"soil1_raw\":";
  payload += String(nilaiSoil[0]);
  payload += ",";

  payload += "\"soil1_pct\":";

  payload += soilValid[0] ?
      String(persenSoil[0], 1) :
      "null";

  payload += ",";

  payload += "\"soil1_valid\":";
  payload += boolKeJson(soilValid[0]);
  payload += ",";

  payload += "\"soil2_raw\":";
  payload += String(nilaiSoil[1]);
  payload += ",";

  payload += "\"soil2_pct\":";

  payload += soilValid[1] ?
      String(persenSoil[1], 1) :
      "null";

  payload += ",";

  payload += "\"soil2_valid\":";
  payload += boolKeJson(soilValid[1]);
  payload += ",";

  payload += "\"soil3_raw\":";
  payload += String(nilaiSoil[2]);
  payload += ",";

  payload += "\"soil3_pct\":";

  payload += soilValid[2] ?
      String(persenSoil[2], 1) :
      "null";

  payload += ",";

  payload += "\"soil3_valid\":";
  payload += boolKeJson(soilValid[2]);
  payload += ",";

  payload += "\"npk_valid\":";
  payload += boolKeJson(dataNpk.valid);
  payload += ",";

  payload += "\"npk_error\":";
  payload += String(dataNpk.kodeModbus);
  payload += ",";

  payload += "\"npk_kelembapan\":";

  payload += dataNpk.valid ?
      String(dataNpk.kelembapan, 1) :
      "null";

  payload += ",";

  payload += "\"npk_suhu\":";

  payload += dataNpk.valid ?
      String(dataNpk.suhu, 1) :
      "null";

  payload += ",";

  payload += "\"npk_ec\":";

  payload += dataNpk.valid ?
      String(dataNpk.ec) :
      "null";

  payload += ",";

  payload += "\"npk_ph\":";

  payload += dataNpk.valid ?
      String(dataNpk.ph, 1) :
      "null";

  payload += ",";

  payload += "\"npk_n\":";

  payload += dataNpk.valid ?
      String(dataNpk.nitrogen) :
      "null";

  payload += ",";

  payload += "\"npk_p\":";

  payload += dataNpk.valid ?
      String(dataNpk.phosphorus) :
      "null";

  payload += ",";

  payload += "\"npk_k\":";

  payload += dataNpk.valid ?
      String(dataNpk.potassium) :
      "null";

  payload += ",";

  payload += "\"kipas\":";
  payload += boolKeJson(kipasNyala);
  payload += ",";

  payload += "\"lampu\":";
  payload += boolKeJson(lampuNyala);
  payload += ",";

  payload += "\"valve\":";
  payload += boolKeJson(valveTerbuka);
  payload += ",";

  payload += "\"valve_timeout\":";
  payload += boolKeJson(valveTimeout);
  payload += ",";

  // === DATA DAYA INA226 ===

  payload += "\"tegangan_baterai\":";

  payload += inaBateraiValid ?
      floatKeJson(tegangan_baterai, 2) :
      "null";

  payload += ",";

  payload += "\"arus_baterai\":";

  payload += inaBateraiValid ?
      floatKeJson(arus_baterai, 3) :
      "null";

  payload += ",";

  payload += "\"daya_baterai\":";

  payload += inaBateraiValid ?
      floatKeJson(daya_baterai, 2) :
      "null";

  payload += ",";

  payload += "\"tegangan_load\":";

  payload += inaLoadValid ?
      floatKeJson(tegangan_load, 2) :
      "null";

  payload += ",";

  payload += "\"arus_load\":";

  payload += inaLoadValid ?
      floatKeJson(arus_load, 3) :
      "null";

  payload += ",";

  payload += "\"daya_load\":";

  payload += inaLoadValid ?
      floatKeJson(daya_load, 2) :
      "null";

  // =======================

  payload += "}";

  return payload;
}

// =====================================================
// KIRIM DATA UDP
// =====================================================

void kirimDataUdp() {
  if (
      WiFi.status() !=
      WL_CONNECTED
  ) {
    return;
  }

  if (!udpSiap) {
    return;
  }

  String payload =
      buatPayloadUdp();

  IPAddress alamatTujuan;

  if (UDP_USE_BROADCAST) {
    alamatTujuan =
        alamatBroadcast;
  }
  else {
    alamatTujuan =
        UDP_LAPTOP_IP;
  }

  int mulaiPaket =
      udp.beginPacket(
          alamatTujuan,
          UDP_DESTINATION_PORT
      );

  if (!mulaiPaket) {
    Serial.println(
        "UDP gagal memulai paket."
    );

    return;
  }

  udp.write(
      reinterpret_cast<
          const uint8_t*
      >(
          payload.c_str()
      ),
      payload.length()
  );

  int hasil =
      udp.endPacket();

  if (hasil == 1) {
    Serial.print(
        "UDP terkirim | seq="
    );

    Serial.print(
        nomorPaketUdp
    );

    Serial.print(
        " | tujuan="
    );

    Serial.print(
        alamatTujuan
    );

    Serial.print(":");

    Serial.print(
        UDP_DESTINATION_PORT
    );

    Serial.print(
        " | ukuran="
    );

    Serial.print(
        payload.length()
    );

    Serial.println(
        " byte"
    );
  }
  else {
    Serial.println(
        "UDP gagal dikirim."
    );
  }
}

void kirimDataThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(
        "ThingSpeak: WiFi tidak terhubung, dilewati."
    );

    return;
  }

  String body =
      "api_key=";

  body += THINGSPEAK_WRITE_KEY;

  /*
    field1 sampai field5: data NPK.
    Seluruhnya dilewati jika pembacaan Modbus gagal.
  */

  if (dataNpk.valid) {
    body += "&field1=";
    body += String(dataNpk.nitrogen);

    body += "&field2=";
    body += String(dataNpk.phosphorus);

    body += "&field3=";
    body += String(dataNpk.potassium);

    body += "&field4=";
    body += String(dataNpk.ph, 2);

    body += "&field5=";
    body += String(dataNpk.ec);
  }

    /*
    field6: 1 Data Angka Rata-rata Kelembapan Tanah (Soil 1 + Soil 2 + NPK)
    Contoh terkirim: 42.5
  */
  if (rataRataKelembapanTanah >= 0.0) {
    body += "&field6=";
    body += String(rataRataKelembapanTanah, 1);
  }

  /*
    field7 dan field8: suhu dan kelembapan luar.
  */

  if (!isnan(suhuLuarRata)) {
    body += "&field7=";
    body += String(suhuLuarRata, 2);
  }

  if (!isnan(lembapLuarRata)) {
    body += "&field8=";
    body += String(lembapLuarRata, 2);
  }

  /*
    Jika tidak ada satu pun sensor yang valid,
    tidak perlu mengirim apa pun.
  */

  if (body.indexOf("&field") < 0) {
    Serial.println(
        "ThingSpeak: tidak ada data valid, dilewati."
    );

    return;
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(5000);

  if (!http.begin(client, THINGSPEAK_URL)) {
    Serial.println(
        "ThingSpeak: gagal memulai koneksi."
    );

    return;
  }

  http.addHeader(
      "Content-Type",
      "application/x-www-form-urlencoded"
  );

  int kode =
      http.POST(body);

  if (kode > 0) {
    String balasan =
        http.getString();

    balasan.trim();

    Serial.print("ThingSpeak: HTTP ");
    Serial.print(kode);
    Serial.print(" -> entry ");
    Serial.println(balasan);

    if (balasan == "0") {
      Serial.println(
          "ThingSpeak: DITOLAK. Periksa write key "
          "atau interval pengiriman."
      );
    }
  }
  else {
    Serial.print("ThingSpeak: error ");
    Serial.println(kode);
  }

  http.end();
}

// =====================================================
// KIRIM DATA THINGSPEAK - CHANNEL PENDUKUNG (NODE 1)
// =====================================================

void kirimDataThingSpeakPendukung() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(
        "ThingSpeak pendukung: WiFi tidak terhubung, dilewati."
    );

    return;
  }

  String body =
      "api_key=";

  body += THINGSPEAK_WRITE_KEY_PENDUKUNG;

  /*
    field1: Suhu Tanah dari sensor NPK Modbus (°C).
  */
  if (dataNpk.valid) {
    body += "&field1=";
    body += String(dataNpk.suhu, 1);
  }

  /*
    field2: Tegangan sisi baterai dari INA226 Modul 1 (V).
  */
  if (inaBateraiValid) {
    body += "&field2=";
    body += String(tegangan_baterai, 2);
  }

  /*
    field3: Tegangan sisi load dari INA226 Modul 2 (V).
  */
  if (inaLoadValid) {
    body += "&field3=";
    body += String(tegangan_load, 2);
  }

  /*
    field4: Arus sisi baterai dari INA226 Modul 1 (A).
  */
  if (inaBateraiValid) {
    body += "&field4=";
    body += String(arus_baterai, 3);
  }

  /*
    field5: Arus sisi load dari INA226 Modul 2 (A).
  */
  if (inaLoadValid) {
    body += "&field5=";
    body += String(arus_load, 3);
  }

  /*
    field6: Suhu dalam box panel dari DHT22 dalam (°C).
  */
  if (!isnan(suhuDalam)) {
    body += "&field6=";
    body += String(suhuDalam, 1);
  }

  /*
    field7: Status aktuator sebagai bitmask integer.
    bit0 = kipas   (nilai 1)
    bit1 = lampu   (nilai 2)
    bit2 = valve   (nilai 4)
    bit3 = timeout (nilai 8)

    Contoh: kipas ON + valve OPEN = 1 + 4 = 5
  */
  uint8_t statusAktuator = 0;

  if (kipasNyala)   statusAktuator |= 0b0001;
  if (lampuNyala)   statusAktuator |= 0b0010;
  if (valveTerbuka) statusAktuator |= 0b0100;
  if (valveTimeout) statusAktuator |= 0b1000;

  body += "&field7=";
  body += String(statusAktuator);

  /*
    field8: Daya Beban Aktual (Watt)
  */
  if (inaLoadValid) {
    body += "&field8=";
    body += String(daya_load, 2);
  }
  // ---------------------------------------


  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(5000);

  if (!http.begin(client, THINGSPEAK_URL)) {
    Serial.println(
        "ThingSpeak pendukung: gagal memulai koneksi."
    );

    return;
  }

  http.addHeader(
      "Content-Type",
      "application/x-www-form-urlencoded"
  );

  int kode =
      http.POST(body);

  if (kode > 0) {
    String balasan =
        http.getString();

    balasan.trim();

    Serial.print("ThingSpeak pendukung: HTTP ");
    Serial.print(kode);
    Serial.print(" -> entry ");
    Serial.println(balasan);

    if (balasan == "0") {
      Serial.println(
          "ThingSpeak pendukung: DITOLAK. Periksa write key "
          "atau interval pengiriman."
      );
    }
  }
  else {
    Serial.print("ThingSpeak pendukung: error ");
    Serial.println(kode);
  }

  http.end();
}
// =====================================================
// TAMPIL DATA SERIAL
// =====================================================

void tampilkanDataSerial() {
  Serial.println();

  Serial.println(
      "---------------- DATA SISTEM ----------------"
  );

  if (rtcTerdeteksi) {
    DateTime now =
        rtc.now();

    Serial.print(
        "Tanggal             : "
    );

    print2Digits(
        now.day()
    );

    Serial.print("/");

    print2Digits(
        now.month()
    );

    Serial.print("/");

    Serial.println(
        now.year()
    );

    Serial.print(
        "Waktu               : "
    );

    print2Digits(
        now.hour()
    );

    Serial.print(":");

    print2Digits(
        now.minute()
    );

    Serial.print(":");

    print2Digits(
        now.second()
    );

    Serial.println();
  }
  else {
    Serial.println(
        "RTC                 : ERROR"
    );
  }

  Serial.print(
      "Wi-Fi               : "
  );

  if (
      WiFi.status() ==
      WL_CONNECTED
  ) {
    Serial.print(
        "CONNECTED | IP "
    );

    Serial.print(
        WiFi.localIP()
    );

    Serial.print(
        " | RSSI "
    );

    Serial.print(
        WiFi.RSSI()
    );

    Serial.println(
        " dBm"
    );
  }
  else {
    Serial.println(
        "DISCONNECTED"
    );
  }

  Serial.print(
      "LDR                 : "
  );

  Serial.print(
      nilaiLDR
  );

  Serial.print(
      " | "
  );

  Serial.println(
      kondisiCahaya(
          nilaiLDR
      )
  );

  // === DATA DAYA PLTS (INA226) ===

  Serial.println(
      "------------- DAYA PLTS (INA226) ------------"
  );

  if (inaBateraiValid) {
    Serial.print(
        "[SCC->BAT]  Tegangan: "
    );

    Serial.print(
        tegangan_baterai,
        2
    );

    Serial.print(
        " V | Arus: "
    );

    Serial.print(
        arus_baterai,
        3
    );

    Serial.print(
        " A | Daya: "
    );

    Serial.print(
        daya_baterai,
        2
    );

    Serial.println(
        " W"
    );
  }
  else {
    Serial.println(
        "[SCC->BAT]  : SENSOR ERROR / TIDAK TERHUBUNG"
    );
  }

  if (inaLoadValid) {
    Serial.print(
        "[SCC->LOAD] Tegangan: "
    );

    Serial.print(
        tegangan_load,
        2
    );

    Serial.print(
        " V | Arus: "
    );

    Serial.print(
        arus_load,
        3
    );

    Serial.print(
        " A | Daya: "
    );

    Serial.print(
        daya_load,
        2
    );

    Serial.println(
        " W"
    );
  }
  else {
    Serial.println(
        "[SCC->LOAD] : SENSOR ERROR / TIDAK TERHUBUNG"
    );
  }

  // ================================

  Serial.println(
      "---------------------------------------------"
  );

  if (
      !isnan(suhuLuar1) &&
      !isnan(lembapLuar1)
  ) {
    Serial.print(
        "Suhu Luar 1         : "
    );

    Serial.print(
        suhuLuar1,
        1
    );

    Serial.println(
        " C"
    );

    Serial.print(
        "Lembap Luar 1       : "
    );

    Serial.print(
        lembapLuar1,
        1
    );

    Serial.println(
        " %"
    );
  }
  else {
    Serial.println(
        "DHT Luar 1          : Gagal terbaca"
    );
  }

  if (
      !isnan(suhuLuar2) &&
      !isnan(lembapLuar2)
  ) {
    Serial.print(
        "Suhu Luar 2         : "
    );

    Serial.print(
        suhuLuar2,
        1
    );

    Serial.println(
        " C"
    );

    Serial.print(
        "Lembap Luar 2       : "
    );

    Serial.print(
        lembapLuar2,
        1
    );

    Serial.println(
        " %"
    );
  }
  else {
    Serial.println(
        "DHT Luar 2          : Gagal terbaca"
    );
  }

  if (
      !isnan(suhuDalam) &&
      !isnan(lembapDalam)
  ) {
    Serial.print(
        "Suhu Dalam Panel    : "
    );

    Serial.print(
        suhuDalam,
        1
    );

    Serial.println(
        " C"
    );

    Serial.print(
        "Lembap Dalam Panel  : "
    );

    Serial.print(
        lembapDalam,
        1
    );

    Serial.println(
        " %"
    );
  }
  else {
    Serial.println(
        "DHT Dalam           : Gagal terbaca"
    );
  }

  Serial.println(
      "---------------------------------------------"
  );

  for (
      uint8_t i = 0;
      i < 3;
      i++
  ) {
    Serial.print(
        "Sensor tanah "
    );

    Serial.print(
        i + 1
    );

    Serial.print(
        "     : ADC "
    );

    Serial.print(
        nilaiSoil[i]
    );

    Serial.print(
        " | "
    );

    if (soilValid[i]) {
      Serial.print(
          persenSoil[i],
          1
      );

      Serial.print(
          "% | VALID"
      );
    }
    else {
      Serial.print(
          "TIDAK TERBACA | ERROR"
      );
    }

    Serial.println();
  }

  Serial.print(
      "Rata-rata tanah     : "
  );

  if (
      rataRataKelembapanTanah >=
      0.0
  ) {
    Serial.print(
        rataRataKelembapanTanah,
        1
    );

    Serial.println(
        " %"
    );
  }
  else {
    Serial.println(
        "TIDAK TERSEDIA"
    );
  }

  Serial.println(
      "---------------------------------------------"
  );

  if (dataNpk.valid) {
    Serial.print(
        "NPK Kelembapan      : "
    );

    Serial.print(
        dataNpk.kelembapan,
        1
    );

    Serial.println(
        " %"
    );

    Serial.print(
        "NPK Suhu            : "
    );

    Serial.print(
        dataNpk.suhu,
        1
    );

    Serial.println(
        " C"
    );

    Serial.print(
        "NPK EC              : "
    );

    Serial.println(
        dataNpk.ec
    );

    Serial.print(
        "NPK pH              : "
    );

    Serial.println(
        dataNpk.ph,
        1
    );

    Serial.print(
        "N / P / K           : "
    );

    Serial.print(
        dataNpk.nitrogen
    );

    Serial.print(
        " / "
    );

    Serial.print(
        dataNpk.phosphorus
    );

    Serial.print(
        " / "
    );

    Serial.println(
        dataNpk.potassium
    );
  }
  else {
    Serial.print(
        "NPK                 : ERROR MODBUS "
    );

    Serial.println(
        dataNpk.kodeModbus
    );
  }

  Serial.println(
      "---------------------------------------------"
  );

  Serial.print(
      "Kipas               : "
  );

  Serial.println(
      kipasNyala ?
      "ON" :
      "OFF"
  );

  Serial.print(
      "Lampu               : "
  );

  Serial.println(
      lampuNyala ?
      "ON" :
      "OFF"
  );

  Serial.print(
      "Valve               : "
  );

  Serial.println(
      valveTerbuka ?
      "OPEN" :
      "CLOSED"
  );

  Serial.print(
      "Valve timeout       : "
  );

  Serial.println(
      valveTimeout ?
      "FAULT" :
      "NORMAL"
  );
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(
      115200
  );

  delay(1000);

  Serial.println();

  Serial.println(
      "=============================================="
  );

  Serial.println(
      " SISTEM MONITORING ESP32-S3 + UDP + 2x INA226"
  );

  Serial.println(
      "=============================================="
  );

  // Relay
  pinMode(
      RELAY_KIPAS_PIN,
      OUTPUT
  );

  pinMode(
      RELAY_LAMPU_PIN,
      OUTPUT
  );

  pinMode(
      RELAY_AKTUATOR_PIN,
      OUTPUT
  );

  setKipas(false);
  setLampu(false);
  setValve(false);

  // ADC
  pinMode(
      LDR_PIN,
      INPUT
  );

  pinMode(
      SOIL_1_PIN,
      INPUT
  );

  pinMode(
      SOIL_2_PIN,
      INPUT
  );

  pinMode(
      SOIL_3_PIN,
      INPUT
  );

  analogReadResolution(
      12
  );

  analogSetPinAttenuation(
      LDR_PIN,
      ADC_11db
  );

  analogSetPinAttenuation(
      SOIL_1_PIN,
      ADC_11db
  );

  analogSetPinAttenuation(
      SOIL_2_PIN,
      ADC_11db
  );

  analogSetPinAttenuation(
      SOIL_3_PIN,
      ADC_11db
  );

  // DHT
  dhtLuar1.begin();
  dhtDalam.begin();
  dhtLuar2.begin();

  // RTC + 2x INA226 di jalur I2C yang sama (Pin 8 & 9)
  Wire.begin(
      SDA_RTC,
      SCL_RTC
  );

  if (!rtc.begin()) {
    rtcTerdeteksi =
        false;

    Serial.println(
        "RTC TIDAK TERDETEKSI."
    );

    setLampu(false);
  }
  else {
    rtcTerdeteksi =
        true;

    Serial.println(
        "RTC terdeteksi."
    );

    if (rtc.lostPower()) {
      Serial.println(
          "PERINGATAN: RTC pernah kehilangan daya."
      );
    }

    if (SET_RTC_ONCE) {
      rtc.adjust(
          DateTime(
              F(__DATE__),
              F(__TIME__)
          )
      );

      Serial.println(
          "RTC disetel dari waktu kompilasi."
      );

      Serial.println(
          "Ubah SET_RTC_ONCE menjadi false."
      );
    }
  }

  // === INA226 Modul 1: SCC ke Baterai (Alamat 0x44) ===

  if (!inaBaterai.init()) {
    inaBateraiValid =
        false;

    Serial.println(
        "INA226 BATERAI (0x44) TIDAK TERDETEKSI!"
    );
  }
  else {
    inaBateraiValid =
        true;

    inaBaterai.setResistorRange(
        R_SHUNT_BATERAI,
        I_MAX_BATERAI
    );

    inaBaterai.setMeasureMode(
        INA226_CONTINUOUS
    );

    inaBaterai.waitUntilConversionCompleted();

    Serial.println(
        "INA226 BATERAI (0x44) siap!"
    );
  }

  // === INA226 Modul 2: SCC ke Load (Alamat 0x45) ===

  if (!inaLoad.init()) {
    inaLoadValid =
        false;

    Serial.println(
        "INA226 LOAD (0x45) TIDAK TERDETEKSI! Cek solder pad A0."
    );
  }
  else {
    inaLoadValid =
        true;

    inaLoad.setResistorRange(
        R_SHUNT_LOAD,
        I_MAX_LOAD
    );

    inaLoad.setMeasureMode(
        INA226_CONTINUOUS
    );

    inaLoad.waitUntilConversionCompleted();

    Serial.println(
        "INA226 LOAD (0x45) siap!"
    );
  }

  // MAX485
  pinMode(
      RS485_DIR_PIN,
      OUTPUT
  );

  digitalWrite(
      RS485_DIR_PIN,
      LOW
  );

  rs485Serial.begin(
      NPK_BAUD_RATE,
      SERIAL_8N1,
      RS485_RX_PIN,
      RS485_TX_PIN
  );

  npkNode.begin(
      NPK_SLAVE_ID,
      rs485Serial
  );

  npkNode.preTransmission(
      rs485PreTransmission
  );

  npkNode.postTransmission(
      rs485PostTransmission
  );

  /*
    Pembacaan pertama dilakukan sebelum
    WiFi dimulai.
  */

  bacaSensorKelembapan();

  // WiFi
  mulaiWifi();

  unsigned long sekarang =
      millis();

  waktuDhtDalamTerakhir =
      sekarang -
      INTERVAL_DHT_DALAM;

  waktuRtcTerakhir =
      sekarang -
      INTERVAL_RTC;

  waktuDhtLuarTerakhir =
      sekarang -
      INTERVAL_DHT_LUAR;

  waktuLdrTerakhir =
      sekarang -
      INTERVAL_LDR;

  /*
    Sensor tanah baru dibaca lagi
    setelah 10 detik.
  */

  waktuSoilTerakhir =
      sekarang;

  waktuNpkTerakhir =
      sekarang -
      INTERVAL_NPK;

  waktuTampilTerakhir =
      sekarang -
      INTERVAL_TAMPIL;

  waktuUdpTerakhir =
      sekarang -
      INTERVAL_UDP;

  waktuValveBerhenti =
      sekarang -
      JEDA_MINIMUM_VALVE;

  waktuThingSpeakTerakhir = 
      sekarang;

  waktuInaTerakhir =
      sekarang -
      INTERVAL_INA226;
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  unsigned long sekarang =
      millis();

  // Periksa WiFi
  periksaWifi(
      sekarang
  );

  // ---------------------------------------------------
  // DHT DALAM DAN FAN
  // ---------------------------------------------------

  if (
      sekarang -
      waktuDhtDalamTerakhir >=
      INTERVAL_DHT_DALAM
  ) {
    waktuDhtDalamTerakhir =
        sekarang;

    suhuDalam =
        dhtDalam.readTemperature();

    lembapDalam =
        dhtDalam.readHumidity();

    bool dhtDalamValid =
        !isnan(suhuDalam) &&
        !isnan(lembapDalam);

    if (kipasNyala) {
      // Kipas sedang aktif: periksa apakah sudah menyala selama 5 menit (300.000 ms)
      if (
          sekarang -
          waktuKipasMulai >=
          DURASI_KIPAS_NYALA
      ) {
        setKipas(false);
        kipasSelesaiSiklus = true;

        // Jika saat mati suhu sudah turun di bawah 34 C, langsung reset siklus
        if (dhtDalamValid && suhuDalam < SUHU_KIPAS_ON) {
          kipasSelesaiSiklus = false;
        }
      }
    }

    if (!kipasNyala) {
      if (dhtDalamValid) {
        if (suhuDalam < SUHU_KIPAS_ON) {
          // Suhu berada di bawah 34 C: reset siklus agar siap menyala kembali saat menyentuh 34 C lagi
          kipasSelesaiSiklus = false;
        }
        else if (suhuDalam >= SUHU_KIPAS_ON && !kipasSelesaiSiklus) {
          // Suhu menyentuh ambang batas 34 C: nyalakan kipas selama 5 menit
          setKipas(true);
          waktuKipasMulai = sekarang;
        }
      }
    }
  }

  // ---------------------------------------------------
  // RTC, LAMPU, DAN VALVE
  // ---------------------------------------------------

  if (
      sekarang -
      waktuRtcTerakhir >=
      INTERVAL_RTC
  ) {
    waktuRtcTerakhir =
        sekarang;

    if (rtcTerdeteksi) {
      DateTime now =
          rtc.now();

      setLampu(
          jadwalLampuNyala(
              now.hour()
          )
      );
    }
    else {
      setLampu(false);
    }

    kontrolValve(
        sekarang
    );
  }

  // ---------------------------------------------------
  // DHT LUAR
  // ---------------------------------------------------

  if (
      sekarang -
      waktuDhtLuarTerakhir >=
      INTERVAL_DHT_LUAR
  ) {
    waktuDhtLuarTerakhir =
        sekarang;

    suhuLuar1 =
        dhtLuar1.readTemperature();

    lembapLuar1 =
        dhtLuar1.readHumidity();

    suhuLuar2 =
        dhtLuar2.readTemperature();

    lembapLuar2 =
        dhtLuar2.readHumidity();

    bool dhtLuar1Valid =
        !isnan(suhuLuar1) &&
        !isnan(lembapLuar1);

    bool dhtLuar2Valid =
        !isnan(suhuLuar2) &&
        !isnan(lembapLuar2);

    if (
        dhtLuar1Valid &&
        dhtLuar2Valid
    ) {
      suhuLuarRata =
          (
            suhuLuar1 +
            suhuLuar2
          ) /
          2.0;

      lembapLuarRata =
          (
            lembapLuar1 +
            lembapLuar2
          ) /
          2.0;
    }
    else {
      suhuLuarRata =
          NAN;

      lembapLuarRata =
          NAN;
    }
  }

  // ---------------------------------------------------
  // LDR
  // ---------------------------------------------------

  if (
      sekarang -
      waktuLdrTerakhir >=
      INTERVAL_LDR
  ) {
    waktuLdrTerakhir =
        sekarang;

    nilaiLDR =
        bacaAdcRataRata(
            LDR_PIN
        );
  }

  // ---------------------------------------------------
  // SENSOR TANAH DAN VALVE
  // ---------------------------------------------------

  if (
      sekarang -
      waktuSoilTerakhir >=
      INTERVAL_SOIL
  ) {
    waktuSoilTerakhir =
        sekarang;

    bacaSensorKelembapan();
  }

  // Proteksi valve maksimum
  if (
      valveTerbuka &&
      sekarang -
      waktuValveMulai >=
      MAKSIMUM_WAKTU_VALVE
  ) {
    setValve(false);

    waktuValveBerhenti =
        sekarang;

    valveTimeout =
        true;
  }

  // ---------------------------------------------------
  // NPK MODBUS
  // ---------------------------------------------------

  if (
      sekarang -
      waktuNpkTerakhir >=
      INTERVAL_NPK
  ) {
    waktuNpkTerakhir =
        sekarang;

    bacaSensorNpk();
  }

  // ---------------------------------------------------
  // SENSOR DAYA INA226
  // ---------------------------------------------------

  if (
      sekarang -
      waktuInaTerakhir >=
      INTERVAL_INA226
  ) {
    waktuInaTerakhir =
        sekarang;

    bacaSensorDaya();
  }

  // ---------------------------------------------------
  // KIRIM UDP
  // ---------------------------------------------------

  if (
      sekarang -
      waktuUdpTerakhir >=
      INTERVAL_UDP
  ) {
    waktuUdpTerakhir =
        sekarang;

    kirimDataUdp();
  }
 
if (
    millis() -
    waktuThingSpeakTerakhir >=
    INTERVAL_THINGSPEAK
) {


  kirimDataThingSpeak();

  /*
    Jadwalkan channel pendukung 5 detik setelah
    channel utama, agar tidak ada dua koneksi
    TLS beruntun yang menguras heap.
  */
  waktuThingSpeakTerakhir =
      millis();
  waktuThingSpeakPendukungTerakhir =
      millis();

  pendukungMenunggu =
      true;
}

if (
    pendukungMenunggu &&
    millis() -
    waktuThingSpeakPendukungTerakhir >=
    INTERVAL_THINGSPEAK_PENDUKUNG
) {
  pendukungMenunggu =
      false;

  kirimDataThingSpeakPendukung();
}
  // ---------------------------------------------------
  // SERIAL MONITOR
  // ---------------------------------------------------

  if (
      sekarang -
      waktuTampilTerakhir >=
      INTERVAL_TAMPIL
  ) {
    waktuTampilTerakhir =
        sekarang;

    tampilkanDataSerial();
  }

  delay(2);
}