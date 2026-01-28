#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <RTClib.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include "DFRobotDFPlayerMini.h"
#include <WebServer.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>

// ===== TELEGRAM CONFIG =====
#define BOT_TOKEN "8509632695:AAHaJ4g08QafGez7zcb1JjqXmYsxTf4HehA"
#define CHAT_ID "1206871328"

// ===== PIN CONFIG =====
#define BUTTON_PIN 18
#define BUZZER_PIN 23
#define DFPLAYER_RX 13
#define DFPLAYER_TX 14
#define RTC_SDA 25
#define RTC_SCL 26
#define LCD_SDA 21
#define LCD_SCL 22

// ===== CONSTANTS =====
#define LONG_PRESS_MS 3000
#define LITTLEFS_CONFIG "/config.json"
#define LITTLEFS_JADWAL "/jadwal.json"
#define LITTLEFS_KOTA "/kota.json"
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 7 * 3600 // GMT+7 untuk WIB
#define DAYLIGHT_OFFSET_SEC 0
#define PASSWORD_ERROR_DISPLAY_TIME 5000
#define NTP_SYNC_INTERVAL 24 * 60 * 60 * 1000 // 24 jam
#define MAX_TIME_DIFF 60                      // Maksimum selisih waktu (detik)
#define EEPROM_SIZE 512
#define EEPROM_TIME_ADDR 0
#define BACKUP_INTERVAL 60 * 60 * 1000 // Backup setiap 1 jam

// ===== STRUCTS =====
struct TimeBackup
{
  uint32_t timestamp; // Unix timestamp
  uint32_t checksum;  // Untuk validasi
};

struct JadwalToday
{
  String subuh;
  String dzuhur;
  String ashar;
  String maghrib;
  String isya;
};

// ===== OBJECTS =====
RTC_DS3231 rtc;
TwoWire I2CRTC = TwoWire(1);
LiquidCrystal_I2C lcd(0x27, 20, 4);
HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
WebServer server(80);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ===== TELEGRAM VARIABLES =====
unsigned long lastTelegramCheck = 0;
bool telegramEnabled = true;

// ===== GLOBAL VARIABLES =====
bool buzzerEnabled = true;
int dfVolume = 20;
int lcdMode = 0;
String wifiSSID = "";
String wifiPassword = "";
String kotaIDStr = "";
String kotaNama = "";
int kotaID = 0;
bool rtcAvailable = false;
const char *namaSholat[] = {"Subuh", "Dzuhur", "Ashar", "Maghrib", "Isya"};

static String lastLine0 = "";
static String lastLine1 = "";
static String lastLine2 = "";
static String lastLine3 = "";

unsigned long buttonPressStart = 0;
bool buttonDown = false;
unsigned long lastMinuteCheck = 0;
bool lcdLocked = false;
unsigned long lcdLockUntil = 0;
int lastStatus = -1;
bool dfPlayerReady = false;
unsigned long passwordErrorStart = 0;
bool showingPasswordError = false;
unsigned long lastNTPSync = 0;
bool wifiPasswordError = false;
unsigned long lastBackupTime = 0;

// ===== JSON DOCUMENTS =====
DynamicJsonDocument configDoc(1024);
DynamicJsonDocument jadwalDoc(32768);
DynamicJsonDocument kotaDoc(1024);

// ===== FUNCTION DECLARATIONS =====
void loadConfig();
void saveConfig();
bool loadKota();
bool saveKota(int id, const String &nama);
bool loadJadwal();
bool saveJadwal(const String &json);
JadwalToday getTodayJadwal();
String getTodayKey();
String getTanggalHariIni();
String padKotaID(String id);
bool shouldTrigger(const String &timeHHMM);
void playAdzan(int index);
void buttonHandler();
void updateLCD();
void updateLCDNow();
int determineStatus();
void displayStatusMessage(int status);
void setupWiFi();
bool syncRTCwithNTP();
void handleAPI();
void handleTelegram();
void sendTelegramMessage(const String &message);
void displayPasswordError();
String truncateText(String text, int maxLength);
void updateLCDLine(int line, String text, String &lastText);
void showWifiPasswordError();
void checkRTCBattery();
void backupTimeToEEPROM();
bool restoreTimeFromEEPROM();
String getFormattedTime(DateTime dt);
String getFormattedDate(DateTime dt);

// ===== HELPER FUNCTIONS =====
bool isKotaConfigured()
{
  return (kotaID > 0 && kotaNama.length() > 0);
}

bool isJadwalAvailable()
{
  JadwalToday jt = getTodayJadwal();
  return (jt.subuh.length() > 0);
}

String truncateText(String text, int maxLength)
{
  if (text.length() <= maxLength)
  {
    return text;
  }
  return text.substring(0, maxLength);
}

void updateLCDLine(int line, String text, String &lastText)
{
  if (text != lastText)
  {
    lcd.setCursor(0, line);

    if (text.length() < 20)
    {
      lcd.print(text);
      for (int i = text.length(); i < 20; i++)
      {
        lcd.print(" ");
      }
    }
    else
    {
      lcd.print(text.substring(0, 20));
    }

    lastText = text;
  }
}

String getFormattedTime(DateTime dt)
{
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", dt.hour(), dt.minute(), dt.second());
  return String(timeStr);
}

String getFormattedDate(DateTime dt)
{
  char dateStr[11];
  sprintf(dateStr, "%02d/%02d/%04d", dt.day(), dt.month(), dt.year());
  return String(dateStr);
}

// ===== RTC BATTERY FUNCTIONS =====
void checkRTCBattery()
{
  // Jika RTC tidak tersedia, hentikan pengecekan
  if (!rtcAvailable)
    return;

  // Periksa apakah RTC pernah kehilangan daya
  if (rtc.lostPower())
  {
    Serial.println("⚠️ Osilator RTC berhenti - kemungkinan baterai lemah!");

    // Kunci tampilan LCD sementara untuk menampilkan peringatan
    lcdLocked = true;
    lcdLockUntil = millis() + 5000;

    // Tampilkan peringatan baterai RTC di LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("BATERAI RTC LEMAH! ");
    lcd.setCursor(0, 1);
    lcd.print("Ganti baterai      ");
    lcd.setCursor(0, 2);
    lcd.print("CR2032             ");
    lcd.setCursor(0, 3);
    lcd.print("Waktu bisa hilang  ");
  }
}

// ===== EEPROM BACKUP FUNCTIONS =====
void backupTimeToEEPROM()
{
  if (!rtcAvailable)
    return;

  DateTime now = rtc.now();
  uint32_t unixTime = now.unixtime();

  TimeBackup backup;
  backup.timestamp = unixTime;
  backup.checksum = unixTime ^ 0xDEADBEEF; // Simple checksum

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_TIME_ADDR, backup);
  EEPROM.commit();
  EEPROM.end();

  Serial.printf("💾 Time backed up to EEPROM: %lu\n", unixTime);
}

bool restoreTimeFromEEPROM()
{
  EEPROM.begin(EEPROM_SIZE);

  TimeBackup backup;
  EEPROM.get(EEPROM_TIME_ADDR, backup);
  EEPROM.end();

  // Validasi checksum
  if (backup.checksum == (backup.timestamp ^ 0xDEADBEEF) &&
      backup.timestamp > 1609459200)
  { // Hanya jika timestamp > 2021-01-01
    rtc.adjust(DateTime(backup.timestamp));
    Serial.printf("💾 Time restored from EEPROM: %lu\n", backup.timestamp);
    return true;
  }

  return false;
}

