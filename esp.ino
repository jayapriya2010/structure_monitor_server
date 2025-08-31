#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>
#include <math.h>
#include <arduinoFFT.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ========== WiFi credentials ==========
const char* ssid = "Cherrinet-2.4G";
const char* password = "9940458344";
const char* serverUrl = "https://structure-monitor-server-xpyz.onrender.com/api/sensor-data";

// ========== MPU6050 + FFT Variables ==========
Adafruit_MPU6050 mpu;
#define SAMPLE_RATE     100
#define WINDOW_SIZE     256
#define FREQ_MIN        1.0f
#define FREQ_MAX        40.0f
float amag_buffer[WINDOW_SIZE];
unsigned int sample_count = 0;
ArduinoFFT<double> FFT;
unsigned long lastMPUCall = 0;

// ========== SW-18010P vibration ==========
const int D0_PIN = 27;
volatile unsigned long hitCount = 0;
unsigned long lastTimeVib = 0;
const unsigned long LOCKOUT_MS = 60;
volatile unsigned long lastISR = 0;
const int MAX_HITS_PER_SEC = 50;
float vibrationValue = 0;

// ========== FSR force/pressure ==========
#define FORCE_SENSOR_PIN 36
float adc0 = 16;    float force0 = 0.00;
float adc1 = 118;   float force1 = 4.90;
float adc2 = 381;   float force2 = 9.81;
const int SAMPLES = 8;
bool printInKg = false;
float sensorDiameter = 0.0127;
float sensorArea = 3.14159 * pow(sensorDiameter/2, 2);
unsigned long lastFSRRead = 0;

// ========== HTTP POST timer ==========
unsigned long lastServerPush = 0;
const unsigned long SERVER_PUSH_INTERVAL = 5000; // 5 seconds

// ========== Data Cache ==========
float lastPitch = 0, lastRoll = 0, lastRms = 0, lastPeak = 0, lastCrest = 0, lastFdom = 0, lastTemp = 0;
float lastSW18010P = 0;
int lastFSR_adc = 0;
float lastFSR_forceN = 0, lastFSR_pressure = 0;

// ========== Helper Structs & Functions ==========
struct CalPoint { float adc; float force; };
CalPoint P[3];
void sortByADC(CalPoint a[]) {
  if (a[0].adc > a[1].adc) { CalPoint t=a[0]; a[0]=a[1]; a[1]=t; }
  if (a[1].adc > a[2].adc) { CalPoint t=a[1]; a[1]=a[2]; a[2]=t; }
  if (a[0].adc > a[1].adc) { CalPoint t=a[0]; a[0]=a[1]; a[1]=t; }
}
float lerp(float x0, float y0, float x1, float y1, float x) {
  if (x1 == x0) return y0;
  return y0 + ( (x - x0) * (y1 - y0) / (x1 - x0) );
}
float mapADCtoForce(float adcVal) {
  CalPoint a[3] = { P[0], P[1], P[2] };
  sortByADC(a);
  if (adcVal <= a[0].adc) {
    return lerp(a[0].adc, a[0].force, a[1].adc, a[1].force, adcVal);
  }
  if (adcVal >= a[2].adc) {
    return lerp(a[1].adc, a[1].force, a[2].adc, a[2].force, adcVal);
  }
  if (adcVal <= a[1].adc) {
    return lerp(a[0].adc, a[0].force, a[1].adc, a[1].force, adcVal);
  } else {
    return lerp(a[1].adc, a[1].force, a[2].adc, a[2].force, adcVal);
  }
}
int readAveragedADC(int pin, int nsamples) {
  if (nsamples <= 1) return analogRead(pin);
  long sum = 0;
  for (int i = 0; i < nsamples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (int)(sum / nsamples);
}

void IRAM_ATTR onVibration() {
  unsigned long now = millis();
  if (now - lastISR > LOCKOUT_MS) {
    lastISR = now;
    hitCount++;
  }
}
float getDominantFrequency(float *data, int size, float fs, float fmin, float fmax) {
  static double vReal[WINDOW_SIZE];
  static double vImag[WINDOW_SIZE];
  for (int i = 0; i < size; i++) {
    vReal[i] = (double)data[i];
    vImag[i] = 0.0;
  }
  FFT.windowing(vReal, size, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, size, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, size);
  float bin_width = fs / size;
  int idx_min = (int)(fmin / bin_width);
  int idx_max = (int)(fmax / bin_width);
  if (idx_min < 1) idx_min = 1;
  if (idx_max > size / 2) idx_max = size / 2;
  int peak_index = idx_min;
  double peak_value = 0;
  for (int i = idx_min; i <= idx_max; i++) {
    if (vReal[i] > peak_value) {
      peak_value = vReal[i];
      peak_index = i;
    }
  }
  return peak_index * bin_width;
}

// ========== SEND TO SERVER FUNCTION ==========
void sendToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Skipping server push.");
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  // Prepare JSON string
  String payload = "{";
  payload += "\"pitch\":" + String(lastPitch, 2) + ",";
  payload += "\"roll\":" + String(lastRoll, 2) + ",";
  payload += "\"rms\":" + String(lastRms, 3) + ",";
  payload += "\"peak\":" + String(lastPeak, 3) + ",";
  payload += "\"crest_factor\":" + String(lastCrest, 3) + ",";
  payload += "\"f_dom\":" + String(lastFdom, 3) + ",";
  payload += "\"temp\":" + String(lastTemp, 2) + ",";
  payload += "\"sw18010p\":" + String(lastSW18010P, 1) + ",";
  payload += "\"fsr_adc\":" + String(lastFSR_adc) + ",";
  payload += "\"fsr_force_N\":" + String(lastFSR_forceN, 2) + ",";
  payload += "\"fsr_pressure_Pa\":" + String(lastFSR_pressure, 2);
  payload += "}";

  int httpCode = http.POST(payload);
  Serial.print("POST to server: ");
  Serial.println(httpCode);
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Server response: " + response);
  }
  http.end();
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected. IP address: ");
  Serial.println(WiFi.localIP());

  // MPU6050
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050");
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  delay(100);

  // Vibration module
  pinMode(D0_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(D0_PIN), onVibration, FALLING);

  // FSR setup
  analogSetAttenuation(ADC_11db);
  P[0] = { adc0, force0 };
  P[1] = { adc1, force1 };
  P[2] = { adc2, force2 };

  Serial.println("All sensors initialized.");
}

