#pragma once
#include <cstdint>
#include <array>

#include "olc6502.h"
#include "olc2C02.h"

class Bus
{
public:
	Bus();
	~Bus();

public: // Devices on bus
	olc6502 cpu;

	olc2C02 ppu;

	// Fake RAM for this part of the series
	std::array<uint8_t, 2048> cpuRam;


public: // Bus Read & Write
	void cpuWrite(uint16_t addr, uint8_t data);
	uint8_t cpuRead(uint16_t addr, bool bReadOnly = false);
};
