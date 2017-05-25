//
// Copyright(c) 2015 Gabi Melman.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

#pragma once

// Helper class for file sink
// When failing to open a file, retry several times(5) with small delay between the tries(10 ms)
// Can be set to auto flush on every line
// Throw spdlog_ex exception on errors

#include <spdlog/details/os.h>
#include <spdlog/details/log_msg.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <cerrno>

#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <libgen.h>
#include <unistd.h>
#else
#include <WinSock2.h>
#include <io.h>
#endif

#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

namespace spdlog
{
namespace details
{

#ifdef _WIN32
	static bool Truncate(HANDLE fd, long long file_size)
	{
		return SetFilePointer(fd, file_size, 0, FILE_BEGIN) && SetEndOfFile(fd);
	}
	static void Truncate(filename_t filename, long long file_size)
	{
		int fd = _open(filename.c_str(), O_RDWR);
		if (fd < 0)
		{
			return ;
		}
		_chsize_s(fd, file_size);
		_close(fd);
	}
#endif

class fastfile_helper
{

public:
    const int open_tries = 5;
    const int open_interval = 10;

    explicit fastfile_helper() :
		m_offset_(0)
    {
#ifndef _WIN32
		m_fd_ = -1;
#else
		m_fd_ = INVALID_HANDLE_VALUE;
		m_map_ = INVALID_HANDLE_VALUE;
#endif
	}

    fastfile_helper(const fastfile_helper&) = delete;
    fastfile_helper& operator=(const fastfile_helper&) = delete;

    ~fastfile_helper()
    {
        close();
    }


    void open(const filename_t& fname, long long max_size, bool truncate = false)
    {
		m_maxsize_ = max_size;
		_filename = fname;
		close();
		if (truncate)
		{
			Truncate(fname, 0);
		}
		if (os::file_exists(fname))
		{
			FILE *p = fopen(fname.c_str(), "r");
			m_offset_ = os::filesize(p);
			fclose(p);
		}

#ifdef _WIN32
		m_fd_ = CreateFile(fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (INVALID_HANDLE_VALUE == m_fd_)
		{
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
		// If CreateFileMapping is called to create a file mapping object for hFile,
		// UnmapViewOfFile must be called first to unmap all views and call CloseHandle to close the file mapping object
		// before you can call SetEndOfFile.
		m_map_ = CreateFileMapping(m_fd_, NULL, PAGE_READWRITE, 0, m_maxsize_, NULL);
		if (NULL == m_map_)
		{
			CloseHandle(m_fd_);
			m_fd_ = INVALID_HANDLE_VALUE;
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
		m_buf_ = (char *)MapViewOfFile(m_map_, FILE_MAP_ALL_ACCESS, 0, m_offset_, m_maxsize_);
		if (NULL == m_buf_)
		{
			CloseHandle(m_map_); // close the file mapping object
			m_map_ = INVALID_HANDLE_VALUE;
			CloseHandle(m_fd_);
			m_fd_ = INVALID_HANDLE_VALUE;
			Truncate(fname, 0);
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
#endif // _WIN32
    }

    void reopen(bool truncate)
    {
        if (_filename.empty())
            throw spdlog_ex("Failed re opening file - was not opened before");
        open(_filename, truncate);

    }

    void flush()
    {
        // no need anymore
    }

    void close()
    {
#ifdef _WIN32
		FlushViewOfFile(m_buf_, m_offset_);
		UnmapViewOfFile(m_buf_);
		CloseHandle(m_map_); // close the file mapping object
		CloseHandle(m_fd_);
		m_fd_ = INVALID_HANDLE_VALUE;
		Truncate(_filename, m_offset_);
		int a = 1;
#else
#endif
    }

    void write(const log_msg& msg)
    {

        size_t msg_size = msg.formatted.size();
        auto data = msg.formatted.data();
		memcpy(m_buf_ + m_offset_, data, msg_size);
		m_offset_ += msg_size;
    }

    size_t size()
    {
		return m_offset_;
    }

    const filename_t& filename() const
    {
        return _filename;
    }

    static bool file_exists(const filename_t& name)
    {

        return os::file_exists(name);
    }

private:
	long long m_maxsize_;
	int m_offset_; // free offset
	char *m_buf_; // mmap dest
#ifndef _WIN32
	int m_fd_; // log file descriptor
#else
	HANDLE m_fd_;
	HANDLE m_map_;
#endif
    filename_t _filename;
};
}
}
