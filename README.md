# Sistem Pertanian Cerdas (Smart Agriculture) Berbasis IoT - PKM Margorejo

Sistem Pertanian Cerdas (*Smart Agriculture*) berbasis IoT dengan pemantauan mandiri Pembangkit Listrik Tenaga Surya (PLTS). Sistem ini dirancang untuk bekerja secara otomatis, memiliki perlindungan (*safety / fail-safe*), dan toleran terhadap gangguan internet (*offline-tolerant*).

---

## 📌 Fitur Utama

1. **Mandiri Sepenuhnya (*Self-Powered*):** Didukung catu daya PLTS (Solar Panel, Solar Charge Controller, dan Baterai VRLA 12V 12Ah) serta RTC internal untuk penjadwalan mandiri.
2. **Offline-Tolerant:** Kontrol aktuator (penyiraman, kipas, lampu) tetap beroperasi normal meskipun koneksi WiFi atau ThingSpeak terputus.
3. **Sistem Proteksi Mandiri (*Self-Protecting*):**
   - **Valve Safety Timeout (+8):** Menutup valve otomatis jika menyala terus-menerus selama 10 menit untuk mencegah kebanjiran atau pompa rusak.
   - **Auto-Fan:** Mendinginkan suhu kotak panel secara otomatis.
4. **Standar Industri:** Menggunakan protokol Modbus RS485 untuk sensor hara tanah (NPK) dan UDP broadcast untuk telemetri berkecepatan tinggi.
5. **Efisiensi Kuota & Memori Cloud:** Pengiriman data ThingSpeak diatur setiap **5 menit sekali** dengan jeda 5 detik antar-channel serta kompresi status aktuator menggunakan sistem **Bitmask (0-15)**.

---

## 🛠️ Arsitektur Perangkat Keras (Hardware)

Sistem menggunakan **dua node** berbasis mikrokontroler **ESP32-S3**:

| Komponen | Spesifikasi / Pin | Fungsi |
| :--- | :--- | :--- |
| **Mikrokontroler** | ESP32-S3 | Unit pemroses utama logika dan telemetri |
| **Catu Daya (PLTS)** | Solar Panel + SCC + Baterai 12V 12Ah | Sumber energi mandiri |
| **Sensor Daya (Dual INA226)** | I2C (Pin 8 SDA, Pin 9 SCL) | Modul 1 (0x44): Arus/tegangan SCC ke Baterai<br>Modul 2 (0x45): Arus/tegangan SCC ke Load |
| **Sensor Suhu & Kelembapan** | 3x DHT22 (GPIO 6, 16, dan 7) | 2 sensor luar (rata-rata cuaca), 1 sensor dalam boks panel |
| **Sensor NPK 7-in-1** | RS485 Modbus RTU (MAX485: Pin 14 DIR, 17 TX, 18 RX) | Mengukur N, P, K, pH, EC, suhu tanah, dan kelembapan tanah |
| **Sensor Kelembapan Tanah Analog** | Kapasitif (GPIO 10, 11, 12) | Pengukuran kelembapan tanah probe analog (khusus Node 1) |
| **Sensor Cahaya** | LDR (GPIO 4) | Pemantauan intensitas cahaya matahari |
| **Real-Time Clock** | DS3231 (I2C Pin 8 SDA, 9 SCL) | Pewaktuan presisi untuk jadwal lampu offline |
| **Aktuator (Relay)** | GPIO 5 (Valve), GPIO 15 (Lampu), GPIO 39 (Kipas) | Pengendali fisik irigasi, penerangan, dan sirkulasi udara |

---

## ⚙️ Logika Kontrol Otomasi

1. **Penyiraman Terjadwal (Solenoid Valve):**
   - Bekerja otomatis menggunakan jadwal **RTC DS3231**:
     - **Pagi:** Pukul 07:00 - 07:15 WIB (15 menit)
     - **Sore:** Pukul 16:00 - 16:15 WIB (15 menit)
   - **Timeout Proteksi (Fail-Safe):** Jika terjadi malfungsi RTC, valve otomatis diputus setelah 20 menit menyala terus-menerus.
2. **Manajemen Suhu Panel (Kipas):**
   - Kipas menyala otomatis saat suhu panel $\ge 34.0^\circ\text{C}$ selama **5 menit (300.000 ms)**, kemudian mati.
   - Kipas akan menyala kembali jika suhu menyentuh $34.0^\circ\text{C}$ lagi (setelah suhu sempat turun di bawah $34.0^\circ\text{C}$).
3. **Penerangan Otomatis (Lampu):**
   - Bekerja otomatis berdasarkan waktu RTC: Menyala pukul 18:00 WIB dan padam pukul 06:00 WIB.

---

## 📡 Telemetri dan Pemantauan Data

* **Jalur Cepat Lokal (UDP Broadcast):**
  - Mengirim payload JSON setiap **1 detik** ke port 4210.
  - Node 1 menggunakan local port 4211, Node 2 menggunakan local port 4212.
  - Ditangkap oleh script Python di laptop secara langsung tanpa memerlukan akses internet.
* **Penyimpanan Awan (ThingSpeak Dual-Channel):**
  - Mengirim data setiap **5 menit sekali (300.000 ms)**.
  - Channel Utama dan Channel Pendukung dikirim bergantian dengan jeda **5 detik** untuk mencegah kehabisan heap memory TLS pada ESP32.
  - Status aktuator dikirim dalam 1 angka bitmask:
    - Bit 0: Kipas (1)
    - Bit 1: Lampu (2)
    - Bit 2: Valve (4)
    - Bit 3: Valve Timeout (8)

---

## 📂 Struktur Direktori

`	ext
.
├── KODE_ESP_NODE1_SEMOGASUDAHFIX/
│   └── KODE_ESP_NODE1_SEMOGASUDAHFIX.ino   # Firmware untuk Node 1 (AGRI-01)
├── KODE_ESP_NODE2_SEMOGASUDAHFIX/
│   └── KODE_ESP_NODE2_SEMOGASUDAHFIX.ino   # Firmware untuk Node 2 (AGRI-02)
├── .gitignore
└── README.md
`
