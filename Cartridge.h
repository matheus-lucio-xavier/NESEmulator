#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "Mapper_000.h"

class Cartridge {
public:
	Cartridge(const std::string& sFileName);
	~Cartridge();

public:
	bool ImageValid();

private:
	bool bImageValid = false;
    MIRROR hw_mirror = HORIZONTAL;

	std::vector<uint8_t> vPRGMemory;
	std::vector<uint8_t> vCHRMemory;

	uint8_t nMapperID = 0x00;
	uint8_t nPRGBanks = 0x00;
	uint8_t nCHRBanks = 0x00;

	std::shared_ptr<Mapper> pMapper;

public:
	// comunicacao com o bus principal
	bool cpuRead(uint16_t addr, uint8_t& data);
	bool cpuWrite(uint16_t addr, uint8_t data);

	// // comunicacao com a ppu do bus
	bool ppuRead(uint16_t addr, uint8_t& data);
	bool ppuWrite(uint16_t addr, uint8_t data);

    // Permits system rest of mapper to know state
    void reset();

    // Get Mirror configuration
    MIRROR Mirror();

    std::shared_ptr<Mapper> GetMapper();
};

