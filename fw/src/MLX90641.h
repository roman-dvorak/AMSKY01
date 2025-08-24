#ifndef MLX90641_H
#define MLX90641_H

#include <Arduino.h>
#include <Wire.h>

// MLX90641 definitions
#define MLX90641_I2C_ADDR 0x33
#define MLX90641_RAM_START 0x0400
#define MLX90641_PIXEL_COUNT 192
#define MLX90641_STATUS_REG 0x8000

typedef struct {
  int16_t offset;
  float alpha;
  float kta;
  float kv;
} PixelParams;

class MLX90641 {
private:
    TwoWire* _wire;
    uint16_t pixelData[MLX90641_PIXEL_COUNT];
    PixelParams pixels[MLX90641_PIXEL_COUNT];
    bool initialized;
    
    // Helper functions
    bool writeRegister(uint8_t deviceAddress, uint16_t registerAddress, uint16_t data);
    bool readRegister(uint8_t deviceAddress, uint16_t registerAddress, uint16_t* data);
    bool readMultipleRegisters(uint8_t deviceAddress, uint16_t startAddress, uint16_t* data, uint16_t length);
    bool checkNewData();
    void clearNewDataBit();
    void loadCalibrationData();
    
public:
    // Constructor
    MLX90641();
    
    // Initialization
    bool begin(TwoWire *wire = &Wire);
    
    // Status
    bool isAvailable() const;
    
    // Measurement
    bool readThermalData(float& vdd, float& ta, float corners[4], float& center);
    
    // Debug functions
    void printRawPixelData();
    void printCalibrationData();
};

#endif
