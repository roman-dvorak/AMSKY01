#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include "AMS_TSL2591.h"
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

// Initialize AMS TSL2591 light sensor
AMS_TSL2591 amsSensor;

// MLX90641 thermal sensor variables
uint16_t mlx90641_pixelData[MLX90641_PIXEL_COUNT];
bool mlx90641_available = false;

// MLX90641 helper functions
bool writeRegister(byte deviceAddress, uint16_t registerAddress, uint16_t data);
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


typedef struct {
  int16_t offset;
  float alpha;
  float kta;
  float kv;
} PixelParams;


void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(2000);
  
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
  
  // Initialize AMS TSL2591 light sensor on I2C1
  if (amsSensor.begin(&Wire1)) {
    tsl_available = true;
  } else {
    tsl_available = false;
  }
  
  // Initialize MLX90641 thermal sensor
  Serial.println("Testing MLX90641 thermal sensor...");
  Wire1.beginTransmission(MLX90641_I2C_ADDR);
  if (Wire1.endTransmission() == 0) {
    mlx90641_available = true;
    Serial.println("MLX90641 thermal sensor initialized successfully");

    // MLX90641 initialization - set control register
    uint16_t ctrlReg1;
    if (readRegister(MLX90641_I2C_ADDR, 0x800D, &ctrlReg1)) {
      ctrlReg1 = (ctrlReg1 & 0xFC1F) | (0b100 << 5) | (0b10 << 2);
      writeRegister(MLX90641_I2C_ADDR, 0x800D, ctrlReg1);
      Serial.println("MLX90641 control register initialized");
    } else {
      Serial.println("Failed to read MLX90641 control register");
    }
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
  
  // Convert sine wave (-1 to 1) to PWM duty cycle (0 to 64) - reduced brightness
  // Use absolute value to make it always positive, then scale to 25% of original brightness
  int pwm_value = (int)(32 + 32 * sine_wave);
  
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
        // Output in CSV format: hygro,\u003ctemp\u003e,\u003chumid\u003e
        Serial.print("$hygro,");
        Serial.print(temp.temperature, 2);
        Serial.print(",");
        Serial.print(humidity.relative_humidity, 2);
        Serial.println();
      } else {
        Serial.println("$hygro,-999,-999");
      }
    }
    
    if (tsl_available && amsSensor.isAvailable()) {
      float normalized_lux;
      uint16_t full_raw, ir_raw;
      const char* gain_str;
      const char* integration_time_str;
      
      if (amsSensor.readLightData(normalized_lux, full_raw, ir_raw, gain_str, integration_time_str)) {
        // Output in CSV format: light,normalized_lux,full_raw,ir_raw,gain,integration_time
        Serial.print("$light,");
        Serial.print(normalized_lux, 2);  // Normalized lux value with 2 decimal places
        Serial.print(",");
        Serial.print(full_raw);    // Raw full spectrum value
        Serial.print(",");
        Serial.print(ir_raw);      // Raw IR value
        Serial.print(",");
        Serial.print(gain_str); // Current gain setting
        Serial.print(",");
        Serial.print(integration_time_str); // Current integration time setting
        Serial.println();
      } else {
        // Settings were adjusted, skip this measurement
        last_measurement = current_time;
        return;
      }
    }
    if (mlx90641_available) {
























if (checkNewData()) {

  PixelParams pixels[192];
  uint16_t eepromData[832];
  readMultipleRegisters(MLX90641_I2C_ADDR, 0x2400, eepromData, 832);

  // Per-pixel offset
  for (int i = 0; i < 192; i++) {
    pixels[i].offset = (int16_t)eepromData[512 + i];
  }

  // Alpha ref a scale
  uint8_t alphaScale = (eepromData[32] >> 12) & 0xF;
  float alphaRef = (float)eepromData[33] / powf(2.0, alphaScale);

  for (int i = 0; i < 192; i++) {
    int16_t alphaVal = eepromData[384 + i];
    pixels[i].alpha = (alphaVal / powf(2.0, alphaScale));
  }

  // Kta a Kv (dle dokumentace jsou 6bitové signed hodnoty)
  int8_t ktaScale1 = (eepromData[56] >> 8) & 0xF;
  int8_t kvScale = (eepromData[56] >> 4) & 0xF;

  for (int i = 0; i < 192; i++) {
    int8_t rawKta = (int8_t)((eepromData[640 + i] >> 8) & 0xFF);
    int8_t rawKv  = (int8_t)(eepromData[640 + i] & 0xFF);
    pixels[i].kta = rawKta / powf(2.0f, ktaScale1);
    pixels[i].kv  = rawKv  / powf(2.0f, kvScale);
  }

  // Čtení RAM + pomocných registrů
  if (readMultipleRegisters(MLX90641_I2C_ADDR, MLX90641_RAM_START, mlx90641_pixelData, MLX90641_PIXEL_COUNT)) {
    uint16_t ptat_raw, vbe_raw, vddpix_raw;
    readRegister(MLX90641_I2C_ADDR, 0x05A0, &ptat_raw);
    readRegister(MLX90641_I2C_ADDR, 0x0580, &vbe_raw);
    readRegister(MLX90641_I2C_ADDR, 0x05AA, &vddpix_raw);

    uint16_t vdd25_raw, kvdd_raw, ktptat_raw, kvptat_raw, alphaptat_raw;
    uint16_t ptat25_raw_1, ptat25_raw_2;
    readRegister(MLX90641_I2C_ADDR, 0x2426, &vdd25_raw);
    readRegister(MLX90641_I2C_ADDR, 0x2427, &kvdd_raw);
    readRegister(MLX90641_I2C_ADDR, 0x242A, &ktptat_raw);
    readRegister(MLX90641_I2C_ADDR, 0x242B, &kvptat_raw);
    readRegister(MLX90641_I2C_ADDR, 0x242C, &alphaptat_raw);
    readRegister(MLX90641_I2C_ADDR, 0x2428, &ptat25_raw_1);
    readRegister(MLX90641_I2C_ADDR, 0x2429, &ptat25_raw_2);

    // Výpočet Vdd
    int16_t vddpix = (int16_t)vddpix_raw;
    int16_t vdd25 = (vdd25_raw & 0x07FF); if (vdd25 > 1023) vdd25 -= 2048; vdd25 *= 25;
    int16_t kvdd = (kvdd_raw & 0x07FF); if (kvdd > 1023) kvdd -= 2048; kvdd *= 25;
    float vdd = ((float)vddpix - vdd25) / (float)kvdd + 3.3f;

    // Výpočet Ta
    int16_t ptat = (int16_t)ptat_raw;
    int16_t vbe = (int16_t)vbe_raw;

    float kvptat = (kvptat_raw & 0x07FF); if (kvptat > 1023) kvptat -= 2048; kvptat = kvptat / 4096.0f;
    float ktptat = (ktptat_raw & 0x07FF); if (ktptat > 1023) ktptat -= 2048; ktptat = ktptat / 8.0f;
    float alpha_ptat = (alphaptat_raw & 0x07FF) / 134217728.0f;
    uint16_t ptat25 = 32 * (ptat25_raw_1 & 0x07FF) + (ptat25_raw_2 & 0x07FF);

    float deltaV = ((float)vddpix - vdd25) / (float)kvdd;
    float v_ptat = ptat / (ptat * alpha_ptat + vbe);
    float v_ptat_art = v_ptat * 262144.0f;
    float Ta = ((v_ptat_art / (1.0f + kvptat * deltaV)) - ptat25) / ktptat + 25.0f;
    Ta /= 10.0f;

    // Serial.println("# MLX90641 raw pixel data");
    // for (int i = 0; i < 192; i++) {
    //   Serial.print(mlx90641_pixelData[i]);
    //   if (i < 767) Serial.print(",");
    // }

    // Serial.println("# MLX90641 offset matrix");
    // for (int i = 0; i < 192; i++) {
    //   Serial.print(pixels[i].offset);
    //   if (i < 767) Serial.print(",");
    // }

    // Serial.println();

    // Serial.println("# MLX90641 alpha matrix");
    // for (int i = 0; i < 192; i++) {
    //   Serial.print(pixels[i].alpha, 6);
    //   if (i < 767) Serial.print(",");
    // }
    
    // Serial.println();

    // Serial.println("# MLX90641 kta matrix");
    // for (int i = 0; i < 192; i++) {
    //   Serial.print(pixels[i].kta, 6);
    //   if (i < 767) Serial.print(",");
    // }
    
    // Serial.println();

    // Serial.println("# MLX90641 kv matrix");
    // for (int i = 0; i < 192; i++) {
    //   Serial.print(pixels[i].kv, 6);
    //   if (i < 767) Serial.print(",");
    // } 

    // Serial.println();

    // === Výpočet teplot pixelů ===
    //Serial.println("CorrectedPixels");
    for (int i = 0; i < 192; i++) {
      int16_t raw = mlx90641_pixelData[i];
      float irData = (float)raw - pixels[i].offset;
      irData -= pixels[i].kta * (Ta - 25.0f);
      irData -= pixels[i].kv * (vdd - 3.3f);
      
      float ir_corrected = irData / pixels[i].alpha;
      float temperature = Ta + ir_corrected * 0.01f;
      
      if (!isfinite(temperature)) {
        //Serial.print("ovf");
      } else {
        //Serial.print(temperature, 2);
      }

      //if (i < 767) Serial.print(",");
    }
    Serial.println();

    Serial.println(String("$thr_parameters,") + vdd + "," + Ta);


    float TL = 0, TR = 0, BL = 0, BR = 0, CTR = 0;
    int count = 0;

    // Levý horní roh (TL): řádky 0-4, sloupce 0-4
    count = 0;
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 5; c++) {
      TL += mlx90641_pixelData[r * 12 + c];
      count++;
      }
    }
    TL /= count;

    // Pravý horní roh (TR): řádky 0-4, sloupce 7-11
    count = 0;
    for (int r = 0; r < 5; r++) {
      for (int c = 7; c < 12; c++) {
      TR += mlx90641_pixelData[r * 12 + c];
      count++;
      }
    }
    TR /= count;

    // Levý dolní roh (BL): řádky 11-15, sloupce 0-4
    count = 0;
    for (int r = 11; r < 16; r++) {
      for (int c = 0; c < 5; c++) {
      BL += mlx90641_pixelData[r * 12 + c];
      count++;
      }
    }
    BL /= count;

    // Pravý dolní roh (BR): řádky 11-15, sloupce 7-11
    count = 0;
    for (int r = 11; r < 16; r++) {
      for (int c = 7; c < 12; c++) {
      BR += mlx90641_pixelData[r * 12 + c];
      count++;
      }
    }
    BR /= count;

    // Střed (CTR): řádky 5-9, sloupce 3-7
    count = 0;
    for (int r = 5; r < 10; r++) {
      for (int c = 3; c < 8; c++) {
      CTR += mlx90641_pixelData[r * 12 + c];
      count++;
      }
    }
    CTR /= count;

    Serial.print("$cloud,");
    Serial.print(TL, 2); Serial.print(",");
    Serial.print(TR, 2); Serial.print(",");
    Serial.print(BL, 2); Serial.print(",");
    Serial.print(BR, 2); Serial.print(",");
    Serial.print(CTR, 2);
    Serial.println();


    

    clearNewDataBit();
  } else {
    Serial.println("# Failed to read pixel data");
  }
} else {
  Serial.println("# No new data available");
}
}


















