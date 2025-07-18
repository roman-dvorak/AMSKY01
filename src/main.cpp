#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_TSL2591.h>
// MLX90641 definitions (from fw_melexis)
#define MLX90641_I2C_ADDR 0x33
#define MLX90641_RAM_START 0x0400
#define MLX90641_PIXEL_COUNT 192
#define MLX90641_STATUS_REG 0x8000
#include "version.h"

// Firmware and hardware version info
#define DEVICE_NAME "AFSKY01 Hygrometer"
#define HW_VERSION "1.0"
#define FW_VERSION BUILD_VERSION

// Pin definitions
#define CPU_STATUS_LED 22   // CPU status LED
#define TRIGGER_OUT_LED 27  // Trigger out LED
#define SDA_PIN 18   // I2C1 SDA
#define SCL_PIN 19   // I2C1 SCL

// Initialize SHT45 sensor
Adafruit_SHT4x sht4 = Adafruit_SHT4x();

// Initialize TSL2591 light sensor
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);

// MLX90641 thermal sensor variables
uint16_t mlx90641_pixelData[MLX90641_PIXEL_COUNT];
bool mlx90641_available = false;

// MLX90641 helper functions
bool readRegister(byte deviceAddress, uint16_t registerAddress, uint16_t* data);
bool readMultipleRegisters(byte deviceAddress, uint16_t startAddress, uint16_t* data, uint16_t length);
bool checkNewData();
void clearNewDataBit();

// Variables for LEDs
bool trigger_led_state = false;
unsigned long last_trigger_blink = 0;
const unsigned long TRIGGER_BLINK_INTERVAL = 1000; // 1000ms - slower blink

// CPU LED PWM breathing effect
const unsigned long CPU_BREATHING_PERIOD = 2000; // 2 seconds for full cycle
const float MY_PI = 3.14159265359;

// Variables for sensors
bool sht4_available = false;
bool tsl_available = false;
unsigned long last_measurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 2000; // 2 seconds

// TSL2591 auto-gain variables
tsl2591Gain_t current_gain = TSL2591_GAIN_MED;
unsigned long last_gain_adjustment = 0;
const unsigned long GAIN_ADJUSTMENT_INTERVAL = 5000; // 5 seconds between gain adjustments
const uint16_t GAIN_SATURATED_THRESHOLD = 60000;  // Near saturation (65535 max)
const uint16_t GAIN_TOO_LOW_THRESHOLD = 1000;     // Too low signal

// Function to get gain string for debugging
const char* getGainString(tsl2591Gain_t gain) {
  switch(gain) {
    case TSL2591_GAIN_LOW:  return "1x";
    case TSL2591_GAIN_MED:  return "25x";
    case TSL2591_GAIN_HIGH: return "428x";
    case TSL2591_GAIN_MAX:  return "9876x";
    default: return "unknown";
  }
}

