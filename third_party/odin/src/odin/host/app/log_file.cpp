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

#include "odin/host/app/log_file.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ctime>
#include <unistd.h>
#endif

#include "odin/host/app/user_files.hpp"

namespace odin {

namespace {

/* The files of the host, the same way on each */
#ifdef _WIN32
int dup_fd(int fd) { return _dup(fd); }
int dup2_fd(int from, int to) { return _dup2(from, to); }
int make_pipe(int fds[2]) { return _pipe(fds, 1 << 16, _O_BINARY); }
long read_fd(int fd, char *data, size_t size) { return _read(fd, data, static_cast<unsigned>(size)); }
long write_fd(int fd, const char *data, size_t size) { return _write(fd, data, static_cast<unsigned>(size)); }
void close_fd(int fd) { _close(fd); }
void not_inherited(int) {}

int
open_file(const std::string &path)
{
	return _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
}

void pause_ms(unsigned ms) { Sleep(ms); }
#else
int dup_fd(int fd) { return dup(fd); }
int dup2_fd(int from, int to) { return dup2(from, to); }
int make_pipe(int fds[2]) { return pipe(fds); }
long read_fd(int fd, char *data, size_t size) { return read(fd, data, size); }
long write_fd(int fd, const char *data, size_t size) { return write(fd, data, size); }
void close_fd(int fd) { close(fd); }
void not_inherited(int fd) { fcntl(fd, F_SETFD, FD_CLOEXEC); }

int
open_file(const std::string &path)
{
	return open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

void
pause_ms(unsigned ms)
{
	const timespec wait = { static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L };

	nanosleep(&wait, nullptr);
}
#endif

/* stdout or stderr: the terminal it went to, the end of its pipe its
 * thread reads, whether that thread is done */
struct Stream {
	Stream(int number, std::FILE *stream) : fd(number), file(stream) {}

	int fd;
	std::FILE *file;
	int terminal = -1;
	int pipe_read = -1;
	std::thread thread;
	bool done = false;
};

struct Log {
	std::mutex mutex;
	std::condition_variable ended;
	int file = -1;
	uint64_t most = 0, written = 0;
	bool cut = false, on = false;
	std::array<Stream, 2> streams = { Stream(1, stdout), Stream(2, stderr) };
};

/* Never destroyed: its threads may outlive the end of main */
Log &
the_log()
{
	static Log *const log = new Log;

	return *log;
}

void
write_all(int fd, const char *data, size_t size)
{
	while (size) {
		const long done = write_fd(fd, data, size);

		if (done <= 0)
			return;
		data += done;
		size -= static_cast<size_t>(done);
	}
}

/* Into the file, as far as it may grow */
void
keep(Log &log, const char *data, size_t size)
{
	static const char END[] = "\n[odin.log stops here, at its largest; the terminal has all]\n";
	const std::lock_guard<std::mutex> lock(log.mutex);

	if (log.file < 0 || log.cut)
		return;

	const uint64_t room = log.most - log.written;

	if (size > room) {
		write_all(log.file, data, static_cast<size_t>(room));
		write_all(log.file, END, sizeof(END) - 1);
		log.cut = true;
		return;
	}
	write_all(log.file, data, size);
	log.written += size;
}

void
copy(Log &log, Stream &stream)
{
	char data[4096];

	for (;;) {
		const long got = read_fd(stream.pipe_read, data, sizeof(data));

		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			break;
		if (stream.terminal >= 0)
			write_all(stream.terminal, data, static_cast<size_t>(got));
		keep(log, data, static_cast<size_t>(got));
	}
	{
		const std::lock_guard<std::mutex> lock(log.mutex);

		stream.done = true;
	}
	log.ended.notify_all();
}

/* A crash: the threads have a moment to write its last lines, then it goes
 * as it would have gone */
constexpr int CRASHES[] = {
	SIGABRT, SIGSEGV, SIGFPE, SIGILL,
#ifndef _WIN32
	SIGBUS,
#endif
};

extern "C" void
on_crash(int signal)
{
	pause_ms(300);
	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

} /* namespace */

std::string
LogFile::path(const std::string &folder)
{
	return user_path("ODIN_LOG", folder.empty() ? folder : folder + "/odin.log", "odin.log");
}

std::string
LogFile::previous(const std::string &path)
{
	const size_t dot = path.rfind(".log");

	if (dot != std::string::npos && dot + 4 == path.size())
		return path.substr(0, dot) + "-previous.log";
	return path + ".previous";
}

void
LogFile::leave_own_console()
{
#ifdef _WIN32
	DWORD processes[2];

	if (GetConsoleProcessList(processes, 2) != 1)
		return;
	FreeConsole();
	/* The C library would still use the console gone, and crash in
	 * start(): NUL instead */
	std::freopen("NUL", "r", stdin);
	std::freopen("NUL", "w", stdout);
	std::freopen("NUL", "w", stderr);
#endif
}

bool
LogFile::start(const std::string &path, uint64_t most)
{
	Log &log = the_log();
	const std::filesystem::path file(path);
	std::error_code error;

	if (path.empty() || log.on)
		return false;
	if (file.has_parent_path())
		std::filesystem::create_directories(file.parent_path(), error);
	if (std::filesystem::exists(file, error))
		std::filesystem::rename(file, previous(path), error);
	log.file = open_file(path);
	if (log.file < 0) {
		std::fprintf(stderr, "ERROR: log: '%s' cannot be written\n", path.c_str());
		return false;
	}
	log.most = most;
	log.written = 0;
	log.cut = false;

	for (Stream &stream : log.streams) {
		int fds[2];

		std::fflush(stream.file);
		if (make_pipe(fds) != 0)
			continue;
		stream.terminal = dup_fd(stream.fd);
#ifdef _WIN32
		if (stream.terminal >= 0)
			_setmode(stream.terminal, _O_BINARY);
#endif
		if (dup2_fd(fds[1], stream.fd) < 0) {
			close_fd(fds[0]);
			close_fd(fds[1]);
			if (stream.terminal >= 0)
				close_fd(stream.terminal);
			stream.terminal = -1;
			continue;
		}
		close_fd(fds[1]);
		not_inherited(fds[0]);
		if (stream.terminal >= 0)
			not_inherited(stream.terminal);
		stream.pipe_read = fds[0];
		stream.done = false;
		stream.thread = std::thread(copy, std::ref(log), std::ref(stream));
	}
	/* stdout a line at a time, as on a terminal (Windows has no lines:
	 * each write at once) */
#ifdef _WIN32
	std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
	std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#endif
	log.on = true;
	std::atexit(stop);
	for (const int crash : CRASHES)
		std::signal(crash, on_crash);
	return true;
}

void
LogFile::stop()
{
	Log &log = the_log();

	if (!log.on)
		return;
	log.on = false;
	std::fflush(stdout);
	std::fflush(stderr);

	/* The terminal back: the last end of each pipe that writes goes, and
	 * its thread, once it has read all, ends; one a child of Odin still
	 * holds is left */
	for (Stream &stream : log.streams) {
		if (stream.pipe_read < 0)
			continue;
		if (stream.terminal >= 0)
			dup2_fd(stream.terminal, stream.fd);
		else
			close_fd(stream.fd);
	}
	{
		std::unique_lock<std::mutex> lock(log.mutex);

		log.ended.wait_for(lock, std::chrono::milliseconds(500), [&log] {
			for (const Stream &stream : log.streams)
				if (stream.pipe_read >= 0 && !stream.done)
					return false;
			return true;
		});
	}
	for (Stream &stream : log.streams) {
		if (!stream.thread.joinable())
			continue;
		if (!stream.done) {
			stream.thread.detach();
			continue;
		}
		stream.thread.join();
		close_fd(stream.pipe_read);
		if (stream.terminal >= 0)
			close_fd(stream.terminal);
		stream.pipe_read = stream.terminal = -1;
	}

	const std::lock_guard<std::mutex> lock(log.mutex);

	close_fd(log.file);
	log.file = -1;
}

} /* namespace odin */
