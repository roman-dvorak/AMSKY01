#include "MLX90641.h"

MLX90641::MLX90641() {
    _wire = nullptr;
    initialized = false;
}

bool MLX90641::begin(TwoWire *wire) {
    _wire = wire;
    
    // Test I2C connection
    _wire->beginTransmission(MLX90641_I2C_ADDR);
    if (_wire->endTransmission() != 0) {
        Serial.println("MLX90641 thermal sensor initialization failed");
        return false;
    }
    
    Serial.println("MLX90641 thermal sensor initialized successfully");
    
    // Initialize control register
    uint16_t ctrlReg1;
    if (readRegister(MLX90641_I2C_ADDR, 0x800D, &ctrlReg1)) {
        ctrlReg1 = (ctrlReg1 & 0xFC1F) | (0b100 << 5) | (0b10 << 2);
        writeRegister(MLX90641_I2C_ADDR, 0x800D, ctrlReg1);
        Serial.println("MLX90641 control register initialized");
    } else {
        Serial.println("Failed to read MLX90641 control register");
        return false;
    }
    
    // Load calibration data
    loadCalibrationData();
    initialized = true;
    return true;
}

bool MLX90641::isAvailable() const {
    return initialized;
}

void MLX90641::loadCalibrationData() {
    uint16_t eepromData[832];
    if (!readMultipleRegisters(MLX90641_I2C_ADDR, 0x2400, eepromData, 832)) {
        Serial.println("Failed to read EEPROM data");
        return;
    }

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
}

bool MLX90641::readThermalData(float& vdd, float& ta, float corners[4], float& center) {
    if (!initialized || !checkNewData()) {
        return false;
    }

    // Čtení RAM + pomocných registrů
    if (!readMultipleRegisters(MLX90641_I2C_ADDR, MLX90641_RAM_START, pixelData, MLX90641_PIXEL_COUNT)) {
        return false;
    }

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
    int16_t vdd25 = (vdd25_raw & 0x07FF); 
    if (vdd25 > 1023) vdd25 -= 2048; 
    vdd25 *= 25;
    int16_t kvdd = (kvdd_raw & 0x07FF); 
    if (kvdd > 1023) kvdd -= 2048; 
    kvdd *= 25;
    vdd = ((float)vddpix - vdd25) / (float)kvdd + 3.3f;

    // Výpočet Ta
    int16_t ptat = (int16_t)ptat_raw;
    int16_t vbe = (int16_t)vbe_raw;

    float kvptat = (kvptat_raw & 0x07FF); 
    if (kvptat > 1023) kvptat -= 2048; 
    kvptat = kvptat / 4096.0f;
    float ktptat = (ktptat_raw & 0x07FF); 
    if (ktptat > 1023) ktptat -= 2048; 
    ktptat = ktptat / 8.0f;
    float alpha_ptat = (alphaptat_raw & 0x07FF) / 134217728.0f;
    uint16_t ptat25 = 32 * (ptat25_raw_1 & 0x07FF) + (ptat25_raw_2 & 0x07FF);

    float deltaV = ((float)vddpix - vdd25) / (float)kvdd;
    float v_ptat = ptat / (ptat * alpha_ptat + vbe);
    float v_ptat_art = v_ptat * 262144.0f;
    ta = ((v_ptat_art / (1.0f + kvptat * deltaV)) - ptat25) / ktptat + 25.0f;
    ta /= 10.0f;

    // Calculate corner averages
    float TL = 0, TR = 0, BL = 0, BR = 0, CTR = 0;
    int count = 0;

    // Levý horní roh (TL): řádky 0-4, sloupce 0-4
    count = 0;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 5; c++) {
            TL += pixelData[r * 12 + c];
            count++;
        }
    }
    TL /= count;

    // Pravý horní roh (TR): řádky 0-4, sloupce 7-11
    count = 0;
    for (int r = 0; r < 5; r++) {
        for (int c = 7; c < 12; c++) {
            TR += pixelData[r * 12 + c];
            count++;
        }
    }
    TR /= count;

    // Levý dolní roh (BL): řádky 11-15, sloupce 0-4
    count = 0;
    for (int r = 11; r < 16; r++) {
        for (int c = 0; c < 5; c++) {
            BL += pixelData[r * 12 + c];
            count++;
        }
    }
    BL /= count;

    // Pravý dolní roh (BR): řádky 11-15, sloupce 7-11
    count = 0;
    for (int r = 11; r < 16; r++) {
        for (int c = 7; c < 12; c++) {
            BR += pixelData[r * 12 + c];
            count++;
        }
    }
    BR /= count;

    // Střed (CTR): řádky 6-9, sloupce 4-7
    count = 0;
    for (int r = 6; r < 10; r++) {
        for (int c = 4; c < 8; c++) {
            CTR += pixelData[r * 12 + c];
            count++;
        }
    }
    CTR /= count;

    // Store corner values
    corners[0] = TL;
    corners[1] = TR;
    corners[2] = BL;
    corners[3] = BR;
    center = CTR;

    clearNewDataBit();
    return true;
}

