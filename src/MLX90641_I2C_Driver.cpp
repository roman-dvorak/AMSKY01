/**
 * @copyright (C) 2017 Melexis N.V.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <Arduino.h>
#include <Wire.h>
#include "MLX90641_I2C_Driver.h"

extern TwoWire Wire1;

void MLX90641_I2CInit()
{   
    // I2C is already initialized in main setup
    // Wire1.end();
}

int MLX90641_I2CGeneralReset(void)
{    
    int ack;
    char cmd[2] = {0,0};
    
    cmd[0] = 0x00;
    cmd[1] = 0x06;    

    Wire1.endTransmission();
    delayMicroseconds(5);
    Wire1.beginTransmission(cmd[0]);
    Wire1.write(cmd[1]);
    ack = Wire1.endTransmission();
    
    if (ack != 0x00)
    {
        return -1;
    }         
    Wire1.endTransmission();
    
    delayMicroseconds(50);
    
    return 0;
}

int MLX90641_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data)
{
    uint8_t sa;                           
    int ack = 0;                               
    int cnt = 0;
    int i = 0;
    char cmd[2] = {0,0};
    char i2cData[1664] = {0};
    uint16_t *p;
    
    p = data;
    sa = slaveAddr;
    cmd[0] = startAddress >> 8;
    cmd[1] = startAddress & 0x00FF;
    
    Wire1.endTransmission();
    delayMicroseconds(5);
    Wire1.beginTransmission(sa);
    Wire1.write(cmd, 2);
    ack = Wire1.endTransmission(false);
    
    if (ack != 0x00)
    {
        return -1;
    }
             
    Wire1.requestFrom(sa, (uint8_t)(2*nMemAddressRead));
    for (int i = 0; i < 2*nMemAddressRead; i++) {
        if (Wire1.available()) {
            i2cData[i] = Wire1.read();
        }
    }
    ack = 0;
    
    if (ack != 0x00)
    {
        return -1; 
    }          
    Wire1.endTransmission();
    
    for(cnt=0; cnt < nMemAddressRead; cnt++)
    {
        i = cnt << 1;
        *p++ = (uint16_t)i2cData[i]*256 + (uint16_t)i2cData[i+1];
    }
    
    return 0;   
} 

void MLX90641_I2CFreqSet(int freq)
{
    Wire1.setClock(1000*freq);
}

int MLX90641_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
    uint8_t sa;
    int ack = 0;
    char cmd[4] = {0,0,0,0};
    static uint16_t dataCheck;
    

    sa = slaveAddr;
    cmd[0] = writeAddress >> 8;
    cmd[1] = writeAddress & 0x00FF;
    cmd[2] = data >> 8;
    cmd[3] = data & 0x00FF;

    Wire1.endTransmission();
    delayMicroseconds(5);
    Wire1.beginTransmission(sa);
    Wire1.write(cmd, 4);
    ack = Wire1.endTransmission();
    
    if (ack != 0x00)
    {
        return -1;
    }         
    Wire1.endTransmission();
    
    MLX90641_I2CRead(slaveAddr,writeAddress,1, &dataCheck);
    
    if ( dataCheck != data)
    {
        return -2;
    }    
    
    return 0;
}