// ===== NTP SYNC FUNCTION =====
bool syncRTCwithNTP()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("❌ Tidak dapat sinkron NTP: WiFi tidak terhubung");
    return false;
  }

  Serial.println("🔄 Mensinkronisasi waktu dari NTP...");

  // Konfigurasi NTP dengan GMT+7
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  // Tunggu untuk sinkronisasi
  delay(2000);

  // Dapatkan waktu saat ini (time_t adalah UTC)
  time_t now_utc = time(nullptr);

  if (now_utc < 100000)
  { // Jika waktu tidak valid
    Serial.println("❌ Gagal mendapatkan waktu dari NTP");
    return false;
  }

  // Konversi UTC ke waktu lokal (GMT+7)
  struct tm *timeinfo;
  timeinfo = localtime(&now_utc);

  // Buat DateTime untuk RTC (tambahkan 7 jam secara manual)
  DateTime ntpDateTime(now_utc + GMT_OFFSET_SEC); // Tambahkan 7 jam (25200 detik)

  // Dapatkan waktu dari RTC
  DateTime rtcTime;
  bool rtcValid = false;

  if (rtcAvailable)
  {
    rtcTime = rtc.now();
    if (rtcTime.year() >= 2023)
    {
      rtcValid = true;
    }
  }

  // Format untuk display
  char ntpTimeStr[20];
  sprintf(ntpTimeStr, "%04d-%02d-%02d %02d:%02d:%02d",
          ntpDateTime.year(), ntpDateTime.month(), ntpDateTime.day(),
          ntpDateTime.hour(), ntpDateTime.minute(), ntpDateTime.second());

  char rtcTimeStr[20];
  if (rtcValid)
  {
    sprintf(rtcTimeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            rtcTime.year(), rtcTime.month(), rtcTime.day(),
            rtcTime.hour(), rtcTime.minute(), rtcTime.second());
  }
  else
  {
    strcpy(rtcTimeStr, "Invalid");
  }

  Serial.printf("Waktu UTC dari NTP: %s", ctime(&now_utc));
  Serial.printf("Waktu Lokal (GMT+7): %s\n", ntpTimeStr);
  Serial.printf("Waktu RTC saat ini : %s\n", rtcTimeStr);

  if (rtcValid)
  {
    // Hitung selisih waktu dalam detik
    time_t rtcUnix = rtcTime.unixtime();
    time_t ntpUnixLocal = now_utc + GMT_OFFSET_SEC; // NTP dalam GMT+7
    int timeDiff = abs(ntpUnixLocal - rtcUnix);

    Serial.printf("Selisih waktu: %d detik\n", timeDiff);

    // Update RTC hanya jika selisih lebih dari batas maksimum
    if (timeDiff > MAX_TIME_DIFF)
    {
      Serial.printf("Selisih > %d detik, memperbarui RTC...\n", MAX_TIME_DIFF);

      // Perbarui RTC dengan waktu NTP (GMT+7)
      rtc.adjust(ntpDateTime);
      Serial.println("✅ RTC diperbarui dari NTP");

      // Backup waktu ke EEPROM
      backupTimeToEEPROM();

      // Kirim notifikasi Telegram
      if (telegramEnabled)
      {
        String message = "🕐 RTC telah disinkronisasi dengan NTP\n";
        message += "Waktu baru: " + String(ntpDateTime.hour()) + ":" +
                   String(ntpDateTime.minute()) + ":" + String(ntpDateTime.second()) + " WIB\n";
        message += "Tanggal: " + String(ntpDateTime.day()) + "/" +
                   String(ntpDateTime.month()) + "/" + String(ntpDateTime.year());
        sendTelegramMessage(message);
      }

      // Tampilkan di LCD
      lcdLocked = true;
      lcdLockUntil = millis() + 5000;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("RTC Diperbarui     ");
      lcd.setCursor(0, 1);
      lcd.print("Dari NTP Server    ");
      lcd.setCursor(0, 2);
      lcd.print(ntpTimeStr);

      lastNTPSync = millis();
      return true;
    }
    else
    {
      Serial.println("✅ Waktu RTC sudah akurat, tidak perlu update");

      // Tampilkan info di LCD singkat
      lcdLocked = true;
      lcdLockUntil = millis() + 2000;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Waktu RTC Akurat   ");
      lcd.setCursor(0, 1);
      lcd.print("Selisih: " + String(timeDiff) + "s");

      lastNTPSync = millis();
      return true;
    }
  }
  else
  {
    // RTC tidak valid, update langsung
    Serial.println("RTC tidak valid, mengupdate dari NTP...");
    rtc.adjust(ntpDateTime);
    Serial.println("✅ RTC diatur ulang dari NTP");

    // Backup waktu ke EEPROM
    backupTimeToEEPROM();

    // Tampilkan di LCD
    lcdLocked = true;
    lcdLockUntil = millis() + 5000;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC Diperbarui     ");
    lcd.setCursor(0, 1);
    lcd.print("Dari NTP Server    ");
    lcd.setCursor(0, 2);
    lcd.print(ntpTimeStr);

    lastNTPSync = millis();
    return true;
  }
}

