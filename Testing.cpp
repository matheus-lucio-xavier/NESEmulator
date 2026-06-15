#include <iostream>
#include <sstream>

#include "Bus.h"
#include "olc6502.h"

#define OLC_PGE_APPLICATION
#include "Cartridge.h"



class Demo_olc2C02 : public olc::PixelGameEngine
{
public:
	Demo_olc2C02() { sAppName = "olc2C02 Demonstration"; }

private:
	// The NES
	Bus nes;
	std::shared_ptr<Cartridge> cart;
	bool bEmulationRun = true;
	float fResidualTime = 0.0f;

	bool OnUserCreate()
	{
		// Load the cartridge
        cart = std::make_shared<Cartridge>("C:/Users/matheus/Downloads/DonkeyKong.nes"); // Use qualquer rom que queira testar
		if (!cart->ImageValid())
			return false;

		// Insert into NES
		nes.insertCartridge(cart);

		// Reset NES
		nes.reset();
		return true;
	}

	bool OnUserUpdate(float fElapsedTime)
	{
		Clear(olc::DARK_BLUE);

        // Handle input for controller in port #1
        nes.controller[0] =
            (GetKey(olc::Key::K).bHeld ? 0x80 : 0x00) |     // A
            (GetKey(olc::Key::L).bHeld ? 0x40 : 0x00) |     // B
            (GetKey(olc::Key::BACK).bHeld ? 0x20 : 0x00) | // Select
            (GetKey(olc::Key::ENTER).bHeld ? 0x10 : 0x00) |  // Start
            (GetKey(olc::Key::W).bHeld ? 0x08 : 0x00) |     // Up
            (GetKey(olc::Key::S).bHeld ? 0x04 : 0x00) |     // Down
            (GetKey(olc::Key::A).bHeld ? 0x02 : 0x00) |     // Left
            (GetKey(olc::Key::D).bHeld ? 0x01 : 0x00);      //Rigth

        // Other keys input
        if (GetKey(olc::Key::SPACE).bPressed) bEmulationRun = !bEmulationRun;
        if (GetKey(olc::Key::R).bPressed) nes.reset();

		if (bEmulationRun)
		{
            sAppName = "olc2C02 Demonstration";

			if (fResidualTime > 0.0f)
				fResidualTime -= fElapsedTime;
			else
			{
				fResidualTime += (1.0f / 60.0f) - fElapsedTime;
				do { nes.clock(); } while (!nes.ppu.frame_complete);
				nes.ppu.frame_complete = false;
			}
        }
        else {
            sAppName = "olc2C02 Demonstration paused";
        }

		return true;
	}
};





int teste()
{
	Demo_olc2C02 demo;
	demo.Construct(480, 480, 2, 2);
	demo.Start();
	return 0;
}
