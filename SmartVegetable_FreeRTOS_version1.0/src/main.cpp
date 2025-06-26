// https://blynk.cloud/dashboard/791919/global/devices/1/organization/791919/devices/3234729/dashboardv
// https://blynk.cloud/dashboard/662630/global/devices/1/organization/662630/devices/3222607/dashboard

// #define BLYNK_TEMPLATE_ID   "TMPL6XkxCCJEn"
// #define BLYNK_TEMPLATE_NAME "Smart Vegetable"
// #define BLYNK_AUTH_TOKEN    "7HvXb9WKANYUid8REtAXoOjTZ1ljUBX9"
#define BLYNK_TEMPLATE_ID "TMPL6OIwNHVmO"
#define BLYNK_TEMPLATE_NAME "Smart Vegetable"
#define BLYNK_AUTH_TOKEN "m3ZO2xEHHWmPrM-xp-_gjHhoeCG4t6_T"

#define BLYNK_PRINT Serial

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Adafruit_SSD1306.h>

// Wi-Fi credentials
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "S21 FE 5G";
char pass[] = "tuchoxiu";

// — Virtual Pins —
#define VP_BUTTON_CANOPY V0
#define VP_BUTTON_PUMP   V1
#define VP_BUTTON_FAN    V2
#define VP_BUTTON_MODE   V7
#define VP_TEMP          V3
#define VP_HUMID         V4
#define VP_SOIL          V5
#define VP_MQ135         V6

// Cờ kết nối Wifi và Blynk
bool wifiConnected  = false;
bool blynkConnected = false;

// Chế độ hệ thống
bool isAutoMode = true;  // true = AUTO, false = REMOTE
bool prevMode   = true;

// Trạng thái thiết bị (được cập nhật trong AUTO hoặc REMOTE)
bool servoClose = false;
bool pumpOn      = false;
bool fanOn       = false;

// — Cấu hình chân & hằng số —
#define LED_PIN            2    // LED báo Blynk OK
#define DHTPIN             25   // Chân DHT11
#define DHTTYPE            DHT11
#define SOIL_MOISTURE_PIN  36   // ADC
#define MQ135_PIN          35   // ADC
#define RAIN_SENSOR_PIN    34   // ADC
#define BH1750_ADDR        0x23
#define OLED_ADDR          0x3C
#define FAN_RELAY_PIN      16
#define PUMP_RELAY_PIN     17
#define SERVO_PIN          18

// Ngưỡng điều khiển
#define TEMP_THRESHOLD   35.0
#define MQ135_THRESHOLD  85
#define SOIL_THRESHOLD_MIN 10.0
#define SOIL_THRESHOLD_MAX 30.0
#define LIGHT_THRESHOLD  200.0
#define RAIN_THRESHOLD   2500
#define openAngle        0
#define closeAngle       180

#define WIFI_TIMEOUT       10000   // ms
#define BLYNK_CONN_TIMEOUT 5000    // ms

// — Định nghĩa OLED —
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// — Mutex cho I2C —
SemaphoreHandle_t i2cMutex;

// — Đối tượng cảm biến —
DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter(BH1750_ADDR);
Servo myServo;

// — Biến toàn cục lưu dữ liệu cảm biến —
float temperature          = 0.0;
float humidity             = 0.0;
float soilMoisturePercent  = 0.0;
float airQuality           = 0.0;
float lux                  = 0.0;

// Giá trị hiệu chỉnh cho sensor (nếu cần)
const int dryValue         = 4095;
const int wetValue         = 0;
const int cleanAirValue    = 0;
const int pollutedAirValue = 4095;

// Biến đọc trực tiếp Rain
int rainRawGlobal = 4095;

// Thêm biến để lưu trạng thái điều khiển từ Blynk
struct BlynkControlState {
  bool modeChanged = false;
  bool canopyChanged = false;
  bool pumpChanged = false;
  bool fanChanged = false;
  bool newMode = true;
  bool newCanopy = false;
  bool newPump = false;
  bool newFan = false;
} blynkControl;