// ========== LOOP ==========
void loop() {
  unsigned long now = millis();

  // ========== MPU6050 FFT ==========
  if (now - lastMPUCall > (1000 / SAMPLE_RATE)) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float pitch = atan2(a.acceleration.y,
                        sqrt(a.acceleration.x * a.acceleration.x +
                             a.acceleration.z * a.acceleration.z)) * 180.0 / M_PI;
    float roll = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / M_PI;
    float amag = sqrt(a.acceleration.x * a.acceleration.x +
                      a.acceleration.y * a.acceleration.y +
                      a.acceleration.z * a.acceleration.z) - 9.80665;
    amag_buffer[sample_count++] = amag;

    if (sample_count >= WINDOW_SIZE) {
      float sum_sq = 0, peak = 0;
      for (int i = 0; i < WINDOW_SIZE; i++) {
        float v = amag_buffer[i];
        sum_sq += v * v;
        if (fabs(v) > peak) peak = fabs(v);
      }
      float rms = sqrt(sum_sq / WINDOW_SIZE);
      float crest = (rms > 0) ? (peak / rms) : 0;
      float dominant_freq = getDominantFrequency(amag_buffer, WINDOW_SIZE, SAMPLE_RATE, FREQ_MIN, FREQ_MAX);

      StaticJsonDocument<256> doc;
      doc["pitch"] = pitch;
      doc["roll"] = roll;
      doc["rms"] = rms;
      doc["peak"] = peak;
      doc["crest_factor"] = crest;
      doc["f_dom"] = dominant_freq;
      doc["temp"] = temp.temperature;
      serializeJson(doc, Serial);
      Serial.println();

      // Save for posting
      lastPitch = pitch;
      lastRoll = roll;
      lastRms = rms;
      lastPeak = peak;
      lastCrest = crest;
      lastFdom = dominant_freq;
      lastTemp = temp.temperature;

      sample_count = 0;
    }
    lastMPUCall = now;
  }

  // ========== SW-18010P Vibration ==========
  if (now - lastTimeVib >= 2000) {
    lastTimeVib = now;
    noInterrupts();
    unsigned long hits = hitCount;
    hitCount = 0;
    interrupts();
    vibrationValue = (hits * 100.0) / MAX_HITS_PER_SEC;
    if (vibrationValue > 100) vibrationValue = 100;
    Serial.print("SW18010P Vibration value: ");
    Serial.println(vibrationValue, 1);
    lastSW18010P = vibrationValue;
  }

  // ========== FSR Sensor ==========
  if (now - lastFSRRead >= 500) {
    lastFSRRead = now;
    int adcRaw = readAveragedADC(FORCE_SENSOR_PIN, SAMPLES);
    float forceN = 0;
    float pressure = 0;
    if (adcRaw >= 10) {
      forceN = mapADCtoForce((float)adcRaw);
      if (sensorArea > 0)
        pressure = forceN / sensorArea;
    }
    Serial.print("FSR ADC: ");
    Serial.print(adcRaw);
    Serial.print(" | Force: ");
    if (printInKg) {
      float massKg = forceN / 9.81;
      Serial.print(massKg, 2);
      Serial.print(" kg");
    } else {
      Serial.print(forceN, 2);
      Serial.print(" N");
    }
    Serial.print(" | Pressure: ");
    Serial.print(pressure, 2);
    Serial.println(" Pa");
    lastFSR_adc = adcRaw;
    lastFSR_forceN = forceN;
    lastFSR_pressure = pressure;
  }

  // ========== Send to Server ==========
  if (now - lastServerPush > SERVER_PUSH_INTERVAL) {
    sendToServer();
    lastServerPush = now;
  }
}
