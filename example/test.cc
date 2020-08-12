#include "cuckoofilter.h"

#include <assert.h>
#include <math.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <fstream>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

bool run_adds(BaseCuckooFilter<size_t> *filter, size_t total_items)
{
  auto start = std::chrono::high_resolution_clock::now();

  // Insert items to this filter
  size_t num_inserted = 0;
  for (size_t i = 0; i < total_items; i++, num_inserted++) {
    if (filter->Add(i) != cuckoofilter::Ok) {
      std::cout << "failed to insert item " << i << "\n";
      return false;
    }
  }
  auto stop = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start); 
  std::cout << num_inserted << " entries added in " << duration.count() << " microseconds " << std::endl;
  return true;
}

bool run_contains(BaseCuckooFilter<size_t> *filter, size_t total_items, size_t fp_mult)
{
  // Check if previously inserted items are in the filter, expected
  // true for all items
  for (size_t i = 0; i < total_items; i++) {
    if (filter->Contain(i) != cuckoofilter::Ok) {
      std::cout << "False negative seen at index " << i << std::endl;
      return false;
    }
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
  return true;
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

  // Create a cuckoo filter where each item is of type size_t and
  // use 12 bits for each item:
  //    CuckooFilter<size_t, 12> filter(total_items);
  // To enable semi-sorting, define the storage of cuckoo filter to be
  // PackedTable, accepting keys of size_t type and making 13 bits
  // for each key:
  //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);

  /*
   * Run the normal test.
   */
  BaseCuckooFilter<size_t> *filter;
  switch(bits_per_item) {
    case 2: filter = new CuckooFilter<size_t, 2>(total_items); break;
    case 4: filter = new CuckooFilter<size_t, 4>(total_items); break;
    case 8: filter = new CuckooFilter<size_t, 8>(total_items); break;
    case 12: filter = new CuckooFilter<size_t, 12>(total_items); break;
    case 16: filter = new CuckooFilter<size_t, 16>(total_items); break;
    case 32: filter = new CuckooFilter<size_t, 32>(total_items); break;
    default: filter = new CuckooFilter<size_t, 12>(total_items); break; // Eliminates compiler warning on Linux
  }
  if (!filter->Valid()) {
    std::cout << "Failed to create cuckoo filter with <size_t, " << bits_per_item << "> and " << total_items << " items\n";
    return 1;
  }

  // Run the add test
  if (!run_adds(filter, total_items)) {
    std::cout << "Add test failed\n";
    return 1;
  }

  // Dump the filter info (after the add test so the added items are included)
  std::cout << filter->Info();

  // Run the contain test
  if (!run_contains(filter, total_items, fp_mult)) {
    std::cout << "Contain test failed\n";
    return 1;
  }

  // Save the filter to disk for the next test
  std::string filename = "filter.dat";
  filter->Save(filename);

  // Delete the filter to free up memory
  delete filter;

  /*
   * Run the filename test.
   */
  // Create a new filter that will load in the saved filter
  switch(bits_per_item) {
    case 2: filter = new CuckooFilter<size_t, 2>(filename); break;
    case 4: filter = new CuckooFilter<size_t, 4>(filename); break;
    case 8: filter = new CuckooFilter<size_t, 8>(filename); break;
    case 12: filter = new CuckooFilter<size_t, 12>(filename); break;
    case 16: filter = new CuckooFilter<size_t, 16>(filename); break;
    case 32: filter = new CuckooFilter<size_t, 32>(filename); break;
  }
  if (!filter->Valid()) {
    std::cout << "Failed to create cuckoo filter with <size_t, " << bits_per_item << "> and " << filename << " items\n";
    return 1;
  }

  // Dump the filter info (it should be the same as the original filter)
  std::cout << filter->Info();

  // Run the contain test
  if (!run_contains(filter, total_items, fp_mult)) {
    std::cout << "Contain test failed\n";
    return 1;
  }

  // Delete the filter to free up memory
  delete filter;

  /*
   * Run the mmap test.
   */
  // Open the saved file as a memory mapped file
  int fd = open(filename.c_str(), O_RDONLY, 0644);
  if (fd < 0) {
    std::cout << "Failed to open " << filename << " for memory mapping\n";
    return 1;
  }

  // Get the size of the file
  struct stat s;
  if (fstat(fd, &s) < 0) {
    std::cout << "Failed to stat " << filename << std::endl;
    return 1;
  }

  // Memory map the file
  void *data = mmap(NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    std::cout << "Failed to memory map " << filename << ", " << strerror(errno) << std::endl;
    close(fd);
    return 1;
  }
  if (data == NULL) {
    std::cout << "data is NULL\n";
  }
  madvise(data, s.st_size, MADV_WILLNEED);

  // Create a new filter that will load in the memory mapped filter
  switch(bits_per_item) {
    case 2: filter = new CuckooFilter<size_t, 2>(data, s.st_size); break;
    case 4: filter = new CuckooFilter<size_t, 4>(data, s.st_size); break;
    case 8: filter = new CuckooFilter<size_t, 8>(data, s.st_size); break;
    case 12: filter = new CuckooFilter<size_t, 12>(data, s.st_size); break;
    case 16: filter = new CuckooFilter<size_t, 16>(data, s.st_size); break;
    case 32: filter = new CuckooFilter<size_t, 32>(data, s.st_size); break;
  }
  if (!filter->Valid()) {
    std::cout << "Failed to create cuckoo filter with <size_t, " << bits_per_item << "> and memory mapped " << filename << " items\n";
    return 1;
  }

  // Dump the filter info (it should be the same as the original filter)
  std::cout << filter->Info();

  // Run the contain test
  if (!run_contains(filter, total_items, fp_mult)) {
    std::cout << "Contain test failed\n";
    return 1;
  }

  // Delete the filter to free up memory
  delete filter;

  // Close the open file
  close(fd);

  size_t num_buckets, num_items, data_size;
  if (!cuckoofilter::SavedInfo(filename, bits_per_item, num_buckets, num_items, data_size)) {
    std::cout << "Failed to get saved info for " << filename << std::endl;
  } else {
    std::cout << "Saved filter file information: " << std::endl;
    std::cout << "  Bits per item: " << bits_per_item << std::endl;
    std::cout << "  Number of buckets: " << num_buckets << std::endl;
    std::cout << "  Number of items: " << num_items << std::endl;
    std::cout << "  Data size: " << data_size << std::endl;
  }

  return 0;
}