// Function to adjust gain based on current light levels
bool adjustGain(uint16_t full_value) {
  tsl2591Gain_t new_gain = current_gain;
  
  // Check if we need to decrease gain (too bright)
  if (full_value > GAIN_SATURATED_THRESHOLD) {
    switch(current_gain) {
      case TSL2591_GAIN_MAX:  new_gain = TSL2591_GAIN_HIGH; break;
      case TSL2591_GAIN_HIGH: new_gain = TSL2591_GAIN_MED; break;
      case TSL2591_GAIN_MED:  new_gain = TSL2591_GAIN_LOW; break;
      case TSL2591_GAIN_LOW:  return false; // Already at minimum
    }
  }
  // Check if we need to increase gain (too dark)
  else if (full_value < GAIN_TOO_LOW_THRESHOLD) {
    switch(current_gain) {
      case TSL2591_GAIN_LOW:  new_gain = TSL2591_GAIN_MED; break;
      case TSL2591_GAIN_MED:  new_gain = TSL2591_GAIN_HIGH; break;
      case TSL2591_GAIN_HIGH: new_gain = TSL2591_GAIN_MAX; break;
      case TSL2591_GAIN_MAX:  return false; // Already at maximum
    }
  }
  else {
    return false; // No adjustment needed
  }
  
  // Apply new gain
  current_gain = new_gain;
  tsl.setGain(current_gain);
  
  Serial.print("Gain adjusted to: ");
  Serial.println(getGainString(current_gain));
  
  return true;
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(100);
  
  // Initialize GPIO pins
  pinMode(CPU_STATUS_LED, OUTPUT);
  pinMode(TRIGGER_OUT_LED, OUTPUT);
  
  // LEDs off at start
  digitalWrite(CPU_STATUS_LED, LOW);
  digitalWrite(TRIGGER_OUT_LED, LOW);
  
// Print device information
Serial.println(DEVICE_NAME);
Serial.print("HW Version: ");
  Serial.println(HW_VERSION);
  Serial.print("FW Version: ");
  Serial.println(FW_VERSION);
  Serial.println();
  
  // Initialize I2C1 with specific pins (GPIO 18=SDA, GPIO 19=SCL)
  Wire1.setSDA(SDA_PIN);
  Wire1.setSCL(SCL_PIN);
  Wire1.begin();
  delay(100);
  
  // Initialize SHT45 sensor on I2C1
  if (sht4.begin(&Wire1)) {
    sht4_available = true;
    
    // Set precision and heater
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    Serial.println("SHT4x sensor initialized successfully");
  } else {
    sht4_available = false;
    Serial.println("SHT4x sensor initialization failed");
  }
  
  // Initialize TSL2591 light sensor on I2C1
  if (tsl.begin(&Wire1)) {
    tsl_available = true;
    
    // Configure TSL2591 sensor
    current_gain = TSL2591_GAIN_MED;
    tsl.setGain(current_gain);      // Start with medium gain
    tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);  // medium integration time
    
    Serial.println("TSL2591 light sensor initialized successfully");
    Serial.print("Initial gain: ");
    Serial.println(getGainString(current_gain));
  } else {
    tsl_available = false;
    Serial.println("TSL2591 light sensor initialization failed");
  }
  
  // Initialize MLX90641 thermal sensor
  Serial.println("Testing MLX90641 thermal sensor...");
  Wire1.beginTransmission(MLX90641_I2C_ADDR);
  if (Wire1.endTransmission() == 0) {
    mlx90641_available = true;
    Serial.println("MLX90641 thermal sensor initialized successfully");
  } else {
    mlx90641_available = false;
    Serial.println("MLX90641 thermal sensor initialization failed");
  }
}

void loop() {
  // Handle CPU status LED PWM breathing effect
  unsigned long current_time = millis();
  float phase = (current_time % CPU_BREATHING_PERIOD) / (float)CPU_BREATHING_PERIOD;
  float sine_wave = sin(phase * 2 * MY_PI);
  
  // Convert sine wave (-1 to 1) to PWM duty cycle (0 to 255)
  // Use absolute value to make it always positive, then scale
  int pwm_value = (int)(127.5 + 127.5 * sine_wave);
  
  analogWrite(CPU_STATUS_LED, pwm_value);
  
  // Handle trigger out LED blinking (1000ms)
  if (current_time - last_trigger_blink >= TRIGGER_BLINK_INTERVAL) {
    trigger_led_state = !trigger_led_state;
    digitalWrite(TRIGGER_OUT_LED, trigger_led_state);
    last_trigger_blink = current_time;
  }
  
// Read sensor data every 2 seconds
  if ((sht4_available || tsl_available || mlx90641_available) && (current_time - last_measurement >= MEASUREMENT_INTERVAL)) {
    if (sht4_available) {
      sensors_event_t humidity, temp;
      
      if (sht4.getEvent(&humidity, &temp)) {
        // Output in CSV format: hygro;<temp>;<humid>;
        Serial.print("hygro;");
        Serial.print(temp.temperature, 2);
        Serial.print(";");
        Serial.print(humidity.relative_humidity, 2);
        Serial.println(";");
      } else {
        Serial.println("hygro;ERROR;ERROR;");
      }
    }
    
    if (tsl_available) {
      uint32_t lum = tsl.getFullLuminosity();
      uint16_t ir, full;

      ir = lum >> 16;
      full = lum & 0xFFFF;
      
      // Calculate actual lux value
      float lux = tsl.calculateLux(full, ir);
      
      // Check if we need to adjust gain (but not too frequently)
      if (current_time - last_gain_adjustment >= GAIN_ADJUSTMENT_INTERVAL) {
        if (adjustGain(full)) {
          last_gain_adjustment = current_time;
          // Skip this measurement after gain change to let sensor settle
          last_measurement = current_time;
          return;
        }
      }

      // Output in CSV format: light;<lux>;<full_raw>;<ir_raw>;<gain_string>;
      Serial.print("light;");
      Serial.print(lux, 2);  // Lux value with 2 decimal places
      Serial.print(";");
      Serial.print(full);    // Raw full spectrum value
      Serial.print(";");
      Serial.print(ir);      // Raw IR value
      Serial.print(";");
      Serial.print(getGainString(current_gain)); // Current gain setting
      Serial.println(";");
    }
    
    if (mlx90641_available) {
      // Check if new data is available
      if (checkNewData()) {
        // Read pixel data
        if (readMultipleRegisters(MLX90641_I2C_ADDR, MLX90641_RAM_START, mlx90641_pixelData, MLX90641_PIXEL_COUNT)) {
          // Calculate basic statistics from raw data
          uint16_t min_val = mlx90641_pixelData[0];
          uint16_t max_val = mlx90641_pixelData[0];
          uint32_t sum_val = 0;
          
          for (int i = 0; i < MLX90641_PIXEL_COUNT; i++) {
            if (mlx90641_pixelData[i] < min_val) min_val = mlx90641_pixelData[i];
            if (mlx90641_pixelData[i] > max_val) max_val = mlx90641_pixelData[i];
            sum_val += mlx90641_pixelData[i];
          }
          
          float avg_val = (float)sum_val / (float)MLX90641_PIXEL_COUNT;
          
          // Output in CSV format: thermal;min_raw;max_raw;avg_raw;
          Serial.print("thermal;");
          Serial.print(min_val);
          Serial.print(";");
          Serial.print(max_val);
          Serial.print(";");
          Serial.print(avg_val, 2);
          Serial.println(";");
          
          // Clear new data bit
          clearNewDataBit();
        } else {
          Serial.println("thermal;ERROR;ERROR;ERROR;");
        }
      } else {
        Serial.println("thermal;NO_NEW_DATA;NO_NEW_DATA;NO_NEW_DATA;");
      }
    }

last_measurement = current_time;
  }

  delay(10); // Small delay to prevent busy waiting
}

