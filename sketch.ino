/*
  Penugasan Day 3 Magang GMRT - Kontrol 5 Servo dengan IMU & PIR
  Nama : Reihan Firmansyah
  NIM  : 25/567418/SV/27177

  Catatan & Asumsi Pengerjaan:
  - Karena di Wokwi tidak ada sensor BNO055, saya menggunakan MPU6050 (alamat I2C 0x68)
    di pin SDA (21) dan SCL (22) sebagai pengganti buat simulasi input Roll, Pitch, dan Yaw.
  - Posisi awal (0 derajat) memakai nilai tengah PWM 1500 us. Rentang sudut -90 sampai +90 derajat
    diubah ke rentang PWM 1000 sampai 2000 us (frekuensi 50 Hz).
  - Saat PIR mendeteksi gerakan, kelima servo diset bergerak bareng ke sudut 60 derajat,
    lalu balik lagi ke posisi awal (0 derajat / 1500 us).
  - Untuk jeda 1 detik pada Servo 5 (Yaw), dipakai state machine dengan millis() supaya
    saat nunggu 1 detik, pembacaan sensor dan gerak servo Roll/Pitch tidak ikut terhenti oleh delay().
    Alur perpindahan state pada Yaw:
    1. YAW_IDLE     : kondisi awal, Servo 5 diam di 0 derajat (1500 us).
    2. YAW_MOVING   : sudut Yaw sedang berubah, Servo 5 bergerak mengikuti sudut Yaw.
    3. YAW_WAIT_1S  : rotasi Yaw berhenti, mulai hitung waktu 1 detik pakai millis().
    4. YAW_RETURNED : sudah lewat 1 detik, Servo 5 balik ke 1500 us sampai ada rotasi Yaw baru.
*/

#include <Wire.h>
#include <ESP32Servo.h>

// Pin I2C bawaan ESP32 buat sensor IMU
const int PIN_SDA    = 21;
const int PIN_SCL    = 22;

// Pin sensor PIR dan 5 servo (pakai pin selain 21 & 22 biar ga tabrakan sama jalur I2C)
const int PIN_PIR    = 4;
const int PIN_SERVO1 = 13; // Servo 1 (Roll)
const int PIN_SERVO2 = 12; // Servo 2 (Roll)
const int PIN_SERVO3 = 14; // Servo 3 (Pitch)
const int PIN_SERVO4 = 27; // Servo 4 (Pitch)
const int PIN_SERVO5 = 26; // Servo 5 (Yaw)

const int IMU_ADDR   = 0x68;

Servo servo1, servo2, servo3, servo4, servo5;

// Variabel buat nyimpan sudut Roll, Pitch, dan Yaw
float rollAngle  = 0.0;
float pitchAngle = 0.0;
float yawAngle   = 0.0;
float prevYaw    = 0.0;

// State untuk ngatur jeda 1 detik di Servo 5 (Yaw)
enum YawState {
  YAW_IDLE,
  YAW_MOVING,
  YAW_WAIT_1S,
  YAW_RETURNED
};
YawState currentYawState = YAW_IDLE;
unsigned long yawLastChangeMs = 0;
unsigned long yawWaitStartMs  = 0;

// Variabel bantu buat gerak serentak waktu PIR aktif
bool pirActive = false;
unsigned long pirStartMs = 0;
const float PIR_TARGET_ANGLE = 60.0;

bool useSerialOverride = false;
unsigned long lastPrintMs = 0;

// Fungsi buat ngubah sudut (-90 s/d 90 derajat) ke nilai PWM (1000 s/d 2000 us)
int angleToPWM(float angle) {
  if (angle > 90.0)  angle = 90.0;
  if (angle < -90.0) angle = -90.0;
  return (int)round(1500.0 + (angle / 90.0) * 500.0);
}

// Ambil data rotasi X, Y, Z dari sensor IMU lewat I2C
void readIMU() {
  if (useSerialOverride) return;

  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x43);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(IMU_ADDR, 6, true) == 6) {
    int16_t gx = (Wire.read() << 8) | Wire.read();
    int16_t gy = (Wire.read() << 8) | Wire.read();
    int16_t gz = (Wire.read() << 8) | Wire.read();

    rollAngle  = constrain(gx / 131.0, -90.0, 90.0);
    pitchAngle = constrain(gy / 131.0, -90.0, 90.0);
    yawAngle   = constrain(gz / 131.0, -90.0, 90.0);
  }
}

// Perintah bantu di Serial Monitor buat ngetes Kasus Uji 1 (T1) dan Kasus Uji 2 (T2)
void checkSerialCommand() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("T1")) {
      useSerialOverride = true;
      rollAngle  = 30.0;
      pitchAngle = -20.0;
      yawAngle   = 0.0;
      Serial.println("\n>>> [KASUS UJI 1 AKTIF] Input Roll = +30 deg, Pitch = -20 deg <<<");
    } else if (cmd.equalsIgnoreCase("T2")) {
      useSerialOverride = true;
      yawAngle = 45.0;
      Serial.println("\n>>> [KASUS UJI 2 AKTIF] Input Yaw = +45 deg (Berhenti & tunggu 1 detik) <<<");
    } else if (cmd.equalsIgnoreCase("SENSOR")) {
      useSerialOverride = false;
      Serial.println("\n>>> [MODE SLIDER SENSOR IMU AKTIF] <<<");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);

  // Aktifkan sensor MPU6050
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  pinMode(PIN_PIR, INPUT);

  // Set frekuensi PWM servo ke 50 Hz dan batas pulsa 1000 - 2000 us
  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo3.setPeriodHertz(50);
  servo4.setPeriodHertz(50);
  servo5.setPeriodHertz(50);

  servo1.attach(PIN_SERVO1, 1000, 2000);
  servo2.attach(PIN_SERVO2, 1000, 2000);
  servo3.attach(PIN_SERVO3, 1000, 2000);
  servo4.attach(PIN_SERVO4, 1000, 2000);
  servo5.attach(PIN_SERVO5, 1000, 2000);

  // Set semua servo ke posisi awal 0 derajat (1500 us) waktu pertama nyala
  servo1.writeMicroseconds(1500);
  servo2.writeMicroseconds(1500);
  servo3.writeMicroseconds(1500);
  servo4.writeMicroseconds(1500);
  servo5.writeMicroseconds(1500);

  Serial.println("Simulasi siap. Geser slider Rotation di IMU atau ketik T1 / T2 di Serial Monitor.");
}