// ===== FUNGSI TAMPILKAN ERROR PASSWORD =====
void displayPasswordError()
{
  showingPasswordError = true;
  passwordErrorStart = millis();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Password Salah!    ");
  lcd.setCursor(0, 1);
  lcd.print("Periksa password   ");
  lcd.setCursor(0, 2);
  lcd.print("                   ");
  lcd.setCursor(0, 3);
  lcd.print("                   ");

  Serial.println("❌ Password salah! Tampilkan di LCD selama 5 detik");

  // Bunyi buzzer sebagai notifikasi
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

// ===== FUNGSI TAMPILKAN ERROR PASSWORD WIFI =====
void showWifiPasswordError()
{
  wifiPasswordError = true;
  passwordErrorStart = millis();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Password WiFi      ");
  lcd.setCursor(0, 1);
  lcd.print("SALAH!             ");
  lcd.setCursor(0, 2);
  lcd.print("Periksa password di");
  lcd.setCursor(0, 3);
  lcd.print("web interface      ");

  Serial.println("❌ Password WiFi salah! Tampilkan di LCD selama 5 detik");

  // Bunyi buzzer sebagai notifikasi
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

// ===== SETUP =====
void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 Jadwal Sholat Booting ===");
  Serial.printf("Free heap awal: %d\n", ESP.getFreeHeap());

  // ===== PIN =====
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ===== LittleFS =====
  Serial.println("Mounting LittleFS...");
  if (!LittleFS.begin(true))
  {
    Serial.println("❌ LittleFS mount failed!");
    LittleFS.format();
    delay(1000);
    if (!LittleFS.begin(true))
    {
      Serial.println("❌ LittleFS mount failed even after format!");
    }
    else
    {
      Serial.println("✅ LittleFS formatted and mounted OK");
    }
  }
  else
  {
    Serial.println("✅ LittleFS mounted OK");
  }

  // ===== BUAT FILE DEFAULT JIKA TIDAK ADA =====
  const char *defaultFiles[] = {
      "/index.html", "/style.css", "/script.js",
      LITTLEFS_KOTA, LITTLEFS_JADWAL, LITTLEFS_CONFIG};

  for (int i = 0; i < 6; i++)
  {
    if (!LittleFS.exists(defaultFiles[i]))
    {
      Serial.printf("⚠️ %s NOT found, creating default...\n", defaultFiles[i]);
      File f = LittleFS.open(defaultFiles[i], "w");
      if (f)
      {
        if (strcmp(defaultFiles[i], LITTLEFS_KOTA) == 0)
          f.print("{\"id\":0,\"nama\":\"\"}");
        else if (strcmp(defaultFiles[i], LITTLEFS_JADWAL) == 0)
          f.print("{\"data\":{\"id\":0,\"lokasi\":\"\",\"daerah\":\"\",\"jadwal\":[]}}");
        else if (strcmp(defaultFiles[i], LITTLEFS_CONFIG) == 0)
          f.print("{\"ssid\":\"\",\"password\":\"\",\"volume\":25,\"buzzerEnabled\":true,\"lcdMode\":0}");
        f.close();
      }
    }
  }

  // ===== Inisialisasi I2C =====
  Serial.println("Initializing I2C buses...");
  Wire.begin(LCD_SDA, LCD_SCL, 100000);
  delay(100);
  I2CRTC.begin(RTC_SDA, RTC_SCL, 100000);
  delay(100);

  // ===== LCD =====
  Serial.println("Initializing LCD...");
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");
  Serial.println("✅ LCD initialized");

  // ===== Load config =====
  loadConfig();

  // ===== RTC =====
  Serial.println("Initializing RTC...");
  rtcAvailable = rtc.begin(&I2CRTC);

  if (rtcAvailable)
  {
    Serial.println("✅ RTC berhasil diinisialisasi");

    // Inisialisasi EEPROM
    EEPROM.begin(EEPROM_SIZE);

    // Cek apakah RTC kehilangan daya (baterai lemah / dilepas)
    if (rtc.lostPower())
    {
      Serial.println("⚠️ RTC kehilangan daya!");

      // Coba pulihkan waktu dari cadangan EEPROM
      if (restoreTimeFromEEPROM())
      {
        Serial.println("✅ Waktu berhasil dipulihkan dari cadangan EEPROM");

        // Ambil waktu hasil pemulihan
        DateTime restoredTime = rtc.now();
        String timeStr = getFormattedDate(restoredTime) + " " +
                         getFormattedTime(restoredTime);

        Serial.printf("Waktu dipulihkan: %s\n", timeStr.c_str());

        // Tampilkan informasi di LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Waktu Dipulihkan   ");
        lcd.setCursor(0, 1);
        lcd.print("Dari EEPROM        ");
        lcd.setCursor(0, 2);
        lcd.print(truncateText(timeStr, 20));
        delay(3000);
      }
      else
      {
        // Jika EEPROM gagal, coba sinkronisasi dari NTP
        bool berhasilNTP = false;

        if (WiFi.status() == WL_CONNECTED)
        {
          Serial.println("🔄 Mencoba sinkronisasi waktu dari NTP...");
          if (syncRTCwithNTP())
          {
            berhasilNTP = true;
            Serial.println("✅ Waktu RTC berhasil dipulihkan dari NTP");
          }
        }

        // Jika NTP juga gagal, gunakan waktu kompilasi sebagai cadangan terakhir
        if (!berhasilNTP)
        {
          Serial.println("⚠️ Menggunakan waktu kompilasi sebagai cadangan");
          rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

          // Simpan waktu ke EEPROM
          backupTimeToEEPROM();

          // Tampilkan peringatan di LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("BATERAI RTC LEMAH! ");
          lcd.setCursor(0, 1);
          lcd.print("Pakai waktu compile");
          lcd.setCursor(0, 2);
          lcd.print("Hubungkan WiFi utk ");
          lcd.setCursor(0, 3);
          lcd.print("sinkron waktu      ");
          delay(3000);
        }
      }
    }
    else
    {
      Serial.println("✅ Baterai RTC normal, waktu tersimpan");

      // Ambil waktu dari RTC
      DateTime now = rtc.now();

      // Validasi tahun RTC
      if (now.year() >= 2023 && now.year() <= 2030)
      {
        Serial.println("✅ Waktu RTC valid");

        String timeStr = getFormattedDate(now) + " " +
                         getFormattedTime(now);

        Serial.printf("Waktu RTC tersimpan: %s\n", timeStr.c_str());

        // Tampilkan info singkat di LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Waktu RTC Aktif    ");
        lcd.setCursor(0, 1);
        lcd.print("Dari Baterai RTC   ");
        lcd.setCursor(0, 2);
        lcd.print(truncateText(timeStr, 20));
        delay(2000);
      }
      else
      {
        Serial.println("⚠️ Waktu RTC tidak valid, mencoba sinkron NTP...");
        if (WiFi.status() == WL_CONNECTED)
        {
          syncRTCwithNTP();
        }
      }
    }

    // Cek status baterai RTC (monitor tambahan)
    checkRTCBattery();
  }
  else
  {
    Serial.println("❌ RTC tidak terdeteksi!");

    // Tampilkan pesan error di LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC TIDAK TERBACA! ");
    lcd.setCursor(0, 1);
    lcd.print("Periksa koneksi    ");
    lcd.setCursor(0, 2);
    lcd.print("Tanpa RTC, waktu   ");
    lcd.setCursor(0, 3);
    lcd.print("akan reset ulang   ");
    delay(3000);
  }

  // ===== Setup WiFi =====
  setupWiFi();

  // ===== DFPlayer =====
  Serial.println("Initializing DFPlayer...");
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  delay(500);

  dfPlayerReady = dfPlayer.begin(dfSerial);
  if (!dfPlayerReady)
  {
    Serial.println("❌ DFPlayer not detected!");
  }
  else
  {
    Serial.println("✅ DFPlayer ready");
    dfPlayer.volume(dfVolume);
    dfPlayer.stop();
  }

  // ===== Load kota & jadwal =====
  loadKota();
  loadJadwal();

  // ===== HTTP Server =====
  handleAPI();

  server.on("/", HTTP_GET, []()
            {
    if (LittleFS.exists("/index.html")) {
      File f = LittleFS.open("/index.html", "r");
      server.streamFile(f, "text/html");
      f.close();
    } else {
      server.send(200, "text/plain", "Web interface not found");
    } });

  server.onNotFound([]()
                    {
    String path = server.uri();
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      if (path.endsWith(".css"))
        server.streamFile(f, "text/css");
      else if (path.endsWith(".js"))
        server.streamFile(f, "application/javascript");
      else
        server.streamFile(f, "text/plain");
      f.close();
    } else {
      server.send(404, "text/plain", "File not found");
    } });

  server.begin();
  Serial.println("✅ HTTP server started on port 80");

  delay(2000);
  lcd.clear();
  Serial.printf("Free heap akhir: %d\n", ESP.getFreeHeap());
}

