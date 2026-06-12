#pragma once

#include <cstdint>

class Mapper
{
public:
	Mapper(uint8_t prgBanks, uint8_t chrBanks);
	~Mapper();

	// Transforme o endereço do bus da CPU em CHR ROM offset
	virtual bool cpuMapRead(uint16_t addr, uint32_t& mapped_addr) = 0;
	virtual bool cpuMapWrite(uint16_t addr, uint32_t& mapped_addr) = 0;

	// Transforme o endereço do bus da PPU em CHR ROM offset
	virtual bool ppuMapRead(uint16_t addr, uint32_t& mapped_addr) = 0;
	virtual bool ppuMapWrite(uint16_t addr, uint32_t& mapped_addr) = 0;

protected:
	// Estes são armazenados localmente, pois muitos dos mapeadores precisam dessas informações
	uint8_t nPRGBanks = 0;
	uint8_t nCHRBanks = 0;
};

