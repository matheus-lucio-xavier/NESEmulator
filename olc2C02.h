#pragma once

#include <cstdint>
#include <memory>

#include "Cartridge.h"
#include "olcPixelGameEngine.h"

class olc2C02
{
public:
	olc2C02();
	~olc2C02();

private:
	uint8_t tblName[2][1024];
	uint8_t tblPalette[32];
	uint8_t tblPattern[2][4096];

private:
	olc::Pixel  palScreen[0x40];
	// olc::Sprite sprScreen = olc::Sprite(256, 240);
	// olc::Sprite sprNameTable[2] = { olc::Sprite(256, 240), olc::Sprite(256, 240) };
	// olc::Sprite sprPatternTable[2] = { olc::Sprite(128, 128), olc::Sprite(128, 128) };

	// Changed To for API breaking subsequent PGE Update
	olc::Sprite* sprScreen;
	olc::Sprite* sprNameTable[2];
	olc::Sprite* sprPatternTable[2];

public:
	// Debugging Utilities
	olc::Sprite& GetScreen();
	olc::Sprite& GetNameTable(uint8_t i);
	olc::Sprite& GetPatternTable(uint8_t i, uint8_t palette);

    olc::Pixel& GetColourFromPaletteRam(uint8_t palette, uint8_t pixel);

	bool frame_complete = false;

private:
	int16_t scanline = 0;
	int16_t cycle = 0;


public:
	// comunicacao com o bus principal
	uint8_t cpuRead(uint16_t addr, bool rdonly = false);
	void    cpuWrite(uint16_t addr, uint8_t  data);

	// comunicacao com PPU do Bus
	uint8_t ppuRead(uint16_t addr, bool rdonly = false);
	void    ppuWrite(uint16_t addr, uint8_t data);

private:
	// cartucho
	std::shared_ptr<Cartridge> cart;

public: // interface
	void connectCartridge(const std::shared_ptr<Cartridge>& cart);
	void clock();
};

