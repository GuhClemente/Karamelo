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

#ifndef ODIN_STATE_FILE_HPP
#define ODIN_STATE_FILE_HPP

#include <cstdint>
#include <cstdio>
#include <string>

namespace odin {

/* A save state on disk: a line that names the format, then items, each
 * its size in 32 bits followed by its bytes. An item is read back into a
 * thing of the same size only: a state made by a build that lays its data
 * out otherwise is refused instead of misread. */
class StateFile {
public:
	/* One more each time what a state keeps changes (the history of the
	 * repository says what each version changed) */
	static constexpr unsigned VERSION = 16;

	enum class Mode {
		SAVE,
		LOAD
	};

	/* Opens the file and writes its first line, or checks it: ok()
	 * tells whether that went well. */
	StateFile(const std::string &path, Mode mode);
	~StateFile();
	StateFile(const StateFile &) = delete;
	StateFile &operator=(const StateFile &) = delete;

	bool ok() const { return m_file != nullptr; }
	/* For the tests */
	bool loading() const { return m_mode == Mode::LOAD; }

	/* One item. False when it could not be written, or read into that
	 * size. */
	bool put(const void *data, uint32_t size);
	bool get(void *data, uint32_t size);
	/* The size of the next item, left to be read; 0 at the end of the
	 * state. An item of a board the state may not have is known by it. */
	uint32_t next_size();
	bool skip();

	/* A plain thing as one item */
	template <typename T>
	bool put(const T &thing)
	{
		return put(&thing, static_cast<uint32_t>(sizeof(thing)));
	}
	template <typename T>
	bool get(T &thing)
	{
		return get(&thing, static_cast<uint32_t>(sizeof(thing)));
	}

private:
	std::FILE *m_file;
	Mode m_mode;
};

} /* namespace odin */

#endif /* ODIN_STATE_FILE_HPP */
