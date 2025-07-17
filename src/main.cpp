#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
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

// Variables for LEDs
bool trigger_led_state = false;
unsigned long last_trigger_blink = 0;
const unsigned long TRIGGER_BLINK_INTERVAL = 1000; // 1000ms - slower blink

// CPU LED PWM breathing effect
const unsigned long CPU_BREATHING_PERIOD = 2000; // 2 seconds for full cycle
const float MY_PI = 3.14159265359;

// Variables for sensor
bool sensor_available = false;
unsigned long last_measurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 2000; // 2 seconds

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
    sensor_available = true;
    
    // Set precision and heater
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
  } else {
    sensor_available = false;
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
  if (sensor_available && (current_time - last_measurement >= MEASUREMENT_INTERVAL)) {
    sensors_event_t humidity, temp;
    
    if (sht4.getEvent(&humidity, &temp)) {
      // Output in CSV format: hygro;<temp>;<humid>
      Serial.print("hygro;");
      Serial.print(temp.temperature, 2);
      Serial.print(";");
      Serial.println(humidity.relative_humidity, 2);
    } else {
      Serial.println("hygro;ERROR;ERROR");
    }
    
    last_measurement = current_time;
  }
  
  delay(10); // Small delay to prevent busy waiting
}
