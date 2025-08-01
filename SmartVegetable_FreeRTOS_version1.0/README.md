# Smart Vegetable Garden System

## 1. Tổng Quan (Overview)
Hệ thống vườn rau thông minh sử dụng ESP32 và FreeRTOS để tự động hóa việc chăm sóc cây trồng. Hệ thống có khả năng giám sát các thông số môi trường và điều khiển các thiết bị tưới tiêu, thông gió, che chắn dựa trên các ngưỡng được cài đặt.

## 2. Phần Cứng (Hardware)
### 2.1. Vi Điều Khiển
- ESP32: Vi điều khiển chính, sử dụng 2 core để chạy FreeRTOS
- Core 0: Xử lý đọc cảm biến và hiển thị OLED
- Core 1: Xử lý kết nối WiFi/Blynk và điều khiển thiết bị

### 2.2. Cảm Biến (Sensors)
1. **DHT11**
   - Chân kết nối: GPIO25
   - Đo nhiệt độ và độ ẩm không khí
   - Cập nhật mỗi 2 giây

2. **Cảm Biến Độ Ẩm Đất**
   - Chân kết nối: GPIO36 (ADC)
   - Thang đo: 0-4095 (0% - 100%)
   - Cập nhật mỗi 2 giây

3. **MQ135 (Cảm Biến Chất Lượng Không Khí)**
   - Chân kết nối: GPIO35 (ADC)
   - Đo chỉ số AQI (Air Quality Index)
   - Cập nhật mỗi 2 giây

4. **Cảm Biến Mưa**
   - Chân kết nối: GPIO34 (ADC)
   - Ngưỡng phát hiện mưa: 2500
   - Cập nhật mỗi 2 giây

5. **BH1750 (Cảm Biến Ánh Sáng)**
   - Địa chỉ I2C: 0x23
   - Đo cường độ ánh sáng (lux)
   - Cập nhật mỗi 2 giây

### 2.3. Thiết Bị Điều Khiển
1. **Servo Motor (Mái Che)**
   - Chân kết nối: GPIO18
   - Góc mở: 5°
   - Góc đóng: 97°

2. **Relay Điều Khiển Bơm**
   - Chân kết nối: GPIO17
   - Logic điều khiển: LOW = ON, HIGH = OFF

3. **Relay Điều Khiển Quạt**
   - Chân kết nối: GPIO16
   - Logic điều khiển: LOW = ON, HIGH = OFF

### 2.4. Hiển Thị
- OLED SSD1306 (128x64)
- Địa chỉ I2C: 0x3C
- Kết nối qua I2C (SDA: GPIO21, SCL: GPIO22)

## 3. Phần Mềm (Software)
### 3.1. Hệ Điều Hành
- FreeRTOS: Hệ điều hành thời gian thực
- Sử dụng Task và Mutex để quản lý tài nguyên

### 3.2. Các Task Chính
1. **Task Đọc Cảm Biến (Core 0)**
   - DHTsensorTask: Đọc nhiệt độ, độ ẩm
   - soilMoistureTask: Đọc độ ẩm đất
   - airQualityTask: Đọc chất lượng không khí
   - rainSensorTask: Đọc cảm biến mưa
   - lightSensorTask: Đọc cường độ ánh sáng
   - oledUpdateTask: Cập nhật màn hình OLED

2. **Task Điều Khiển (Core 1)**
   - TaskConnectToWiFiAndBlynk: Kết nối WiFi và Blynk
   - blynkUpdateTask: Cập nhật dữ liệu lên Blynk
   - modeControlTask: Quản lý chế độ AUTO/REMOTE
   - blynkControlTask: Xử lý lệnh điều khiển từ Blynk
   - displayInfoTask: Hiển thị thông tin trên Serial

### 3.3. Chế Độ Hoạt Động
1. **Chế Độ Tự Động (AUTO)**
   - Điều khiển mái che dựa trên:
     + Ánh sáng > 200 lux
     + Có mưa (rainRaw < 2500)
   - Điều khiển bơm dựa trên:
     + Độ ẩm đất < 30%
   - Điều khiển quạt dựa trên:
     + Nhiệt độ > 35°C
     + Chất lượng không khí <= 85%

2. **Chế Độ Điều Khiển Từ Xa (REMOTE)**
   - Điều khiển thông qua ứng dụng Blynk
   - Có thể bật/tắt từng thiết bị riêng lẻ
   - Hiển thị dữ liệu cảm biến realtime

### 3.4. Kết Nối
- WiFi: Kết nối với mạng local
- Blynk: Kết nối với cloud để điều khiển từ xa
- Template ID: TMPL6XkxCCJEn
- Auth Token: 7HvXb9WKANYUid8REtAXoOjTZ1ljUBX9

## 4. Ngưỡng Điều Khiển
- Nhiệt độ: 35.0°C
- Chất lượng không khí: 85%
- Độ ẩm đất: 30.0%
- Ánh sáng: 200.0 lux
- Mưa: 2500 (ADC)

## 5. Tính Năng Bảo Mật
- Sử dụng Mutex để bảo vệ truy cập I2C
- Timeout cho kết nối WiFi (10s) và Blynk (5s)
- Xử lý lỗi khi đọc cảm biến

## 6. Yêu Cầu Phần Mềm
- VScode có cài extension PlatformIO
- Board DOIT DEVKIT V1
- Framework Arduino
- Thư viện:
  + FreeRTOS
  + DHT
  + Wire
  + BH1750
  + ESP32Servo
  + WiFi
  + BlynkSimpleEsp32
  + Adafruit_SSD1306

## 7. Hướng Dẫn Cài Đặt
1. Cài đặt extension PlatformIO trên VScode
2. Cài đặt các thư viện cần thiết được liệt kê ở mục 6
3. Kết nối phần cứng theo mô tả chân kết nối ở mục 2
4. Cập nhật thông tin WiFi gồm SSID và PASS; Blynk gồm BLYNK_TEMPLATE_ID, BLYNK_TEMPLATE_NAME và BLYNK_AUTH_TOKEN
5. Upload code lên ESP32

## 8. Lưu Ý
- Đảm bảo nguồn điện ổn định cấp cho ESP32 là 3.3V
- Các động cơ như quạt, Servo và Bơm dùng nguồn riêng 5V
- Kiểm tra kết nối cảm biến trước khi vận hành
- Cập nhật ngưỡng điều khiển phù hợp 