// ===== LOOP =====
void loop()
{
  static unsigned long lastLoopTime = 0;
  static unsigned long lastSecondUpdate = 0;

  if (millis() - lastLoopTime < 10)
  {
    delay(1);
    return;
  }
  lastLoopTime = millis();

  server.handleClient();
  buttonHandler();

  if (WiFi.status() == WL_CONNECTED)
  {
    handleTelegram();

    // === CEK PERIODIC NTP SYNC ===
    if (rtcAvailable && millis() - lastNTPSync > NTP_SYNC_INTERVAL)
    {
      Serial.println("🔄 Waktunya sinkronisasi NTP periodic...");
      if (syncRTCwithNTP())
      {
        lastNTPSync = millis();
      }
    }
  }

  // Backup waktu ke EEPROM secara periodic
  if (rtcAvailable && millis() - lastBackupTime >= BACKUP_INTERVAL)
  {
    backupTimeToEEPROM();
    lastBackupTime = millis();
  }

  // Cek apakah masih dalam periode tampilkan error password
  if (showingPasswordError || wifiPasswordError)
  {
    if (millis() - passwordErrorStart >= PASSWORD_ERROR_DISPLAY_TIME)
    {
      showingPasswordError = false;
      wifiPasswordError = false;
      lcdLocked = false;
      lastStatus = -1; // Force LCD update
    }
    else
    {
      // Tetap tampilkan error password
      return;
    }
  }

  // ===== Tentukan status =====
  int currentStatus = determineStatus();

  // ===== LCD terkunci sementara =====
  if (lcdLocked && millis() > lcdLockUntil)
  {
    lcdLocked = false;
    lastLine0 = "";
    lastLine1 = "";
    lastLine2 = "";
    lastLine3 = "";
    lastStatus = -1;
  }

  // ===== Update LCD hanya jika status berubah =====
  if (currentStatus != lastStatus && !lcdLocked)
  {
    lcd.clear();
    lastLine0 = "";
    lastLine1 = "";
    lastLine2 = "";
    lastLine3 = "";

    if (currentStatus == 2 || currentStatus == 3 || currentStatus == 4)
    {
      // Status 2, 3, atau 4: tampilkan jadwal dengan info WiFi
      lastStatus = currentStatus;
      // Panggil updateLCD() segera untuk menampilkan waktu tanpa menunggu 1 detik
      updateLCDNow();
    }
    else
    {
      // Status lainnya: tampilkan pesan status
      displayStatusMessage(currentStatus);
      lastStatus = currentStatus;
    }
  }

  // ===== Update detik setiap 1 detik untuk status 2, 3 dan 4 =====
  if (millis() - lastSecondUpdate >= 1000 && !lcdLocked)
  {
    lastSecondUpdate = millis();
    if (currentStatus == 2 || currentStatus == 3 || currentStatus == 4)
    {
      updateLCD();
    }
  }

  // ===== Cek adzan setiap menit =====
  if (millis() - lastMinuteCheck >= 60000)
  {
    lastMinuteCheck = millis();

    if (isKotaConfigured() && isJadwalAvailable() && (currentStatus == 3 || currentStatus == 4))
    {
      JadwalToday jt = getTodayJadwal();
      if (shouldTrigger(jt.subuh))
        playAdzan(0);
      if (shouldTrigger(jt.dzuhur))
        playAdzan(1);
      if (shouldTrigger(jt.ashar))
        playAdzan(2);
      if (shouldTrigger(jt.maghrib))
        playAdzan(3);
      if (shouldTrigger(jt.isya))
        playAdzan(4);
    }
  }
}

// ===== FUNGSI UPDATE LCD LANGSUNG =====
void updateLCDNow()
{
  if (lcdLocked || showingPasswordError || wifiPasswordError)
    return;

  // Dapatkan waktu dari RTC atau sistem
  DateTime now;
  bool hasTime = false;

  if (rtcAvailable)
  {
    now = rtc.now();
    if (now.year() >= 2023)
    {
      hasTime = true;
    }
  }

  if (!hasTime)
  {
    updateLCDLine(0, "Waktu belum sync   ", lastLine0);
    updateLCDLine(1, "                   ", lastLine1);
    updateLCDLine(2, "                   ", lastLine2);
    updateLCDLine(3, "                   ", lastLine3);
    return;
  }

  // Format tanggal & jam
  String dateStr = getFormattedDate(now);
  String timeStr = getFormattedTime(now);

  // Baris 0: Kota (dipotong) + status WiFi
  String kotaDisplay = truncateText(kotaNama, 15);
  String wifiStatus = (WiFi.status() == WL_CONNECTED) ? "✓" : "✗";
  String line0 = "K:" + kotaDisplay + " W:" + wifiStatus;
  updateLCDLine(0, line0, lastLine0);

  // Baris 1: Tanggal
  String line1 = "Tgl:" + dateStr;
  updateLCDLine(1, line1, lastLine1);

  // Baris 2: Jam dengan detik - SELALU UPDATE
  // Tidak menggunakan updateLCDLine untuk baris ini agar detik selalu terupdate
  lcd.setCursor(0, 2);
  String timeDisplay = "Jam:" + timeStr;
  lcd.print(timeDisplay);
  for (int i = timeDisplay.length(); i < 20; i++)
  {
    lcd.print(" ");
  }
  lastLine2 = timeDisplay;

  // Baris 3: Jadwal sholat (2 waktu)
  if (isJadwalAvailable())
  {
    JadwalToday jt = getTodayJadwal();
    String listSholat[5] = {jt.subuh, jt.dzuhur, jt.ashar, jt.maghrib, jt.isya};
    String names[5] = {"Sbh", "Dzr", "Ash", "Mgh", "Isy"};

    // Cari sholat berikutnya
    int nextIdx = 0;
    for (int i = 0; i < 5; i++)
    {
      if (listSholat[i].length() >= 5)
      {
        int h = listSholat[i].substring(0, 2).toInt();
        int m = listSholat[i].substring(3, 5).toInt();
        if ((now.hour() < h) || (now.hour() == h && now.minute() < m))
        {
          nextIdx = i;
          break;
        }
      }
      if (i == 4)
        nextIdx = 0;
    }

    int lastIdx = (nextIdx == 0) ? 4 : nextIdx - 1;

    String jadwalLine = "";
    if (lastIdx >= 0 && lastIdx < 5 && nextIdx >= 0 && nextIdx < 5)
    {
      if (listSholat[lastIdx].length() >= 5 && listSholat[nextIdx].length() >= 5)
      {
        // Format: "S:04:30 D:12:15"
        jadwalLine = names[lastIdx] + ":" + listSholat[lastIdx] + " " +
                     names[nextIdx] + ":" + listSholat[nextIdx];
      }
    }

    updateLCDLine(3, truncateText(jadwalLine, 20), lastLine3);
  }
  else
  {
    updateLCDLine(3, "Jadwal tidak ada   ", lastLine3);
  }
}

// ===== FUNGSI UPDATE LCD NORMAL =====
void updateLCD()
{
  if (lcdLocked || showingPasswordError || wifiPasswordError)
    return;

  // Dapatkan waktu dari RTC atau sistem
  DateTime now;
  bool hasTime = false;

  if (rtcAvailable)
  {
    now = rtc.now();
    if (now.year() >= 2023)
    {
      hasTime = true;
    }
  }

  if (!hasTime)
  {
    updateLCDLine(0, "Waktu belum sync   ", lastLine0);
    updateLCDLine(1, "                   ", lastLine1);
    updateLCDLine(2, "                   ", lastLine2);
    updateLCDLine(3, "                   ", lastLine3);
    return;
  }

  // Format tanggal & jam
  String dateStr = getFormattedDate(now);
  String timeStr = getFormattedTime(now);

  // Baris 0: Kota (dipotong) + status WiFi
  String line0 = "Kota:" + kotaNama;
  updateLCDLine(0, line0, lastLine0);

  // Baris 1: Tanggal
  String line1 = "Tgl:" + dateStr;
  updateLCDLine(1, line1, lastLine1);

  // Baris 2: Jam dengan detik - SELALU UPDATE
  // Tidak menggunakan updateLCDLine untuk baris ini agar detik selalu terupdate
  lcd.setCursor(0, 2);
  String timeDisplay = "Jam:" + timeStr;
  lcd.print(timeDisplay);
  for (int i = timeDisplay.length(); i < 20; i++)
  {
    lcd.print(" ");
  }
  lastLine2 = timeDisplay;

  // Baris 3: Jadwal sholat (2 waktu)
  if (isJadwalAvailable())
  {
    JadwalToday jt = getTodayJadwal();
    String listSholat[5] = {jt.subuh, jt.dzuhur, jt.ashar, jt.maghrib, jt.isya};
    String names[5] = {"Sbh", "Dzr", "Ash", "Mgh", "Isy"};

    // Cari sholat berikutnya
    int nextIdx = 0;
    for (int i = 0; i < 5; i++)
    {
      if (listSholat[i].length() >= 5)
      {
        int h = listSholat[i].substring(0, 2).toInt();
        int m = listSholat[i].substring(3, 5).toInt();
        if ((now.hour() < h) || (now.hour() == h && now.minute() < m))
        {
          nextIdx = i;
          break;
        }
      }
      if (i == 4)
        nextIdx = 0;
    }

    int lastIdx = (nextIdx == 0) ? 4 : nextIdx - 1;

    String jadwalLine = "";
    if (lastIdx >= 0 && lastIdx < 5 && nextIdx >= 0 && nextIdx < 5)
    {
      if (listSholat[lastIdx].length() >= 5 && listSholat[nextIdx].length() >= 5)
      {
        // Format: "S:04:30 D:12:15"
        jadwalLine = names[lastIdx] + ":" + listSholat[lastIdx] + " " +
                     names[nextIdx] + ":" + listSholat[nextIdx];
      }
    }

    updateLCDLine(3, truncateText(jadwalLine, 20), lastLine3);
  }
  else
  {
    updateLCDLine(3, "Jadwal tidak ada   ", lastLine3);
  }
}

