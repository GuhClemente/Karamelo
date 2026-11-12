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

#include "odin/host/ui/folder_dialog.hpp"

#include <filesystem>
#include <mutex>
#include <system_error>
#include <utility>

namespace odin {

/* Shared with each picker open, which may answer after Odin is done with
 * it */
struct FolderDialog::Slot {
	std::mutex mutex;
	std::optional<Answer> answer;
	bool open = false;
	Uint32 event = SDL_EVENT_USER;
};

namespace {

struct Asked {
	std::shared_ptr<FolderDialog::Slot> slot;
	int asked;
};

/* Read by the picker until it answers */
constexpr SDL_DialogFileFilter ARCHIVES[] = { { "MAME game archives (zip, 7z)", "zip;7z" } };

/* Where the picker opens: the folder when it is there */
const char *
start_at(const std::string &from)
{
	std::error_code error;

	return !from.empty() && std::filesystem::is_directory(from, error) ? from.c_str() : nullptr;
}

void SDLCALL
answered(void *userdata, const char *const *files, int)
{
	const std::unique_ptr<Asked> asked(static_cast<Asked *>(userdata));
	FolderDialog::Answer answer;
	SDL_Event event{};

	answer.asked = asked->asked;
	if (!files)
		answer.error = SDL_GetError();
	else if (files[0])
		answer.folder = files[0];
	{
		const std::lock_guard<std::mutex> lock(asked->slot->mutex);

		asked->slot->answer = answer;
		asked->slot->open = false;
		event.type = asked->slot->event;
	}
	SDL_PushEvent(&event);
}

} /* namespace */

FolderDialog::FolderDialog() : m_slot(std::make_shared<Slot>())
{
	if (const Uint32 event = SDL_RegisterEvents(1))
		m_slot->event = event;
}

bool
FolderDialog::start()
{
	const std::lock_guard<std::mutex> lock(m_slot->mutex);

	if (m_slot->open)
		return false;
	m_slot->open = true;
	return true;
}

void
FolderDialog::ask(SDL_Window *window, const std::string &from, int asked)
{
	if (start())
		SDL_ShowOpenFolderDialog(answered, new Asked{ m_slot, asked }, window, start_at(from), false);
}

void
FolderDialog::ask_archive(SDL_Window *window, const std::string &from, int asked)
{
	if (start())
		SDL_ShowOpenFileDialog(answered, new Asked{ m_slot, asked }, window, ARCHIVES, 1, start_at(from), false);
}

bool
FolderDialog::open() const
{
	const std::lock_guard<std::mutex> lock(m_slot->mutex);

	return m_slot->open;
}

Uint32
FolderDialog::event() const
{
	return m_slot->event;
}

std::optional<FolderDialog::Answer>
FolderDialog::answer()
{
	const std::lock_guard<std::mutex> lock(m_slot->mutex);

	return std::exchange(m_slot->answer, std::nullopt);
}

} /* namespace odin */
