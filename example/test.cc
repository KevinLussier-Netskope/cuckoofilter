#include "cuckoofilter.h"

#include <assert.h>
#include <math.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

#include <sys/resource.h>

using cuckoofilter::CuckooFilter;
using cuckoofilter::BaseCuckooFilter;

void usage()
{
  printf("Usage: test [item_count] [bits_per_item] [fp_mult]\n");
  printf("  where item_count is the number of items to add to the filter\n");
  printf("        bits_per_item is the number of bits to allocate per item (2, 4, 8, 12, 16, or 32)\n");
  printf("        fp_mult is the number of false positives to test for as a multiple of item_count (e.g. 3)\n");
  exit(-1);
}

int main(int argc, const char **argv)
{
  size_t total_items = 1000000;
  size_t bits_per_item = 12;
  size_t fp_mult = 2;
  if (argc > 4) {
    usage();
  }
  if (argc > 1) {
    total_items = std::atoi(argv[1]);
    if (argc > 2) {
      bits_per_item = std::atoi(argv[2]);
      if (argc > 3) {
        fp_mult = std::atoi(argv[3]);
      }
    }
  }

  if (bits_per_item != 2 && bits_per_item != 4 && bits_per_item != 8 &&
      bits_per_item != 12 && bits_per_item != 16 && bits_per_item != 32) {
    usage();
  }

  std::cout << "running test with " << total_items << " items with " <<
    bits_per_item << " bits per item and a false positive multiple of " <<
    fp_mult << "\n";

  struct rusage startuse;
  memset(&startuse, 0, sizeof(startuse));
  if (getrusage(RUSAGE_SELF, &startuse) < 0) {
    std::cout << "failed to get usage before adds: << " << errno;
  }

  // Create a cuckoo filter where each item is of type size_t and
  // use 12 bits for each item:
  //    CuckooFilter<size_t, 12> filter(total_items);
  // To enable semi-sorting, define the storage of cuckoo filter to be
  // PackedTable, accepting keys of size_t type and making 13 bits
  // for each key:
  //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);
  BaseCuckooFilter<size_t> *filter;
  switch(bits_per_item) {
    case 2: filter = new CuckooFilter<size_t, 2>(total_items); break;
    case 4: filter = new CuckooFilter<size_t, 4>(total_items); break;
    case 8: filter = new CuckooFilter<size_t, 8>(total_items); break;
    case 12: filter = new CuckooFilter<size_t, 12>(total_items); break;
    case 16: filter = new CuckooFilter<size_t, 16>(total_items); break;
    case 32: filter = new CuckooFilter<size_t, 32>(total_items); break;
  }

  struct rusage createuse;
  memset(&createuse, 0, sizeof(createuse));
  if (getrusage(RUSAGE_SELF, &createuse) < 0) {
    std::cout << "failed to get usage after create: << " << errno;
  }
  std::cout << "memory usage after create: " << createuse.ru_maxrss << "\n";
  std::cout << "memory used by create: " << createuse.ru_maxrss - startuse.ru_maxrss << "\n";
  std::cout << "size reported by object: " << filter->SizeInBytes() << "\n";

  auto start = std::chrono::high_resolution_clock::now();
  // Insert items to this cuckoo filter
  size_t num_inserted = 0;
  for (size_t i = 0; i < total_items; i++, num_inserted++) {
    if (filter->Add(i) != cuckoofilter::Ok) {
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
    assert(filter->Contain(i) == cuckoofilter::Ok);
  }

  // Check non-existing items, a few false positives expected
  size_t total_queries = 0;
  size_t false_queries = 0;
  for (size_t i = total_items; i < (fp_mult + 1) * total_items; i++) {
    if (filter->Contain(i) == cuckoofilter::Ok) {
      false_queries++;
    }
    total_queries++;
  }

  // Output the measured false positive rate
  std::cout.precision(15);
  std::cout << "false queries: " << false_queries << ", total_queries: " << total_queries << "\n";
  std::cout << std::fixed << "false positive rate is "
            << 100.0 * false_queries / total_queries << "%\n";

  struct rusage enduse;
  memset(&enduse, 0, sizeof(enduse));
  if (getrusage(RUSAGE_SELF, &enduse) < 0) {
    std::cout << "failed to get usage at end: << " << errno;
  }
  std::cout << "memory usage at end: " << enduse.ru_maxrss << "\n";

  return 0;
}