// ===== FUNGSI TAMPILKAN PESAN STATUS =====
void displayStatusMessage(int status)
{
  lcd.clear();

  switch (status)
  {
  case 0:
    updateLCDLine(0, "Hubungkan WiFi     ", lastLine0);
    updateLCDLine(1, "Buka di browser    ", lastLine1);
    updateLCDLine(2, "IP AP: " + truncateText(WiFi.softAPIP().toString(), 13), lastLine2);
    updateLCDLine(3, "Set Kota & WiFi    ", lastLine3);
    break;

  case 1:
    updateLCDLine(0, "WiFi Terhubung     ", lastLine0);
    updateLCDLine(1, "Set Kota via IP    ", lastLine1);
    updateLCDLine(2, truncateText(WiFi.localIP().toString(), 20), lastLine2);
    updateLCDLine(3, "Buka browser       ", lastLine3);
    break;

  case 2:
    // Status 2 sekarang menampilkan waktu seperti status 3 dan 4
    updateLCDNow();
    break;

  default:
    break;
  }
}

// ===== FUNGSI TENTUKAN STATUS =====
int determineStatus()
{
  // Status 0: Belum ada SSID
  if (wifiSSID.length() == 0)
  {
    return 0;
  }

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  bool kotaDiset = isKotaConfigured();
  bool jadwalAda = isJadwalAvailable();

  // Status 1: Ada SSID, WiFi terhubung, kota belum diset
  if (wifiConnected && !kotaDiset)
  {
    return 1;
  }

  // Status 2: Ada SSID, WiFi terhubung, kota sudah diset, jadwal kosong
  if (wifiConnected && kotaDiset && !jadwalAda)
  {
    return 2;
  }

  // Status 3: Ada SSID, WiFi terhubung, kota sudah diset, jadwal ada
  if (wifiConnected && kotaDiset && jadwalAda)
  {
    // Cek apakah RTC perlu sinkronisasi
    if (rtcAvailable)
    {
      DateTime now = rtc.now();
      if (now.year() < 2023)
      {
        // Waktu RTC tidak valid
        return 2; // Kembali ke status 2 untuk menunjukkan perlu update
      }
    }
    return 3;
  }

  // Status 4: Ada SSID, WiFi TIDAK terhubung, kota sudah diset, jadwal ada
  if (!wifiConnected && kotaDiset && jadwalAda)
  {
    // Cek apakah RTC perlu sinkronisasi
    if (rtcAvailable)
    {
      DateTime now = rtc.now();
      if (now.year() < 2023)
      {
        // Waktu RTC tidak valid
        return 2; // Kembali ke status 2 untuk menunjukkan perlu WiFi
      }
    }
    return 4;
  }

  // Default: kembali ke status 0
  return 0;
}

// ===== SETUP WIFI =====
void setupWiFi()
{
  lcd.clear();

  // Mode AP_STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true, true);
  delay(500);

  // ===== KONFIGURASI AP =====
  WiFi.softAP("ESP32-JadwalSholat", "12345678", 1, 0, 4);

  // Konfigurasi IP
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  Serial.println("========================================");
  Serial.println("ESP32 Access Point Configuration:");
  Serial.println("========================================");
  Serial.printf("AP SSID: ESP32-JadwalSholat\n");
  Serial.printf("AP Password: 12345678\n");
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("========================================");

  // Tampilkan di LCD
  lcdLocked = true;
  lcdLockUntil = millis() + 3000;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AP Mode Aktif      ");
  lcd.setCursor(0, 1);
  lcd.print("SSID:ESP32-Jadwal- ");
  lcd.setCursor(0, 2);
  lcd.print("Sholat             ");
  lcd.setCursor(0, 3);
  lcd.print("Buka IP di browser ");

  delay(2000);

  // ===== COBA KONEK KE WIFI JIKA ADA SSID =====
  if (wifiSSID.length() > 0)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Menyambung WiFi   ");
    lcd.setCursor(0, 1);

    String ssidDisplay = truncateText(wifiSSID, 16);
    lcd.print("Ke: " + ssidDisplay);

    Serial.printf("\nMencoba koneksi ke: %s\n", wifiSSID.c_str());

    // Koneksi dengan timeout
    Serial.println("Menghubungkan ke WiFi...");
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    unsigned long startTime = millis();
    bool connected = false;

    while (millis() - startTime < 10000)
    { // 10 detik timeout
      if (WiFi.status() == WL_CONNECTED)
      {
        connected = true;
        break;
      }
      delay(500);
    }

    if (connected)
    {
      Serial.printf("✅ Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());

      // === SINCRONISASI NTP SETELAH WIFI TERHUBUNG ===
      if (rtcAvailable)
      {
        Serial.println("🔄 Cek sinkronisasi NTP...");

        // Coba sinkronisasi hingga 3 kali
        bool ntpSynced = false;
        for (int i = 0; i < 3; i++)
        {
          Serial.printf("Percobaan sinkronisasi NTP ke-%d...\n", i + 1);
          if (syncRTCwithNTP())
          {
            ntpSynced = true;
            break;
          }
          delay(3000);
        }

        if (ntpSynced)
        {
          lastNTPSync = millis();
          Serial.println("✅ NTP sinkronisasi berhasil");
        }
        else
        {
          Serial.println("⚠️ NTP sinkronisasi gagal, coba lagi nanti");
        }
      }
      // === END SINCRONISASI ===

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Terhubung    ");
      lcd.setCursor(0, 1);
      lcd.print("IP: " + truncateText(WiFi.localIP().toString(), 16));
      lcd.setCursor(0, 2);
      lcd.print("RSSI: " + String(WiFi.RSSI()) + " dBm");
      delay(2000);
    }
    else
    {
      // Cek apakah error karena password
      int status = WiFi.status();
      if (status == WL_CONNECT_FAILED)
      {
        displayPasswordError();
      }
      else
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Gagal Koneksi     ");
        lcd.setCursor(0, 1);
        lcd.print("Timeout koneksi   ");
        lcd.setCursor(0, 2);
        lcd.print("Coba lagi nanti   ");
        delay(3000);
      }
      WiFi.disconnect(true);
    }
  }
}

