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

#ifndef ODIN_FOLDER_DIALOG_HPP
#define ODIN_FOLDER_DIALOG_HPP

#include <memory>
#include <optional>
#include <string>

#include <SDL3/SDL.h>

namespace odin {

/* The folder picker of the system (SDL_ShowOpenFolderDialog), or its file
 * picker for an archive of a game. Its answer may come on another thread,
 * before the call returns or long after: it is kept here, and an event of
 * its own wakes the thread that asked. */
class FolderDialog {
public:
	/* What was asked, the folder (or the archive) chosen (empty: none), and
	 * why there is no picker when the system has none */
	struct Answer {
		int asked = 0;
		std::string folder, error;
	};

	FolderDialog();

	/* A folder for what `asked` says, the picker opened at `from` when it
	 * is there; nothing while another picker is open */
	void ask(SDL_Window *window, const std::string &from, int asked);
	/* A zip or a 7z, as MAME keeps a game, the same way */
	void ask_archive(SDL_Window *window, const std::string &from, int asked);
	bool open() const;
	/* The event that says the answer has come, and the answer, once */
	Uint32 event() const;
	std::optional<Answer> answer();

	struct Slot;

private:
	/* The picker may be opened: no other one is */
	bool start();

	std::shared_ptr<Slot> m_slot;
};

} /* namespace odin */

#endif /* ODIN_FOLDER_DIALOG_HPP */