void loop() {
  unsigned long now = millis();

  checkSerialCommand();
  readIMU();

  // Cek apakah sensor PIR mendeteksi gerakan
  if (digitalRead(PIN_PIR) == HIGH && !pirActive) {
    pirActive = true;
    pirStartMs = now;
    Serial.println("[PIR] Ada gerakan! Semua servo putar ke 60 derajat.");
  }

  // Kalau PIR lagi aktif, gerakkan semua servo secara bersamaan lalu kembalikan ke 0 derajat
  if (pirActive) {
    if (now - pirStartMs < 1000) {
      int pwmPir = angleToPWM(PIR_TARGET_ANGLE);
      servo1.writeMicroseconds(pwmPir);
      servo2.writeMicroseconds(pwmPir);
      servo3.writeMicroseconds(pwmPir);
      servo4.writeMicroseconds(pwmPir);
      servo5.writeMicroseconds(pwmPir);
      return;
    } else {
      servo1.writeMicroseconds(1500);
      servo2.writeMicroseconds(1500);
      servo3.writeMicroseconds(1500);
      servo4.writeMicroseconds(1500);
      servo5.writeMicroseconds(1500);
      Serial.println("[PIR] Selesai, semua servo balik ke posisi awal (1500 us).");
      if (digitalRead(PIN_PIR) == LOW) {
        pirActive = false;
      }
      return;
    }
  }

  // Servo 1 & 2 bergerak berlawanan arah dengan Roll (dikali minus)
  float targetServo12 = -rollAngle;
  int pwm1 = angleToPWM(targetServo12);
  int pwm2 = angleToPWM(targetServo12);
  servo1.writeMicroseconds(pwm1);
  servo2.writeMicroseconds(pwm2);

  // Servo 3 & 4 bergerak searah dengan Pitch
  float targetServo34 = pitchAngle;
  int pwm3 = angleToPWM(targetServo34);
  int pwm4 = angleToPWM(targetServo34);
  servo3.writeMicroseconds(pwm3);
  servo4.writeMicroseconds(pwm4);

  // Cek perubahan sudut Yaw untuk Servo 5
  bool yawChanged = (abs(yawAngle - prevYaw) > 0.5);
  if (yawChanged) {
    prevYaw = yawAngle;
    yawLastChangeMs = now;
    if (abs(yawAngle) > 0.5) {
      currentYawState = YAW_MOVING;
    } else {
      currentYawState = YAW_IDLE;
    }
  }

  // State machine buat ngatur gerak Servo 5 dan jeda 1 detik
  int pwm5 = 1500;
  switch (currentYawState) {
    case YAW_IDLE:
      pwm5 = 1500;
      break;

    case YAW_MOVING:
      pwm5 = angleToPWM(yawAngle);
      // Kalau sudut Yaw udah ga berubah selama 150 ms, anggap rotasi berhenti
      if (now - yawLastChangeMs >= 150) {
        currentYawState = YAW_WAIT_1S;
        yawWaitStartMs = now;
        Serial.print("[YAW STATE] Rotasi Yaw berhenti di ");
        Serial.print(yawAngle, 1);
        Serial.println(" deg -> Menunggu 1 detik (1000 ms)...");
      }
      break;

    case YAW_WAIT_1S:
      pwm5 = angleToPWM(yawAngle);
      // Tunggu sampai 1000 ms (1 detik), lalu balikkan Servo 5 ke posisi awal
      if (now - yawWaitStartMs >= 1000) {
        currentYawState = YAW_RETURNED;
        pwm5 = 1500;
        Serial.println("[YAW STATE - KASUS UJI 2] 1 detik selesai -> Servo 5 kembali ke posisi initial 0 deg (1500 us)!");
      }
      break;

    case YAW_RETURNED:
      pwm5 = 1500;
      break;
  }
  servo5.writeMicroseconds(pwm5);

  // Tampilkan nilai sudut dan PWM ke Serial Monitor tiap 500 ms
  if (now - lastPrintMs >= 500) {
    lastPrintMs = now;
    Serial.print("Roll: ");  Serial.print(rollAngle, 1);
    Serial.print(" deg | Pitch: "); Serial.print(pitchAngle, 1);
    Serial.print(" deg | Yaw: ");   Serial.print(yawAngle, 1);
    Serial.print(" deg  ==>  PWM [us] -> S1: "); Serial.print(pwm1);
    Serial.print(", S2: "); Serial.print(pwm2);
    Serial.print(", S3: "); Serial.print(pwm3);
    Serial.print(", S4: "); Serial.print(pwm4);
    Serial.print(", S5: "); Serial.println(pwm5);
  }
}