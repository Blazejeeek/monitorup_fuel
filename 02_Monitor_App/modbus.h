// Monitor App - serwer Modbus TCP (mapa rejestrow: MODBUS.md)
#pragma once
#include <Arduino.h>

uint8_t mbClientCount();
void mbApply();                    // po zmianie ustawien
void mbLoop();
