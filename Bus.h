#pragma once
#include <cstdint>
#include <array>
#include <memory>

#include "olc6502.h"
#include "olc2C02.h"
#include "olc2A03.h"
#include "Cartridge.h"

class Bus
{
public:
	Bus();
	~Bus();

public: // Devices on bus
	olc6502 cpu;

	olc2C02 ppu;

    olc2A03 apu;

	// falsa ram temporaria
	std::array<uint8_t, 2048> cpuRam;

	std::shared_ptr<Cartridge> cart;

    uint8_t controller[2];


public: // Bus ler e escrever
	void cpuWrite(uint16_t addr, uint8_t data);
	uint8_t cpuRead(uint16_t addr, bool bReadOnly = false);

public:
    void SetSampleFrequency(uint32_t sample_rate);
    double dAudioSample = 0.0;

private:
    double dAudioTime = 0.0;
    double dAudioTimePerNESClock = 0.0;
    double dAudioTimePerSystemSample = 0.0f;

public: // interface do sistema
	void insertCartridge(const std::shared_ptr<Cartridge>& cartridge);
	void reset();
	bool clock();

private:
	// conta quantos clocks ja passaram
	uint32_t nSystemClockCounter = 0;
    uint8_t controller_state[2];

    uint8_t dma_page = 0x00;
    uint8_t dma_addr = 0x00;
    uint8_t dma_data = 0x00;

    bool dma_tansfer = false;
    bool dma_dummy = false;
};
