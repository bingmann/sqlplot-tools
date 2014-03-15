/******************************************************************************
 * examples/sorting-speed/sorting-speed.cpp
 *
 * Very simple experiment to measure the speed of std::sort, std::stable_sort,
 * and STL's heapsort implementations on a random integer permutation.
 *
 ******************************************************************************
 * Copyright (C) 2014 Timo Bingmann <tb@panthema.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <algorithm>
#include <iostream>
#include <cmath>
#include <sys/time.h>

//! minimum total item count sorted per experiment -> increase for faster
//! machines.
const size_t test_volume = 32*1024*1024;

//! smallest item count to test
const size_t size_min = 1024;

//! largest item count to test
const size_t size_max = 1024*1024*1024;

//! number of iterations of each test size
const size_t iterations = 15;

//! item type
typedef unsigned int item_type;

////////////////////////////////////////////////////////////////////////////////

void test_std_sort(item_type* array, size_t n)
{
    std::sort(array, array + n);
}

void test_std_stable_sort(item_type* array, size_t n)
{
    std::stable_sort(array, array + n);
}

void test_std_heap_sort(item_type* array, size_t n)
{
    std::make_heap(array, array + n);
    std::sort_heap(array, array + n);
}

////////////////////////////////////////////////////////////////////////////////

//! timestamp
double timestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

//! the test framework routine
template <void (*test)(item_type* array, size_t n)>
void run_test(const std::string& algoname)
{
    for (size_t size = size_min; size <= size_max; size *= 2)
    {
        size_t repeats = test_volume / size;
        if (repeats == 0) repeats = 1;

        std::cout << "Running algorithm " << algoname
                  << " with size=" << size << " repeats=" << repeats << "\n";

        for (size_t iter = 0; iter < iterations; ++iter)
        {
            std::cout << "iteration=" << iter << "\n";
                
            item_type* array = new item_type[size];

            for (size_t i = 0; i < size; ++i)
                array[i] = i;

            std::random_shuffle(array, array + size);

            item_type* arraycopy = new item_type[size];

            double ts1 = timestamp();

            for (size_t r = 0; r < repeats; ++r)
            {
                // copy in new version of random permutation
                std::copy(array, array + size, arraycopy);

                test(array, size);
            }

            double ts2 = timestamp();

            std::cout << "time = " << ts2-ts1 << std::endl;

            delete [] arraycopy;
            delete [] array;

            std::cout << "RESULT"
                      << " algo=" << algoname
                      << " size=" << size
                      << " size_log2=" << log(size) / log(2)
                      << " time=" << ts2-ts1
                      << " repeats=" << repeats
                      << " iteration=" << iter
                      << " typesize=" << sizeof(item_type)
                      << " datasize=" << size * sizeof(item_type)
                      << std::endl;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

int main()
{
    run_test<test_std_sort>("std::sort");
    run_test<test_std_stable_sort>("std::stable_sort");
    run_test<test_std_heap_sort>("std::heap_sort");

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
