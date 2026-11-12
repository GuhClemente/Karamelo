/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef ODIN_HIKARU_HOST_HPP
#define ODIN_HIKARU_HOST_HPP

#include <cstdint>

/* What is around the board (hikaru.hpp): its screen and the control panel
 * of the cabinet, in a header of their own so that the front end does not
 * take the whole board in. */

namespace odin {

class StateFile;

/* What the cabinet feeds the board. JVS bit order: system byte bit 7
 * TEST; player word high byte bit 7 START, 6 SERVICE, 5 UP, 4 DOWN, 3 LEFT,
 * 2 RIGHT, 1 PUSH1, 0 PUSH2; analog channels 0x8000 centre. */
struct CabinetInputs {
	int mainboard_test;	/* SW7 on the main board */
	int mainboard_service;	/* SW8 */
	uint8_t jvs_system;
	uint16_t jvs_player[2];
	uint16_t jvs_analog[8];
	int jvs_coin[2];	/* 1 = a coin went through since the last call */
};

/* The screen draws what the graphics chips leave (Hikaru::Screen). */
class HikaruScreen {
public:
	virtual ~HikaruScreen() = default;
	/* A display list was launched. */
	virtual void list_launched() = 0;
	/* A rectangle of texels of a bank of the texture RAM, 2048 x 1024
	 * texels, is about to be written. */
	virtual void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
	/* The memories of the board are about to change as a whole, on a
	 * reset or for a state: what is being drawn from them must be over. */
	virtual void before_memory_changes() = 0;
	/* They have: what was decoded from them is stale. */
	virtual void memory_changed() = 0;
	/* The pictures the screen keeps from a frame to the next, as items of
	 * a state. */
	virtual bool save_pictures(StateFile &state) = 0;
	virtual bool load_pictures(StateFile &state) = 0;
};

/* And the control panel: the switches of the board and the controls of
 * the cabinet as they are now (Hikaru::Panel). */
class HikaruPanel {
public:
	virtual ~HikaruPanel() = default;
	virtual void read_inputs(CabinetInputs &inputs) = 0;
};

} /* namespace odin */

#endif /* ODIN_HIKARU_HOST_HPP */