last_measurement = current_time;
  }

  delay(10); // Small delay to prevent busy waiting
}



bool writeRegister(uint8_t i2c_addr, uint16_t reg, uint16_t value) {
  Wire1.beginTransmission(i2c_addr);
  Wire1.write(reg >> 8);
  Wire1.write(reg & 0xFF);
  Wire1.write(value >> 8);
  Wire1.write(value & 0xFF);
  return Wire1.endTransmission() == 0;
}

bool readRegister(uint8_t i2c_addr, uint16_t reg, uint16_t* value) {
  // Zapiš adresu registru
  Wire1.beginTransmission(i2c_addr);
  Wire1.write(reg >> 8);
  Wire1.write(reg & 0xFF);

  if (Wire1.endTransmission(false) != 0) {
    Serial.println("# I2C write address phase failed!");
    return false;
  }

  // Čti 2 bajty ze zařízení
  Wire1.requestFrom(i2c_addr, (uint8_t)2);
  if (Wire1.available() < 2) {
    Serial.println("# I2C read failed – not enough bytes!");
    return false;
  }

  uint16_t raw = (Wire1.read() << 8) | Wire1.read();

  // Pokud adresa spadá do EEPROM rozsahu, odstraň Hamming
  if (reg >= 0x2400 && reg <= 0x273F) {
    *value = raw & 0x07FF; // pouze bity D0–D10
  } else {
    *value = raw; // ostatní čteme celé
  }

  //Serial.print("# Read 0x"); Serial.print(reg, HEX);
  //Serial.print(" = 0x"); Serial.print(raw, HEX);
  //Serial.print(" → data = 0x"); Serial.println(*value, HEX);

  return true;
}