// MLX90641 helper functions implementation (from fw_melexis)
bool readRegister(byte deviceAddress, uint16_t registerAddress, uint16_t* data) {
  Wire1.beginTransmission(deviceAddress);
  Wire1.write(registerAddress >> 8);   // MSB
  Wire1.write(registerAddress & 0xFF); // LSB
  
  if (Wire1.endTransmission(false) != 0) {
    return false;
  }
  
  Wire1.requestFrom(deviceAddress, (uint8_t)2);
  if (Wire1.available() >= 2) {
    *data = Wire1.read() << 8;  // MSB
    *data |= Wire1.read();      // LSB
    return true;
  }
  
  return false;
}

bool readMultipleRegisters(byte deviceAddress, uint16_t startAddress, uint16_t* data, uint16_t length) {
  // Read in smaller chunks to avoid I2C issues
  const uint16_t CHUNK_SIZE = 16; // Read 16 words at a time
  uint16_t wordsRead = 0;
  
  while (wordsRead < length) {
    uint16_t wordsToRead = min(CHUNK_SIZE, length - wordsRead);
    uint16_t currentAddress = startAddress + wordsRead;
    
    Wire1.beginTransmission(deviceAddress);
    Wire1.write(currentAddress >> 8);   // MSB
    Wire1.write(currentAddress & 0xFF); // LSB
    
    if (Wire1.endTransmission(false) != 0) {
      return false;
    }
    
    Wire1.requestFrom(deviceAddress, (uint8_t)(wordsToRead * 2));
    
    for (uint16_t i = 0; i < wordsToRead; i++) {
      if (Wire1.available() >= 2) {
        data[wordsRead + i] = Wire1.read() << 8;  // MSB
        data[wordsRead + i] |= Wire1.read();      // LSB
      } else {
        return false;
      }
    }
    
    wordsRead += wordsToRead;
    delay(5); // Small delay between chunks
  }
  
  return true;
}

bool checkNewData() {
  uint16_t statusReg = 0;
  if (readRegister(MLX90641_I2C_ADDR, MLX90641_STATUS_REG, &statusReg)) {
    // Check bit 3 - "New data available in RAM"
    return (statusReg & 0x0008) != 0;
  }
  return false;
}

void clearNewDataBit() {
  uint16_t statusReg = 0;
  if (readRegister(MLX90641_I2C_ADDR, MLX90641_STATUS_REG, &statusReg)) {
    // Clear bit 3 by writing back the status with bit 3 cleared
    statusReg &= ~0x0008;
    
    Wire1.beginTransmission(MLX90641_I2C_ADDR);
    Wire1.write(MLX90641_STATUS_REG >> 8);   // MSB
    Wire1.write(MLX90641_STATUS_REG & 0xFF); // LSB
    Wire1.write(statusReg >> 8);             // Data MSB
    Wire1.write(statusReg & 0xFF);           // Data LSB
    Wire1.endTransmission();
  }
}