// ===== FUNGSI LOAD CONFIG =====
void loadConfig()
{
  Serial.println("Loading config...");

  if (!LittleFS.exists(LITTLEFS_CONFIG))
  {
    Serial.println("Config file not found, using defaults");
    return;
  }

  File f = LittleFS.open(LITTLEFS_CONFIG, "r");
  if (!f)
  {
    Serial.println("Failed to open config.json");
    return;
  }

  DeserializationError err = deserializeJson(configDoc, f);
  f.close();

  if (err)
  {
    Serial.print("Failed to parse config.json: ");
    Serial.println(err.c_str());
    return;
  }

  dfVolume = configDoc["volume"] | 25;
  buzzerEnabled = configDoc["buzzerEnabled"] | true;
  lcdMode = configDoc["lcdMode"] | 0;

  const char *ssid = configDoc["ssid"];
  wifiSSID = ssid ? String(ssid) : "";

  const char *password = configDoc["password"];
  wifiPassword = password ? String(password) : "";

  Serial.println("Config loaded:");
  Serial.printf(" - volume: %d\n", dfVolume);
  Serial.printf(" - buzzerEnabled: %s\n", buzzerEnabled ? "true" : "false");
  Serial.printf(" - lcdMode: %d\n", lcdMode);
  Serial.printf(" - ssid: %s\n", wifiSSID.c_str());
}

void saveConfig()
{
  File f = LittleFS.open(LITTLEFS_CONFIG, "w");
  if (!f)
  {
    Serial.println("Failed to write config.json");
    return;
  }

  DynamicJsonDocument doc(256);
  doc["volume"] = dfVolume;
  doc["buzzerEnabled"] = buzzerEnabled;
  doc["lcdMode"] = lcdMode;
  doc["ssid"] = wifiSSID;
  doc["password"] = wifiPassword;

  serializeJson(doc, f);
  f.close();
  Serial.println("Config saved");
}

bool loadKota()
{
  if (!LittleFS.exists(LITTLEFS_KOTA))
  {
    Serial.println("kota.json not found");
    return false;
  }

  File f = LittleFS.open(LITTLEFS_KOTA, "r");
  if (!f)
  {
    Serial.println("Failed to open kota.json");
    return false;
  }

  DeserializationError err = deserializeJson(kotaDoc, f);
  f.close();

  if (err)
  {
    Serial.print("Failed to parse kota.json: ");
    Serial.println(err.c_str());
    return false;
  }

  kotaID = kotaDoc["id"] | 0;
  const char *nama = kotaDoc["nama"];
  kotaNama = nama ? String(nama) : "";
  kotaIDStr = String(kotaID);

  Serial.printf("Kota loaded: %d - %s\n", kotaID, kotaNama.c_str());
  return kotaID > 0;
}