bool readMultipleRegisters(byte deviceAddress, uint16_t startAddress, uint16_t* data, uint16_t length) {
  const uint16_t CHUNK_SIZE = 16;
  uint16_t wordsRead = 0;
  
  while (wordsRead < length) {
    uint16_t wordsToRead = min(CHUNK_SIZE, length - wordsRead);
    uint16_t currentAddress = startAddress + wordsRead;

    Wire1.beginTransmission(deviceAddress);
    Wire1.write(currentAddress >> 8);
    Wire1.write(currentAddress & 0xFF);
    
    if (Wire1.endTransmission(false) != 0) {
      Serial.print("# I2C write failed at reg 0x");
      Serial.println(currentAddress, HEX);
      return false;
    }

    Wire1.requestFrom(deviceAddress, (uint8_t)(wordsToRead * 2));
    
    for (uint16_t i = 0; i < wordsToRead; i++) {
      if (Wire1.available() >= 2) {
        data[wordsRead + i] = (Wire1.read() << 8) | Wire1.read();

        if ((startAddress + wordsRead + i) >= 0x0400 && (startAddress + wordsRead + i) < 0x0800) {
          data[wordsRead + i] &= 0x07FF; // clear Hamming bits for RAM addresses only
        }

      } else {
        Serial.println("# I2C read underflow!");
        return false;
      }
    }

    wordsRead += wordsToRead;
    delay(5);
  }
  
  return true;
}


bool checkNewData() {
  uint16_t statusReg = 0;
  if (readRegister(MLX90641_I2C_ADDR, MLX90641_STATUS_REG, &statusReg)) {
    // Check bit 3 - "New data available in RAM"
    return (statusReg & 0x0008) != 0;
  }

  Serial.println("#Failed to read MLX90641 status register");
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
