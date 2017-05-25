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
		offset_(0)
    {
#ifndef _WIN32
		fd_ = -1;
#else
		fd_ = INVALID_HANDLE_VALUE;
		mmap_ = INVALID_HANDLE_VALUE;
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
		max_size_ = max_size;
		_filename = fname;
		close();
		if (truncate)
		{
			Truncate(fname, 0);
		}
		if (os::file_exists(fname))
		{
			FILE *p = fopen(fname.c_str(), "r");
			offset_ = os::filesize(p);
			fclose(p);
		}

#ifdef _WIN32
		fd_ = CreateFile(fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (INVALID_HANDLE_VALUE == fd_)
		{
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
		// If CreateFileMapping is called to create a file mapping object for hFile,
		// UnmapViewOfFile must be called first to unmap all views and call CloseHandle to close the file mapping object
		// before you can call SetEndOfFile.
		mmap_ = CreateFileMapping(fd_, NULL, PAGE_READWRITE, 0, max_size_, NULL);
		if (NULL == mmap_)
		{
			CloseHandle(fd_);
			fd_ = INVALID_HANDLE_VALUE;
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
		buf_ = (char *)MapViewOfFile(mmap_, FILE_MAP_ALL_ACCESS, 0, offset_, max_size_);
		if (NULL == buf_)
		{
			CloseHandle(mmap_); // close the file mapping object
			mmap_ = INVALID_HANDLE_VALUE;
			CloseHandle(fd_);
			fd_ = INVALID_HANDLE_VALUE;
			Truncate(fname, 0);
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
#else
		fd_ = ::open(m_file_
			, O_CREAT | O_APPEND | O_RDWR
			, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd_ < 0)
		{
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
		if (::ftruncate(fd_, max_size_) != 0)
		{
			::close(fd_);
			fd_ = -1;
			throw spdlog_ex("Failed opening file " + os::filename_to_str(_filename) + " for writing", errno);
		}
		buf_ = (char *)::mmap(0, max_size_, PROT_WRITE, MAP_SHARED, fd_, offset_);
		if (buf_ == MAP_FAILED)
		{
			::ftruncate(fd_, 0);
			::close(fd_);
			fd_ = -1;
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
		FlushViewOfFile(buf_, offset_);
		UnmapViewOfFile(buf_);
		CloseHandle(mmap_); // close the file mapping object
		CloseHandle(fd_);
		fd_ = INVALID_HANDLE_VALUE;
		Truncate(_filename, offset_);
		int a = 1;
#else
		::ftruncate(fd_, offset_);
		::close(fd_);
#endif
    }

    void write(const log_msg& msg)
    {

        size_t msg_size = msg.formatted.size();
        auto data = msg.formatted.data();
		memcpy(buf_ + offset_, data, msg_size);
		offset_ += msg_size;
    }

    size_t size()
    {
		return offset_;
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
	size_t max_size_;
	int offset_; // free offset
	char *buf_; // mmap dest
#ifndef _WIN32
	int fd_; // log file descriptor
#else
	HANDLE fd_;
	HANDLE mmap_;
#endif
    filename_t _filename;
};
}
}