// --------------------------------------------------------------------------------
// 1) Hàm điều khiển thực tế
void smoothServoControl(int targetAngle, int stepDelay = 15) {
  int currentAngle = myServo.read();
  
  if (currentAngle < targetAngle) {
    for (int angle = currentAngle; angle <= targetAngle; angle++) {
      myServo.write(angle);
      delay(stepDelay);
    }
  } else {
    for (int angle = currentAngle; angle >= targetAngle; angle--) {
      myServo.write(angle);
      delay(stepDelay);
    }
  }
}

void controlServo(bool close) {
  if (!close) {
    smoothServoControl(openAngle);
  } else {
    smoothServoControl(closeAngle);
  }
}

void controlPump(bool on) {
  digitalWrite(PUMP_RELAY_PIN, on ? LOW : HIGH);
}

void controlFan(bool on) {
  digitalWrite(FAN_RELAY_PIN, on ? LOW : HIGH);
}

// 2) Hàm đọc Rain cho mỗi lần cần (dùng trong autoControlLogic)
int readRainSensor() {
  int raw = analogRead(RAIN_SENSOR_PIN);
  rainRawGlobal = raw;
  return raw;
}

// --------------------------------------------------------------------------------
// 3) Task đọc DHT11 (cập nhật temperature, humidity hàng 2s)
void DHTsensorTask(void *pvParameters) {
  dht.begin();
  while (1) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// 4) Task đọc Soil Moisture (hàng 2s)
void soilMoistureTask(void *pvParameters) {
  while (1) {
    int raw = analogRead(SOIL_MOISTURE_PIN);
    raw = constrain(raw, wetValue, dryValue);
    soilMoisturePercent = 100.0 * (dryValue - raw) / (dryValue - wetValue);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// 5) Task đọc MQ135 (hàng 2s)
void airQualityTask(void *pvParameters) {
  while (1) {
    int raw = analogRead(MQ135_PIN);
    raw = constrain(raw, cleanAirValue, pollutedAirValue);
    airQuality = map(raw, cleanAirValue, pollutedAirValue, 100, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// 6) Task đọc Rain Sensor (hàng 2s, chỉ in ra log)
void rainSensorTask(void *pvParameters) {
  while (1) {
    int raw = analogRead(RAIN_SENSOR_PIN);
    rainRawGlobal = raw;
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// 7) Task đọc BH1750 (hàng 2s)
void lightSensorTask(void *pvParameters) {
  while (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  while (1) {
    float val = lightMeter.readLightLevel();
    if (val < 0) {
      lux = 0.0;
    } else {
      lux = val;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// 8) Task hiển thị trên OLED (hàng 2s)
void oledUpdateTask(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
      display.clearDisplay();
      display.setTextSize(0.2);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      
      // Dòng 1: Mode
      display.print("Mode: ");
      display.println(isAutoMode ? "AUTO" : "REMOTE");
      
      // Dòng 2: Temp và Humd
      display.print("T: ");
      display.print(temperature, 1);
      display.print("C| H: ");
      display.print(humidity, 1);
      display.println("%");
      
      // Dòng 3: DoAmDat và Pump
      display.print("DoAmDat: ");
      display.print(soilMoisturePercent, 1);
      display.println("%");
      
      // Dòng 4: AQI
      display.print("AQI: ");
      display.print(airQuality, 1);
      display.print("% -> ");
      display.println(airQuality > MQ135_THRESHOLD ? "GOOD" : "BAD");
      
      // Dòng 5: Weather 
      display.print("Weather:");
      display.println(rainRawGlobal < RAIN_THRESHOLD ? "Rain" : "Sun");

      // Dòng 6: Light
      display.print("Light: ");
      display.print(lux, 1);
      display.println("lux");

      // Dòng 7: Canopy và Fan states
      display.print("Canopy:");
      display.print(servoClose ? "Close" : "Open");
      display.print("| Fan:");
      display.println(fanOn ? "On" : "Off");

      // Dòng 8: Pump state
      display.print("Pump: ");
      display.println(pumpOn ? "On" : "Off");
      
      display.display();
      xSemaphoreGive(i2cMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// --------------------------------------------------------------------------------
// 9) Hàm điều khiển tự động (gọi mỗi khi isAutoMode = true):
void autoControlLogic() {
  // 9.1. Lấy giá trị cảm biến
  float currentTemp  = temperature;
  float currentSoil  = soilMoisturePercent;
  float currentLux   = lux;
  int   currentRain  = rainRawGlobal;
  int   currentMQ    = airQuality;

  // 9.2. Điều khiển Servo (mái che):
  if (currentLux > LIGHT_THRESHOLD || currentRain < RAIN_THRESHOLD) {
    // điều kiện đóng mái che
    if (!servoClose) {
      servoClose = true;
      controlServo(true);
    }
  } else {
    // điều kiện mở mái che
    if (servoClose) {
      servoClose = false;
      controlServo(false);
    }
  }

  // 9.3. Điều khiển Pump
  if ((SOIL_THRESHOLD_MIN < currentSoil) && (currentSoil < SOIL_THRESHOLD_MAX)) {
    if (!pumpOn) {
      pumpOn = true;
      controlPump(true);
    }
  } else {
    if (pumpOn) {
      pumpOn = false;
      controlPump(false);
    }
  }

  // 9.4. Điều khiển Fan
  if (currentTemp > TEMP_THRESHOLD || airQuality <= MQ135_THRESHOLD) {
    if (!fanOn) {
      fanOn = true;
      controlFan(true);
    }
  } else {
    if (fanOn) {
      fanOn = false;
      controlFan(false);
    }
  }

  // 9.5. Đồng bộ trạng thái lên Blynk
  Blynk.virtualWrite(VP_BUTTON_CANOPY, servoClose);
  Blynk.virtualWrite(VP_BUTTON_PUMP,    pumpOn);
  Blynk.virtualWrite(VP_BUTTON_FAN,     fanOn);
}

// --------------------------------------------------------------------------------
// 10) Task quản lý chế độ AUTO / REMOTE
void modeControlTask(void *pvParameters) {
  (void) pvParameters;
  while (1) {
    // Nếu vừa chuyển đổi chế độ
    if (isAutoMode != prevMode) {
      if (isAutoMode) {
        Serial.println("→ Chuyển sang chế độ AUTO");
        // Đồng bộ trạng thái ban đầu lên Blynk (nếu cần)
        Blynk.virtualWrite(VP_BUTTON_CANOPY, servoClose);
        Blynk.virtualWrite(VP_BUTTON_PUMP,    pumpOn);
        Blynk.virtualWrite(VP_BUTTON_FAN,     fanOn);
      } else {
        Serial.println("→ Chuyển sang chế độ REMOTE");
        // Reset các widget Blynk về 0
        Blynk.virtualWrite(VP_BUTTON_CANOPY, 0);
        Blynk.virtualWrite(VP_BUTTON_PUMP,   0);
        Blynk.virtualWrite(VP_BUTTON_FAN,    0);
        // Tắt hết thiết bị
        controlServo(false);
        controlPump(false);
        controlFan(false);
        // Cập nhật lại cờ
        servoClose = false;
        pumpOn      = false;
        fanOn       = false;
      }
      prevMode = isAutoMode;
    }

    // Nếu đang ở AUTO → gọi autoControlLogic()
    if (isAutoMode) {
      autoControlLogic();
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// --------------------------------------------------------------------------------
// 11) Task xử lý lệnh điều khiển từ Blynk
void blynkControlTask(void *pvParameters) {
  while (1) {
    if (blynkConnected) {
      if (blynkControl.modeChanged) {
        isAutoMode = blynkControl.newMode;
        blynkControl.modeChanged = false;
      }
      
      if (!isAutoMode) {  // Chỉ xử lý khi ở chế độ REMOTE
        if (blynkControl.canopyChanged) {
          servoClose = blynkControl.newCanopy;
          controlServo(servoClose);
          blynkControl.canopyChanged = false;
        }
        
        if (blynkControl.pumpChanged) {
          pumpOn = blynkControl.newPump;
          controlPump(pumpOn);
          blynkControl.pumpChanged = false;
        }
        
        if (blynkControl.fanChanged) {
          fanOn = blynkControl.newFan;
          controlFan(fanOn);
          blynkControl.fanChanged = false;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));  // Kiểm tra mỗi 50ms
  }
}

// --------------------------------------------------------------------------------
// 12) Tối ưu hóa callback từ Blynk
BLYNK_WRITE(VP_BUTTON_MODE) {
  blynkControl.newMode = (param.asInt() == 0);
  blynkControl.modeChanged = true;
}

BLYNK_WRITE(VP_BUTTON_CANOPY) {
  blynkControl.newCanopy = param.asInt();
  blynkControl.canopyChanged = true;
}

BLYNK_WRITE(VP_BUTTON_PUMP) {
  blynkControl.newPump = param.asInt();
  blynkControl.pumpChanged = true;
}

BLYNK_WRITE(VP_BUTTON_FAN) {
  blynkControl.newFan = param.asInt();
  blynkControl.fanChanged = true;
}

// --------------------------------------------------------------------------------
// 13) Task kết nối WiFi + Blynk
void TaskConnectToWiFiAndBlynk(void *pvParameters) {
  (void) pvParameters;
  while (1) {
    Serial.println();
    Serial.printf("Connecting to WiFi: %s …", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    unsigned long t0 = millis();
    while ((millis() - t0 < WIFI_TIMEOUT) && WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.print("WiFi OK, IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi FAILED (timeout)");
      vTaskDelete(NULL);
    }

    if (wifiConnected) {
      Blynk.config(auth);
      Serial.println("Connecting to Blynk …");
      if (Blynk.connect(BLYNK_CONN_TIMEOUT)) {
        blynkConnected = true;
        digitalWrite(LED_PIN, HIGH);
        Serial.println("Blynk connected!");
        // Gửi giá trị khởi tạo
        Blynk.virtualWrite(VP_BUTTON_MODE, 0);
        Blynk.virtualWrite(VP_TEMP,  0);
        Blynk.virtualWrite(VP_HUMID, 0);
        Blynk.virtualWrite(VP_SOIL,  0);
        Blynk.virtualWrite(VP_MQ135, 0);
      } else {
        digitalWrite(LED_PIN, LOW);
        Serial.println("Blynk FAILED (timeout)");
      }
    }

    if (wifiConnected && blynkConnected) break;
    Serial.println("Retrying WiFi/Blynk in 10s …");
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
  vTaskDelete(NULL);
}

// 14) Task cập nhật dữ liệu lên Blynk (hàng 10s)
void blynkUpdateTask(void *pvParameters) {
  const TickType_t delayTicks = pdMS_TO_TICKS(10000);
  while (1) {
    if (blynkConnected) {
      Blynk.run();
      Blynk.virtualWrite(VP_TEMP,  temperature);
      Blynk.virtualWrite(VP_HUMID, humidity);
      Blynk.virtualWrite(VP_SOIL,  soilMoisturePercent);
      Blynk.virtualWrite(VP_MQ135, airQuality);
    }
    vTaskDelay(delayTicks);
  }
}

// 15) Task mới để hiển thị thông tin
void displayInfoTask(void *pvParameters) {
  while (1) {
    Serial.println("\n----------------------------------------");
    Serial.println(">>> SENSOR READINGS:");
    Serial.printf("DHT11 → Temperature: %.1f°C  Humidity: %.1f%%\n", temperature, humidity);
    Serial.printf("Soil Moisture → Raw: %d  Percentage: %.1f%%\n", analogRead(SOIL_MOISTURE_PIN), soilMoisturePercent);
    Serial.printf("MQ135 → Raw: %d  AQI: %.1f%%  Status: %s\n", 
                 analogRead(MQ135_PIN),
                 airQuality, 
                 airQuality > MQ135_THRESHOLD ? "GOOD" : "BAD");
    Serial.printf("Rain Sensor → Raw: %d  Status: %s\n", 
                 rainRawGlobal,
                 rainRawGlobal < RAIN_THRESHOLD ? "RAINING" : "NO RAIN");
    Serial.printf("BH1750 → Light Level: %.1f lux\n", lux);
    
    Serial.println("\n>>> DEVICE STATUS:");
    Serial.printf("Canopy: %s  |  Fan: %s  |  Pump: %s\n", 
                 servoClose ? "CLOSED" : "OPEN",
                 fanOn ? "ON" : "OFF",
                 pumpOn ? "ON" : "OFF");
    Serial.println("----------------------------------------");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// --------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Khởi tạo I2C
  Wire.begin(21, 22);

  // Khởi tạo LED báo kết nối
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Khởi tạo chân Input/Output
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(MQ135_PIN,         INPUT);
  pinMode(RAIN_SENSOR_PIN,   INPUT);
  pinMode(PUMP_RELAY_PIN,    OUTPUT);
  pinMode(FAN_RELAY_PIN,     OUTPUT);

  // Đặt relay ở trạng thái OFF (HIGH) ban đầu
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  digitalWrite(FAN_RELAY_PIN,  HIGH);

  // Khởi tạo Servo ở góc 5°
  myServo.attach(SERVO_PIN);
  myServo.write(openAngle);

  // Khởi tạo mutex cho I2C
  i2cMutex = xSemaphoreCreateMutex();

  // Khởi tạo OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();

  // 1) Task kết nối WiFi/Blynk (core 1)
  xTaskCreatePinnedToCore(
    TaskConnectToWiFiAndBlynk,
    "WiFiBlynkTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  // 2) Task cập nhật Blynk (core 1)
  xTaskCreatePinnedToCore(
    blynkUpdateTask,
    "Blynk Update Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );

  // 3) Task đọc DHT11 (core 0)
  xTaskCreatePinnedToCore(
    DHTsensorTask,
    "DHT Sensor Task",
    2048,
    NULL,
    2,
    NULL,
    0
  );

  // 4) Task đọc Soil Moisture (core 0)
  xTaskCreatePinnedToCore(
    soilMoistureTask,
    "Soil Moisture Task",
    2048,
    NULL,
    2,
    NULL,
    0
  );

  // 5) Task đọc MQ135 (core 0)
  xTaskCreatePinnedToCore(
    airQualityTask,
    "Air Quality Task",
    2048,
    NULL,
    2,
    NULL,
    0
  );

  // 6) Task đọc Rain Sensor (core 0)
  xTaskCreatePinnedToCore(
    rainSensorTask,
    "Rain Sensor Task",
    2048,
    NULL,
    2,
    NULL,
    0
  );

  // 7) Task đọc BH1750 (core 0)
  xTaskCreatePinnedToCore(
    lightSensorTask,
    "Light Sensor Task",
    2048,
    NULL,
    2,
    NULL,
    0
  );

  // 8) Task hiển thị OLED (core 0)
  xTaskCreatePinnedToCore(
    oledUpdateTask,
    "OLED Update Task",
    3072,
    NULL,
    1,
    NULL,
    0
  );

  // 9) Task quản lý chế độ AUTO/REMOTE (core 1)
  xTaskCreatePinnedToCore(
    modeControlTask,
    "Mode Control Task",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  // 10) Task xử lý lệnh điều khiển Blynk (core 1)
  xTaskCreatePinnedToCore(
    blynkControlTask,
    "Blynk Control Task",
    2048,
    NULL,
    2,  // Priority cao hơn
    NULL,
    1
  );

  // 11) Task hiển thị thông tin
  xTaskCreatePinnedToCore(
    displayInfoTask,
    "Display Info Task",
    4096,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  // Không cần code trong loop() vì đã dùng FreeRTOS Task
  vTaskDelay(pdMS_TO_TICKS(10000));
}
