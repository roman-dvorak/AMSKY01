#ifndef AMS_TSL2591_H
#define AMS_TSL2591_H

#include <Arduino.h>
#include <Adafruit_TSL2591.h>

class AMS_TSL2591 {
private:
    Adafruit_TSL2591 tsl;
    
    // Auto-gain variables
    tsl2591Gain_t current_gain;
    tsl2591IntegrationTime_t current_integration_time;
    unsigned long last_gain_adjustment;
    uint16_t previous_measurement;
    bool improvement_detected;
    
    // Thresholds
    static const unsigned long GAIN_ADJUSTMENT_INTERVAL = 5000;
    static const uint16_t GAIN_SATURATED_THRESHOLD = 60000;
    static const uint16_t EXTREME_SATURATED_THRESHOLD = 64000;
    static const uint16_t GAIN_TOO_LOW_THRESHOLD = 2000;
    static const uint16_t INTEGRATION_TIME_INCREASE_THRESHOLD = 1500;
    static const uint16_t INTEGRATION_TIME_DECREASE_THRESHOLD = 50000;
    
    // Smart alternating adjustment tracking
    enum LastAdjustmentType {
        ADJUST_NONE = 0,
        ADJUST_GAIN = 1,
        ADJUST_INTEGRATION = 2,
        ADJUST_BOTH = 3
    };
    LastAdjustmentType last_adjustment_type;
    
    // Helper functions
    const char* getGainString(tsl2591Gain_t gain);
    float getGainValue(tsl2591Gain_t gain);
    float getIntegrationTimeMs(tsl2591IntegrationTime_t integrationTime);
    const char* getIntegrationTimeString(tsl2591IntegrationTime_t integrationTime);
    bool adjustGainAndIntegrationTime(uint16_t full_value);
    
public:
    // Constructor
    AMS_TSL2591();
    
    // Initialization
    bool begin(TwoWire *wire = &Wire);
    
    // Status
    bool isAvailable() const;
    
    // Measurement
    bool readLightData(float &normalized_lux, uint16_t &full_raw, uint16_t &ir_raw, 
                       const char* &gain_str, const char* &integration_time_str);
    
    // Settings
    tsl2591Gain_t getCurrentGain() const { return current_gain; }
    tsl2591IntegrationTime_t getCurrentIntegrationTime() const { return current_integration_time; }
};

#endif
