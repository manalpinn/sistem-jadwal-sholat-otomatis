# 📿 ESP32 Jadwal Sholat Otomatis

Sistem **jadwal sholat otomatis berbasis ESP32 DevKit V1** dengan konfigurasi melalui **website (Web Configuration)**. Perangkat ini menampilkan jadwal sholat di LCD, membunyikan adzan menggunakan DFPlayer, serta tetap berfungsi walaupun **tanpa koneksi WiFi** karena seluruh data penting disimpan di memori internal ESP32.

---

## ✨ Fitur Utama

* **ESP32 DevKit V1**
* **LCD 20x4 I2C** untuk tampilan waktu dan jadwal sholat
* Pengambilan jadwal sholat **1 bulan penuh** dari API MyQuran
* Pencarian dan pemilihan kota otomatis melalui website
* Penyimpanan jadwal sholat dalam **format JSON** di ESP32
* Tetap berfungsi walaupun WiFi mati (offline mode)
* **DFPlayer Mini** sebagai pemutar audio adzan
* **Buzzer aktif 5V (GPIO 23)** dapat diaktifkan / dinonaktifkan melalui website
* Konfigurasi **100% melalui website** (tanpa tombol fisik)

---

## 🧠 Alur Kerja Sistem

1. ESP32 menyala dan membaca konfigurasi dari memori.
2. Jika data kota atau jadwal belum tersedia, ESP32 otomatis masuk **Access Point Mode**.
3. Pengguna menghubungkan HP/Laptop ke WiFi ESP32.
4. Pengguna membuka halaman konfigurasi melalui browser.
5. Pengguna mencari dan memilih kota.
6. ESP32 mengunduh jadwal sholat **1 bulan penuh** dan menyimpannya ke memori.
7. Pada setiap waktu sholat:

   * DFPlayer memutar suara adzan.
   * Buzzer berbunyi **hanya jika diaktifkan melalui Web Configuration**.

---

## 🌐 API MyQuran

### 🔎 Pencarian Kota

```text
https://api.myquran.com/v2/sholat/kota/cari/:kota
```

### 📅 Jadwal Sholat Bulanan

```text
https://api.myquran.com/v2/sholat/jadwal/:kota/:tahun/:bulan
```

---

## 🗂️ Struktur Folder Proyek

```text
ESP32-Jadwal-Sholat/
├── src/
│   └── main.cpp
├── data/
│   ├── index.html
│   ├── style.css
│   └── script.js
├── README.md
```

---

## 🔧 Perangkat Keras

* ESP32 DevKit V1
* LCD 20x4 I2C
* DFPlayer Mini + speaker
* Buzzer aktif 5V (GPIO 23)
* Power supply 5V

---

## 🔌 Mapping Pin (Rekomendasi)

| Perangkat   | GPIO    |
| ----------- | ------- |
| LCD SDA     | GPIO 21 |
| LCD SCL     | GPIO 22 |
| DFPlayer RX | GPIO 13 |
| DFPlayer TX | GPIO 14 |
| Buzzer      | GPIO 23 |

---

## 🛠️ Software & Library

* PlatformIO atau Arduino IDE
* `WiFi.h`
* `HTTPClient.h`
* `ArduinoJson`
* `LiquidCrystal_I2C`
* `DFRobotDFPlayerMini`
* `WebServer` / `ESPAsyncWebServer`

---

## 🚀 Cara Menjalankan

1. Upload firmware ke ESP32.
2. ESP32 otomatis membuat WiFi Access Point:

   * **SSID:** `ESP32-JadwalShola`
   * **Password:** `12345678`
3. Hubungkan HP/Laptop ke WiFi ESP32.
4. Buka browser dan akses:

   ```
   http://192.168.4.1
   ```
5. Cari dan pilih kota melalui halaman konfigurasi.
6. Jadwal sholat otomatis tersimpan dan langsung aktif.

---

## ⚠️ Catatan Penting

* Jadwal sholat disimpan dalam **format JSON**.
* WiFi hanya diperlukan saat konfigurasi awal dan pembaruan jadwal.
* Sistem tetap berjalan normal walaupun WiFi tidak tersedia.

---

## 📌 Rencana Pengembangan

* OTA Firmware Update
* Dark Mode Web Configuration
* Optimalisasi sinkronisasi waktu

---

## 📄 Lisensi

Proyek ini bersifat **open-source** dan bebas digunakan untuk pembelajaran maupun pengembangan.

---

🙏 *Semoga bermanfaat dan menjad
