/***************************************************************************
 *  utils/createdisks.cpp
 *
 *  Part of the STXXL. See http://stxxl.sourceforge.net
 *
 *  Copyright (C) 2003 Roman Dementiev <dementiev@mpi-sb.mpg.de>
 *  Copyright (C) 2007 Andreas Beckmann <beckmann@mpi-inf.mpg.de>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#include <cstdio>
#include <iomanip>
#include <vector>

#include <stxxl/io>
#include <stxxl/aligned_alloc>

#ifndef BOOST_MSVC
 #include <unistd.h>
#endif


using stxxl::request_ptr;
using stxxl::file;
using stxxl::timestamp;


#ifdef BLOCK_ALIGN
 #undef BLOCK_ALIGN
#endif

#define BLOCK_ALIGN  4096

#define NOREAD

//#define DO_ONLY_READ

#define POLL_DELAY 1000

#define RAW_ACCESS

//#define WATCH_TIMES

#define CHECK_AFTER_READ 0


#ifdef WATCH_TIMES
void watch_times(request_ptr reqs[], unsigned n, double * out)
{
    bool * finished = new bool[n];
    unsigned count = 0;
    unsigned i = 0;
    for (i = 0; i < n; i++)
        finished[i] = false;


    while (count != n)
    {
        usleep(POLL_DELAY);
        i = 0;
        for (i = 0; i < n; i++)
        {
            if (!finished[i])
                if (reqs[i]->poll())
                {
                    finished[i] = true;
                    out[i] = timestamp();
                    count++;
                }
        }
    }
    delete[] finished;
}


void out_stat(double start, double end, double * times, unsigned n, const std::vector<std::string> & names)
{
    for (unsigned i = 0; i < n; i++)
    {
        std::cout << i << " " << names[i] << " took " <<
        100. * (times[i] - start) / (end - start) << " %" << std::endl;
    }
}
#endif

#define MB (1024 * 1024)

int main(int argc, char * argv[])
{
    if (argc < 3)
    {
        STXXL_MSG("Usage: " << argv[0] << " filesize_in_MiB filename1 [filename2 [filename3 ...]]");
        return -1;
    }

    stxxl::int64 offset = 0;
    stxxl::int64 length = stxxl::int64(atoi(argv[1])) * MB;
    stxxl::int64 endpos = offset + length;

    std::vector<std::string> disks_arr;

    for (int ii = 2; ii < argc; ++ii)
    {
        unlink(argv[ii]);
        std::cout << "# Add disk: " << argv[ii] << std::endl;
        disks_arr.push_back(argv[ii]);
    }

    const unsigned ndisks = disks_arr.size();


#ifdef BOOST_MSVC
    unsigned buffer_size = 64 * MB;
#else
    unsigned buffer_size = 256 * MB;
#endif
    const unsigned buffer_size_int = buffer_size / sizeof(int);

    unsigned chunks = 2;
    const unsigned chunk_size = buffer_size / chunks;
    const unsigned chunk_size_int = chunk_size / sizeof(int);

    unsigned i = 0, j = 0;

    int * buffer = (int *)stxxl::aligned_alloc<BLOCK_ALIGN>(buffer_size * ndisks);
    file ** disks = new file *[ndisks];
    request_ptr * reqs = new request_ptr[ndisks * chunks];
#ifdef WATCH_TIMES
    double * r_finish_times = new double[ndisks];
    double * w_finish_times = new double[ndisks];
#endif

    for (i = 0; i < ndisks * buffer_size_int; i++)
        buffer[i] = i;

    for (i = 0; i < ndisks; i++)
    {
#ifdef BOOST_MSVC
 #ifdef RAW_ACCESS
        disks[i] = new stxxl::wincall_file(disks_arr[i],
                                           file::CREAT | file::RDWR | file::DIRECT, i);
 #else
        disks[i] = new stxxl::wincall_file(disks_arr[i],
                                           file::CREAT | file::RDWR, i);
 #endif
#else
 #ifdef RAW_ACCESS
        disks[i] = new stxxl::syscall_file(disks_arr[i],
                                           file::CREAT | file::RDWR | file::DIRECT, i);
 #else
        disks[i] = new stxxl::syscall_file(disks_arr[i],
                                           file::CREAT | file::RDWR, i);
 #endif
#endif
    }

    while (offset < endpos)
    {
        const unsigned current_block_size = length ? std::min<stxxl::int64>(buffer_size, endpos - offset) : buffer_size;
        const unsigned current_chunk_size = current_block_size / chunks;

        std::cout << "Disk offset " << std::setw(7) << offset / MB << " MiB: " << std::fixed;

        double begin = timestamp(), end;

#ifndef DO_ONLY_READ
        for (i = 0; i < ndisks; i++)
        {
            for (j = 0; j < chunks; j++)
                reqs[i * chunks + j] =
                    disks[i]->awrite(buffer + buffer_size_int * i + j * chunk_size_int,
                                     offset + j * current_chunk_size,
                                     current_chunk_size,
                                     stxxl::default_completion_handler());
        }

 #ifdef WATCH_TIMES
        watch_times(reqs, ndisks, w_finish_times);
 #else
        wait_all(reqs, ndisks * chunks);
 #endif

        end = timestamp();

#if 0
        std::cout << "WRITE\nDisks: " << ndisks
                  << " \nElapsed time: " << end - begin
                  << " \nThroughput: " << int(double(buffer_size * ndisks) / MB / (end - begin))
                  << " MiB/s \nPer one disk:"
                  << int((buffer_size) / MB / (end - begin)) << " MiB/s"
                  << std::endl;
#endif

 #ifdef WATCH_TIMES
        out_stat(begin, end, w_finish_times, ndisks, disks_arr);
 #endif
        std::cout << std::setw(7) << int(double(current_block_size) / MB / (end - begin)) << " MiB/s,";
#endif


#ifndef NOREAD
        begin = timestamp();

        for (i = 0; i < ndisks; i++)
        {
            for (j = 0; j < chunks; j++)
                reqs[i * chunks + j] = disks[i]->aread(buffer + buffer_size_int * i + j * chunk_size_int,
                                                       offset + j * current_chunk_size,
                                                       current_chunk_size,
                                                       stxxl::default_completion_handler());
        }

 #ifdef WATCH_TIMES
        watch_times(reqs, ndisks, r_finish_times);
 #else
        wait_all(reqs, ndisks * chunks);
 #endif

        end = timestamp();

#if 0
        std::cout << "READ\nDisks: " << ndisks
                  << " \nElapsed time: " << end - begin
                  << " \nThroughput: " << int(double(buffer_size * ndisks) / MB / (end - begin))
                  << " MiB/s \nPer one disk:"
                  << int(double(buffer_size) / MB / (end - begin)) << " MiB/s"
                  << std::endl;
#endif

        std::cout << int(double(current_block_size) / MB / (end - begin)) << " MiB/s" << std::endl;

#ifdef WATCH_TIMES
        out_stat(begin, end, r_finish_times, ndisks, disks_arr);
#endif

        if (CHECK_AFTER_READ) {
            for (int i = 0; unsigned(i) < ndisks * buffer_size_int; i++)
            {
                if (buffer[i] != i)
                {
                    int ibuf = i / buffer_size_int;
                    int pos = i % buffer_size_int;

                    std::cout << "Error on disk " << ibuf << " position " << std::hex << std::setw(8) << offset + pos * sizeof(int)
                              << "  got: " << std::hex << std::setw(8) << buffer[i] << " wanted: " << std::hex << std::setw(8) << i
                              << std::dec << std::endl;

                    i = (ibuf + 1) * buffer_size_int; // jump to next
                }
            }
        }
#else
        std::cout << std::endl;
#endif

        offset += current_block_size;
    }

#ifdef WATCH_TIMES
    delete[] r_finish_times;
    delete[] w_finish_times;
#endif
    delete[] reqs;
    for (i = 0; i < ndisks; i++)
        delete disks[i];
    delete[] disks;
    stxxl::aligned_dealloc<BLOCK_ALIGN>(buffer);

    return 0;
}