bool saveKota(int id, const String &nama)
{
  DynamicJsonDocument doc(256);
  doc["id"] = id;
  doc["nama"] = nama;

  File f = LittleFS.open(LITTLEFS_KOTA, "w");
  if (!f)
  {
    Serial.println("Failed to save kota.json");
    return false;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println("kota.json saved");
  return true;
}

bool loadJadwal()
{
  if (!LittleFS.exists(LITTLEFS_JADWAL))
  {
    Serial.println("jadwal.json not found");
    return false;
  }

  File f = LittleFS.open(LITTLEFS_JADWAL, "r");
  if (!f)
  {
    Serial.println("Failed to open jadwal.json");
    return false;
  }

  size_t size = f.size();
  if (size == 0)
  {
    f.close();
    Serial.println("jadwal.json is empty");
    return false;
  }

  DeserializationError err = deserializeJson(jadwalDoc, f);
  f.close();

  if (err)
  {
    Serial.print("Failed to parse jadwal.json: ");
    Serial.println(err.c_str());
    return false;
  }

  Serial.println("jadwal.json loaded");
  return true;
}

bool saveJadwal(const String &json)
{
  File f = LittleFS.open(LITTLEFS_JADWAL, "w");
  if (!f)
  {
    Serial.println("Failed to save jadwal.json");
    return false;
  }
  f.print(json);
  f.close();
  Serial.println("jadwal.json saved");
  return true;
}

void sendTelegramMessage(const String &message)
{
  if (!telegramEnabled || WiFi.status() != WL_CONNECTED)
    return;

  secured_client.setInsecure();
  if (bot.sendMessage(CHAT_ID, message, ""))
  {
    Serial.println("Telegram message sent");
  }
  else
  {
    Serial.println("Failed to send Telegram message");
  }
}

void handleTelegram()
{
  if (millis() - lastTelegramCheck < 1000)
    return;
  lastTelegramCheck = millis();

  if (WiFi.status() != WL_CONNECTED)
    return;

  secured_client.setInsecure();
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages)
  {
    for (int i = 0; i < numNewMessages; i++)
    {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;

      if (chat_id != CHAT_ID)
      {
        bot.sendMessage(chat_id, "Unauthorized", "");
        continue;
      }

      if (text == "/start")
      {
        String welcome = "Selamat datang di Bot Jadwal Sholat!\n\n";
        welcome += "Perintah yang tersedia:\n";
        welcome += "/status - Status perangkat\n";
        welcome += "/jadwal - Jadwal sholat hari ini\n";
        welcome += "/kota - Kota saat ini\n";
        welcome += "/waktu - Waktu saat ini\n";
        welcome += "/help - Menampilkan bantuan";
        bot.sendMessage(chat_id, welcome, "");
      }
      else if (text == "/status")
      {
        String status = "📱 Status Perangkat:\n";
        status += "WiFi: " + WiFi.SSID() + "\n";
        status += "IP: " + WiFi.localIP().toString() + "\n";
        status += "Kota: " + kotaNama + "\n";
        status += "Volume: " + String(dfVolume) + "\n";
        status += "Buzzer: " + String(buzzerEnabled ? "Aktif" : "Nonaktif") + "\n";
        status += "RTC: " + String(rtcAvailable ? "OK" : "Gagal");
        bot.sendMessage(chat_id, status, "");
      }
      else if (text == "/jadwal")
      {
        if (isJadwalAvailable())
        {
          JadwalToday jt = getTodayJadwal();
          String jadwal = "🕌 Jadwal Sholat Hari Ini:\n\n";
          jadwal += "Subuh   : " + jt.subuh + "\n";
          jadwal += "Dzuhur  : " + jt.dzuhur + "\n";
          jadwal += "Ashar   : " + jt.ashar + "\n";
          jadwal += "Maghrib : " + jt.maghrib + "\n";
          jadwal += "Isya    : " + jt.isya;
          bot.sendMessage(chat_id, jadwal, "");
        }
        else
        {
          bot.sendMessage(chat_id, "Jadwal belum tersedia", "");
        }
      }
      else if (text == "/kota")
      {
        bot.sendMessage(chat_id, "Kota saat ini: " + kotaNama, "");
      }
      else if (text == "/waktu")
      {
        if (rtcAvailable)
        {
          DateTime now = rtc.now();
          String waktu = String(now.hour()) + ":" +
                         String(now.minute()) + ":" +
                         String(now.second());
          bot.sendMessage(chat_id, "Waktu RTC: " + waktu, "");
        }
        else
        {
          bot.sendMessage(chat_id, "RTC tidak tersedia", "");
        }
      }
      else if (text == "/help")
      {
        String help = "ℹ️ Perintah Bot:\n";
        help += "/start - Memulai bot\n";
        help += "/status - Status perangkat\n";
        help += "/jadwal - Jadwal sholat hari ini\n";
        help += "/kota - Kota saat ini\n";
        help += "/waktu - Waktu saat ini\n";
        help += "/help - Menampilkan bantuan";
        bot.sendMessage(chat_id, help, "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

void playAdzan(int index)
{
  if (index < 0 || index > 4)
    return;

  JadwalToday jt = getTodayJadwal();
  String jam;

  switch (index)
  {
  case 0:
    jam = jt.subuh;
    break;
  case 1:
    jam = jt.dzuhur;
    break;
  case 2:
    jam = jt.ashar;
    break;
  case 3:
    jam = jt.maghrib;
    break;
  case 4:
    jam = jt.isya;
    break;
  default:
    return;
  }

  if (jam.isEmpty())
    return;

  // Buzzer 3x
  if (buzzerEnabled)
  {
    for (int i = 0; i < 3; i++)
    {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(150);
      digitalWrite(BUZZER_PIN, LOW);
      delay(150);
    }
  }

  // Kirim notifikasi Telegram jika terhubung
  if (WiFi.status() == WL_CONNECTED)
  {
    String telegramMsg = "🕌 Waktu Sholat " + String(namaSholat[index]) + " telah tiba\n";
    telegramMsg += "⏰ Jam: " + jam + " WIB\n";
    telegramMsg += "📍 Kota: " + kotaNama;
    sendTelegramMessage(telegramMsg);
  }

  // Lock LCD
  lcdLocked = true;
  lcdLockUntil = millis() + 60000;

  // Tampilkan di LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WAKTU SHOLAT       ");
  lcd.setCursor(0, 1);
  String sholatDisplay = truncateText(String(namaSholat[index]) + " " + jam, 20);
  lcd.print(sholatDisplay);
  lcd.setCursor(0, 2);
  lcd.print("Adzan Berlangsung  ");
  lcd.setCursor(0, 3);
  lcd.print("Segera Tunaikan    ");

  // Play audio hanya jika DFPlayer siap
  if (dfPlayerReady)
  {
    dfPlayer.volume(dfVolume);
    dfPlayer.play(index + 1);
  }
}

bool shouldTrigger(const String &timeHHMM)
{
  if (timeHHMM.length() != 5)
    return false;

  static bool adzanTriggered[5] = {false, false, false, false, false};
  static int lastDay = -1;

  if (!rtcAvailable)
    return false;

  DateTime now = rtc.now();
  if (now.day() != lastDay)
  {
    lastDay = now.day();
    for (int i = 0; i < 5; i++)
      adzanTriggered[i] = false;
  }

  int idx = -1;
  JadwalToday jt = getTodayJadwal();
  if (timeHHMM == jt.subuh)
    idx = 0;
  else if (timeHHMM == jt.dzuhur)
    idx = 1;
  else if (timeHHMM == jt.ashar)
    idx = 2;
  else if (timeHHMM == jt.maghrib)
    idx = 3;
  else if (timeHHMM == jt.isya)
    idx = 4;

  if (idx == -1)
    return false;
  if (adzanTriggered[idx])
    return false;

  int h = timeHHMM.substring(0, 2).toInt();
  int m = timeHHMM.substring(3, 5).toInt();

  if (now.hour() == h && now.minute() == m)
  {
    adzanTriggered[idx] = true;
    return true;
  }

  return false;
}

void buttonHandler()
{
  bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed && !buttonDown)
  {
    buttonDown = true;
    buttonPressStart = millis();
  }
  else if (!pressed && buttonDown)
  {
    buttonDown = false;
    unsigned long duration = millis() - buttonPressStart;
    if (duration >= LONG_PRESS_MS)
    {
      Serial.println("Button long-press -> Restarting WiFi config");
      wifiSSID = "";
      wifiPassword = "";
      saveConfig();
      ESP.restart();
    }
    else
    {
      lcdMode = (lcdMode + 1) % 3;
      Serial.printf("Button short-press -> lcdMode = %d\n", lcdMode);
    }
  }
}

String getTodayKey()
{
  DateTime now;

  if (rtcAvailable)
  {
    now = rtc.now();
  }
  else
  {
    time_t t = time(nullptr);
    if (t > 0)
    {
      now = DateTime(t);
    }
    else
    {
      return "";
    }
  }

  char buf[11];
  sprintf(buf, "%04d-%02d-%02d", now.year(), now.month(), now.day());
  return String(buf);
}

String getTanggalHariIni()
{
  return getTodayKey();
}

JadwalToday getTodayJadwal()
{
  JadwalToday jt;
  jt.subuh = jt.dzuhur = jt.ashar = jt.maghrib = jt.isya = "";

  if (!jadwalDoc.containsKey("data") || !jadwalDoc["data"].containsKey("jadwal"))
  {
    return jt;
  }

  String todayKey = getTodayKey();
  if (todayKey.isEmpty())
    return jt;

  JsonArray jadwalArr = jadwalDoc["data"]["jadwal"].as<JsonArray>();

  for (JsonObject day : jadwalArr)
  {
    const char *dateStr = day["date"];
    if (!dateStr)
      continue;

    if (String(dateStr) == todayKey)
    {
      jt.subuh = day["subuh"].as<String>();
      jt.dzuhur = day["dzuhur"].as<String>();
      jt.ashar = day["ashar"].as<String>();
      jt.maghrib = day["maghrib"].as<String>();
      jt.isya = day["isya"].as<String>();
      break;
    }
  }

  return jt;
}

String padKotaID(String id)
{
  String cleanId = "";
  for (unsigned int i = 0; i < id.length(); i++)
  {
    if (isdigit(id.charAt(i)))
    {
      cleanId += id.charAt(i);
    }
  }

  while (cleanId.length() < 4)
  {
    cleanId = "0" + cleanId;
  }

  return cleanId;
}

// ===== API HANDLERS =====
void handleAPI()
{
  // Status perangkat
  server.on("/api/status", HTTP_GET, []()
            {
    DateTime now;
    bool waktuTersedia = false;

    if (rtcAvailable) {
      now = rtc.now();
      if (!rtc.lostPower() && now.year() >= 2023) {
        waktuTersedia = true;
      }
    }

    char waktu[9];
    if (waktuTersedia) {
      sprintf(waktu, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    } else {
      strcpy(waktu, "--:--:--");
    }

    String json = "{";
    json += "\"status\":\"ok\",";
    json += "\"wifi\":\"" + WiFi.SSID() + "\",";
    json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"kota\":\"" + kotaNama + "\",";
    json += "\"waktu\":\"" + String(waktu) + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"jadwal_available\":" + String(isJadwalAvailable() ? "true" : "false");
    json += "}";
    
    server.send(200, "application/json", json); });

  // Jadwal hari ini
  server.on("/api/jadwal_today", HTTP_GET, []()
            {
    if (!LittleFS.exists("/jadwal.json")) {
      server.send(404, "application/json", "{\"error\":\"jadwal belum ada\"}");
      return;
    }

    File f = LittleFS.open("/jadwal.json", "r");
    DynamicJsonDocument doc(32768);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    
    if (err) {
      server.send(500, "application/json", "{\"error\":\"json rusak\"}");
      return;
    }

    String today = getTanggalHariIni();
    if (today.isEmpty()) {
      server.send(500, "application/json", "{\"error\":\"waktu belum valid\"}");
      return;
    }

    JsonArray jadwalArr = doc["data"]["jadwal"].as<JsonArray>();
    
    if (jadwalArr.isNull()) {
      server.send(500, "application/json", "{\"error\":\"struktur jadwal tidak valid\"}");
      return;
    }

    bool found = false;
    for (JsonObject day : jadwalArr) {
      const char* dateStr = day["date"];
      if (!dateStr) continue;

      if (String(dateStr) == today) {
        DynamicJsonDocument out(512);
        out["imsak"]   = day["imsak"] | "";
        out["subuh"]   = day["subuh"] | "";
        out["dzuhur"]  = day["dzuhur"] | "";
        out["ashar"]   = day["ashar"] | "";
        out["maghrib"] = day["maghrib"] | "";
        out["isya"]    = day["isya"] | "";

        String res;
        serializeJson(out, res);
        server.send(200, "application/json", res);
        found = true;
        break;
      }
    }

    if (!found) {
      server.send(404, "application/json", "{\"error\":\"jadwal hari ini tidak ditemukan\"}");
    } });

  // Search kota
  server.on("/api/search_kota", HTTP_GET, []()
            {
    if (!server.hasArg("kota")) {
      server.send(400, "application/json", "{\"error\":\"Parameter 'kota' dibutuhkan\"}");
      return;
    }

    String query = server.arg("kota");
    query.replace(" ", "%20");

    HTTPClient http;
    String apiURL = "https://api.myquran.com/v2/sholat/kota/cari/" + query;

    http.begin(apiURL);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      server.send(200, "application/json", payload);
    } else {
      server.send(500, "application/json", "{\"error\":\"Gagal menghubungi API\"}");
    }
    http.end(); });

  // Set kota
  server.on("/api/set_kota", HTTP_POST, []()
            {
    Serial.println("[API] /api/set_kota called");
    
    if (!server.hasArg("plain") || server.arg("plain").isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Body kosong\"}");
      return;
    }

    String body = server.arg("plain");
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, body);
    
    if (err) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"JSON tidak valid\"}");
      return;
    }

    String idRaw = doc["id"].as<String>();
    String nama = doc["nama"].as<String>();

    if (idRaw.isEmpty() || nama.isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Data kota tidak valid\"}");
      return;
    }

    String id = padKotaID(idRaw);
    
    if (!saveKota(id.toInt(), nama)) {
      server.send(500, "application/json", "{\"success\":false,\"error\":\"Gagal menyimpan kota\"}");
      return;
    }

    kotaID = id.toInt();
    kotaNama = nama;
    kotaIDStr = id;

    // Download jadwal jika WiFi tersambung
    if (WiFi.status() == WL_CONNECTED) {
      int tahun, bulan;
      if (rtcAvailable) {
        DateTime now = rtc.now();
        tahun = now.year();
        bulan = now.month();
      } else {
        time_t t = time(nullptr);
        struct tm *tm_now = localtime(&t);
        tahun = tm_now->tm_year + 1900;
        bulan = tm_now->tm_mon + 1;
      }
      
      char url[160];
      snprintf(url, sizeof(url), "https://api.myquran.com/v2/sholat/jadwal/%s/%04d/%02d",
               kotaIDStr.c_str(), tahun, bulan);
      
      Serial.println("Download jadwal: " + String(url));
      
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.begin(client, url);
      http.setTimeout(5000);
      
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        
        if (saveJadwal(payload)) {
          DeserializationError jerr = deserializeJson(jadwalDoc, payload);
          if (jerr) {
            loadJadwal();
          }
        }
      } else {
        http.end();
      }
    }

    lcdLocked = true;
    lcdLockUntil = millis() + 3000;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Kota disimpan      ");
    lcd.setCursor(0, 1);
    String namaDisplay = truncateText(nama, 20);
    lcd.print(namaDisplay);
    
    String response = "{\"success\":true,\"msg\":\"Kota berhasil disimpan\",";
    response += "\"kota\":{\"id\":\"" + id + "\",\"nama\":\"" + nama + "\"}}";
    
    server.send(200, "application/json", response); });

  // Connect WiFi - DENGAN PENGECEKAN ERROR PASSWORD DI LCD
  server.on("/api/wifi/connect", HTTP_POST, []()
            {
    Serial.println("[API] /api/wifi/connect requested");

    if (!server.hasArg("plain") || server.arg("plain").isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Body kosong\"}");
      return;
    }

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"JSON tidak valid\"}");
      return;
    }

    String ssid = doc["ssid"] | "";
    String password = doc["password"] | "";

    if (ssid.isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID kosong\"}");
      return;
    }

    // Scan jaringan untuk cek apakah SSID ada
    WiFi.scanDelete();
    delay(100);
    int n = WiFi.scanNetworks();
    bool ssidFound = false;
    
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == ssid) {
        ssidFound = true;
        break;
      }
    }
    
    if (!ssidFound) {
      server.send(200, "application/json", 
        "{\"success\":false,\"error\":\"SSID tidak ditemukan\"}");
      return;
    }

    // Coba koneksi
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    bool connected = false;
    String failureReason = "TIMEOUT";
    
    while (millis() - start < 20000) {
      int status = WiFi.status();
      
      if (status == WL_CONNECTED) {
        connected = true;
        break;
      } else if (status == WL_CONNECT_FAILED) {
        failureReason = "PASSWORD";
        break;
      } else if (status == WL_NO_SSID_AVAIL) {
        failureReason = "AP_NOT_FOUND";
        break;
      }
      delay(100);
    }

    if (connected) {
      wifiSSID = ssid;
      wifiPassword = password;
      showingPasswordError = false;
      wifiPasswordError = false;
      saveConfig();

      // === SINCRONISASI NTP SETELAH WIFI TERHUBUNG ===
      if (rtcAvailable) {
        Serial.println("🔄 Cek sinkronisasi NTP setelah koneksi WiFi...");
        
        // Coba sinkronisasi hingga 3 kali
        bool ntpSynced = false;
        for (int i = 0; i < 3; i++) {
          Serial.printf("Percobaan sinkronisasi NTP ke-%d...\n", i+1);
          if (syncRTCwithNTP()) {
            ntpSynced = true;
            break;
          }
          delay(3000);
        }
        
        if (ntpSynced) {
          lastNTPSync = millis();
          Serial.println("✅ NTP sinkronisasi berhasil");
        } else {
          Serial.println("⚠️ NTP sinkronisasi gagal, coba lagi nanti");
        }
      }
      // === END SINCRONISASI ===

      server.send(200, "application/json",
                  "{\"success\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}");
    } else {
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_AP_STA);
      
      String errorMsg = "Gagal koneksi ke WiFi";
      if (failureReason == "PASSWORD") {
        errorMsg = "Password salah";
        // Tampilkan error password WiFi di LCD
        showWifiPasswordError();
      } else if (failureReason == "AP_NOT_FOUND") {
        errorMsg = "SSID tidak ditemukan";
      }
      
      server.send(200, "application/json",
                  "{\"success\":false,\"error\":\"" + errorMsg + "\"}");
    } });

  // Scan WiFi
  server.on("/api/wifi/scan", HTTP_GET, []()
            {
    WiFi.scanDelete();
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    int n = WiFi.scanNetworks();

    if (n <= 0) {
      server.send(200, "application/json", "[]");
    } else {
      String json = "[";
      for (int i = 0; i < n; i++) {
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        if (i < n - 1) json += ",";
      }
      json += "]";
      server.send(200, "application/json", json);
    } });

  // Update config
  server.on("/api/config", HTTP_POST, []()
            {
    String body = server.arg("plain");
    if (body.isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Body kosong\"}");
      return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"JSON invalid\"}");
      return;
    }

    dfVolume = doc["volume"] | dfVolume;
    buzzerEnabled = doc["buzzer"] | buzzerEnabled;
    saveConfig();

    server.send(200, "application/json", "{\"success\":true}"); });

  // Adzan instant
  server.on("/api/adzan/instant", HTTP_POST, []()
            {
  String body = server.arg("plain");
  int index = 0;

  if (!body.isEmpty()) {
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      index = doc["index"] | 0;
    }
  }

  playAdzan(index);

  server.send(
    200, "application/json", "{\"success\":true,\"mode\":\"instant\"}"); });
}