bool MLX90641::writeRegister(uint8_t deviceAddress, uint16_t reg, uint16_t value) {
    _wire->beginTransmission(deviceAddress);
    _wire->write(reg >> 8);
    _wire->write(reg & 0xFF);
    _wire->write(value >> 8);
    _wire->write(value & 0xFF);
    return _wire->endTransmission() == 0;
}

bool MLX90641::readRegister(uint8_t i2c_addr, uint16_t reg, uint16_t* value) {
    // Zapiš adresu registru
    _wire->beginTransmission(i2c_addr);
    _wire->write(reg >> 8);
    _wire->write(reg & 0xFF);

    if (_wire->endTransmission(false) != 0) {
        Serial.println("# I2C write address phase failed!");
        return false;
    }

    // Čti 2 bajty ze zařízení
    _wire->requestFrom(i2c_addr, (uint8_t)2);
    if (_wire->available() < 2) {
        Serial.println("# I2C read failed – not enough bytes!");
        return false;
    }

    uint16_t raw = (_wire->read() << 8) | _wire->read();

    // Pokud adresa spadá do EEPROM rozsahu, odstraň Hamming
    if (reg >= 0x2400 && reg <= 0x273F) {
        *value = raw & 0x07FF; // pouze bity D0–D10
    } else {
        *value = raw; // ostatní čteme celé
    }

    return true;
}

bool MLX90641::readMultipleRegisters(uint8_t deviceAddress, uint16_t startAddress, uint16_t* data, uint16_t length) {
    const uint16_t CHUNK_SIZE = 16;
    uint16_t wordsRead = 0;
  
    while (wordsRead < length) {
        uint16_t wordsToRead = min(CHUNK_SIZE, length - wordsRead);
        uint16_t currentAddress = startAddress + wordsRead;

        _wire->beginTransmission(deviceAddress);
        _wire->write(currentAddress >> 8);
        _wire->write(currentAddress & 0xFF);
    
        if (_wire->endTransmission(false) != 0) {
            Serial.print("# I2C write failed at reg 0x");
            Serial.println(currentAddress, HEX);
            return false;
        }

        _wire->requestFrom(deviceAddress, (uint8_t)(wordsToRead * 2));
    
        for (uint16_t i = 0; i < wordsToRead; i++) {
            if (_wire->available() >= 2) {
                data[wordsRead + i] = (_wire->read() << 8) | _wire->read();

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

bool MLX90641::checkNewData() {
    uint16_t statusReg = 0;
    if (readRegister(MLX90641_I2C_ADDR, MLX90641_STATUS_REG, &statusReg)) {
        // Check bit 3 - "New data available in RAM"
        return (statusReg & 0x0008) != 0;
    }

    Serial.println("#Failed to read MLX90641 status register");
    return false;
}

void MLX90641::clearNewDataBit() {
    uint16_t statusReg = 0;
    if (readRegister(MLX90641_I2C_ADDR, MLX90641_STATUS_REG, &statusReg)) {
        // Clear bit 3 by writing back the status with bit 3 cleared
        statusReg &= ~0x0008;
    
        _wire->beginTransmission(MLX90641_I2C_ADDR);
        _wire->write(MLX90641_STATUS_REG >> 8);   // MSB
        _wire->write(MLX90641_STATUS_REG & 0xFF); // LSB
        _wire->write(statusReg >> 8);             // Data MSB
        _wire->write(statusReg & 0xFF);           // Data LSB
        _wire->endTransmission();
    }
}

void MLX90641::printRawPixelData() {
    Serial.println("# MLX90641 raw pixel data");
    for (int i = 0; i < MLX90641_PIXEL_COUNT; i++) {
        Serial.print(pixelData[i]);
        if (i < MLX90641_PIXEL_COUNT - 1) Serial.print(",");
    }
    Serial.println();
}

void MLX90641::printCalibrationData() {
    Serial.println("# MLX90641 offset matrix");
    for (int i = 0; i < MLX90641_PIXEL_COUNT; i++) {
        Serial.print(pixels[i].offset);
        if (i < MLX90641_PIXEL_COUNT - 1) Serial.print(",");
    }
    Serial.println();

    Serial.println("# MLX90641 alpha matrix");
    for (int i = 0; i < MLX90641_PIXEL_COUNT; i++) {
        Serial.print(pixels[i].alpha, 6);
        if (i < MLX90641_PIXEL_COUNT - 1) Serial.print(",");
    }
    Serial.println();

    Serial.println("# MLX90641 kta matrix");
    for (int i = 0; i < MLX90641_PIXEL_COUNT; i++) {
        Serial.print(pixels[i].kta, 6);
        if (i < MLX90641_PIXEL_COUNT - 1) Serial.print(",");
    }
    Serial.println();

    Serial.println("# MLX90641 kv matrix");
    for (int i = 0; i < MLX90641_PIXEL_COUNT; i++) {
        Serial.print(pixels[i].kv, 6);
        if (i < MLX90641_PIXEL_COUNT - 1) Serial.print(",");
    }
    Serial.println();
}
