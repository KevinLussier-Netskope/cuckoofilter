#include "cuckoofilter.h"

#include <assert.h>
#include <math.h>

#include <iostream>
#include <vector>
#include <chrono>

#include <sys/resource.h>

using cuckoofilter::CuckooFilter;

int main(int argc, char **argv) {
  size_t total_items = 1000000;
//  size_t total_items = 500000000;

  std::cout << "size_t: " << sizeof(size_t) << "\n";
  struct rusage startuse;
  memset(&startuse, 0, sizeof(startuse));
  if (getrusage(RUSAGE_SELF, &startuse) < 0) {
    std::cout << "failed to get usage before adds: << " << errno;
  }
  std::cout << "memory usage before adds: " << startuse.ru_maxrss << "\n";

  // Create a cuckoo filter where each item is of type size_t and
  // use 12 bits for each item:
  //    CuckooFilter<size_t, 12> filter(total_items);
  // To enable semi-sorting, define the storage of cuckoo filter to be
  // PackedTable, accepting keys of size_t type and making 13 bits
  // for each key:
  //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);
  CuckooFilter<size_t, 12> filter(total_items);

  struct rusage createuse;
  memset(&createuse, 0, sizeof(createuse));
  if (getrusage(RUSAGE_SELF, &createuse) < 0) {
    std::cout << "failed to get usage after create: << " << errno;
  }
  std::cout << "memory usage after create: " << createuse.ru_maxrss << "\n";
  std::cout << "memory used by create: " << createuse.ru_maxrss - startuse.ru_maxrss << "\n";
  std::cout << "size reported by object: " << filter.SizeInBytes() << "\n";

  auto start = std::chrono::high_resolution_clock::now();
  // Insert items to this cuckoo filter
  size_t num_inserted = 0;
  for (size_t i = 0; i < total_items; i++, num_inserted++) {
    if (filter.Add(i) != cuckoofilter::Ok) {
      std::cout << "failed to insert item " << i << "\n";
      break;
    }
  }
  auto stop = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start); 
  std::cout << num_inserted << " entries added in " << duration.count() << " microseconds " << std::endl;

  struct rusage adduse;
  memset(&adduse, 0, sizeof(adduse));
  if (getrusage(RUSAGE_SELF, &adduse) < 0) {
    std::cout << "failed to get usage after adds: << " << errno;
  }
  std::cout << "memory usage after adds: " << adduse.ru_maxrss << "\n";
  std::cout << "memory used by adds: " << adduse.ru_maxrss - createuse.ru_maxrss << "\n";

  // Check if previously inserted items are in the filter, expected
  // true for all items
  for (size_t i = 0; i < num_inserted; i++) {
    assert(filter.Contain(i) == cuckoofilter::Ok);
  }

  // Check non-existing items, a few false positives expected
  size_t total_queries = 0;
  size_t false_queries = 0;
  for (size_t i = total_items; i < 2 * total_items; i++) {
    if (filter.Contain(i) == cuckoofilter::Ok) {
      false_queries++;
    }
    total_queries++;
  }

  // Output the measured false positive rate
  std::cout << "false queries: " << false_queries << ", total_queries: " << total_queries << "\n";
  std::cout << "false positive rate is "
            << 100.0 * false_queries / total_queries << "%\n";

  struct rusage enduse;
  memset(&enduse, 0, sizeof(enduse));
  if (getrusage(RUSAGE_SELF, &enduse) < 0) {
    std::cout << "failed to get usage at end: << " << errno;
  }
  std::cout << "memory usage at end: " << enduse.ru_maxrss << "\n";

  return 0;
}
