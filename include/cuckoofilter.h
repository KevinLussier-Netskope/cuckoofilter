#ifndef CUCKOO_FILTER_CUCKOO_FILTER_H_
#define CUCKOO_FILTER_CUCKOO_FILTER_H_

#include <assert.h>
#include <algorithm>
#include <fstream>
#include "singletable.h"
#include "twoindependentmultiplyshift.h"

namespace cuckoofilter {
// status returned by a cuckoo filter operation
enum Status {
  Ok = 0,
  NotFound = 1,
  NotEnoughSpace = 2,
  NotSupported = 3,
};

// maximum number of cuckoo kicks before claiming failure
const size_t kMaxCuckooCount = 500;

// Base cuckoo filter class
template <typename ItemType>
class BaseCuckooFilter
{
  public:
    BaseCuckooFilter() {}
    virtual ~BaseCuckooFilter() {}

  // Add an item to the filter.
  virtual Status Add(const ItemType &item) = 0;

  // Report if the item is inserted, with false positive rate.
  virtual Status Contain(const ItemType &item) const = 0;

  // Delete an key from the filter
  virtual Status Delete(const ItemType &item) = 0;

  /* methods for providing stats  */
  // summary infomation
  virtual std::string Info() const = 0;

  // number of current inserted items;
  virtual size_t Size() const = 0;

  // size of the filter in bytes.
  virtual size_t SizeInBytes() const = 0;

  // save the filter to a file
  virtual bool Save(const std::string path) const = 0;

  // returns if the filter if valid
  virtual bool Valid() const = 0;
};

// A cuckoo filter class exposes a Bloomier filter interface,
// providing methods of Add, Delete, Contain. It takes three
// template parameters:
//   ItemType:  the type of item you want to insert
//   bits_per_item: how many bits each item is hashed into
//   TableType: the storage of table, SingleTable by default, and
// PackedTable to enable semi-sorting
template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType = SingleTable,
          typename HashFamily = TwoIndependentMultiplyShift>
class CuckooFilter : public BaseCuckooFilter<ItemType> {
  // Storage of items
  TableType<bits_per_item> *table_;

  // Number of items stored
  size_t num_items_;

  typedef struct {
    size_t index;
    uint32_t tag;
    bool used;
  } VictimCache;

  // The header we will use when we save the filter
  struct save_header {
    size_t num_buckets_;
    size_t num_items_;
    VictimCache victim_;
    HashFamily hasher_;
  };

  VictimCache victim_;

  HashFamily hasher_;

  // Buffer created if we read the filter from a file
  char *readbuf_;

  inline size_t IndexHash(uint32_t hv) const {
    // table_->num_buckets is always a power of two, so modulo can be replaced
    // with
    // bitwise-and:
    return hv & (table_->NumBuckets() - 1);
  }

  inline uint32_t TagHash(uint32_t hv) const {
    uint32_t tag;
    tag = hv & ((1ULL << bits_per_item) - 1);
    tag += (tag == 0);
    return tag;
  }

  inline void GenerateIndexTagHash(const ItemType& item, size_t* index,
                                   uint32_t* tag) const {
    const uint64_t hash = hasher_(item);
    *index = IndexHash(hash >> 32);
    *tag = TagHash(hash);
  }

  inline size_t AltIndex(const size_t index, const uint32_t tag) const {
    // NOTE(binfan): originally we use:
    // index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
    // now doing a quick-n-dirty way:
    // 0x5bd1e995 is the hash constant from MurmurHash2
    return IndexHash((uint32_t)(index ^ (tag * 0x5bd1e995)));
  }

  Status AddImpl(const size_t i, const uint32_t tag);

  // load factor is the fraction of occupancy
  double LoadFactor() const { return 1.0 * Size() / table_->SizeInTags(); }

  double BitsPerItem() const { return 8.0 * table_->SizeInBytes() / Size(); }

 public:
  explicit CuckooFilter(const size_t max_num_keys) : table_(nullptr), num_items_(0), victim_(), hasher_(), readbuf_(nullptr) {
    // Build the filter fased on the max number of keys and the bit size.
    size_t assoc = 4;
    size_t num_buckets = upperpower2(std::max<uint64_t>(1, max_num_keys / assoc));
    double frac = (double)max_num_keys / num_buckets / assoc;
    if (frac > 0.96) {
      num_buckets <<= 1;
    }
    victim_.used = false;
    try {
      table_ = new(std::nothrow) TableType<bits_per_item>(num_buckets);
    } catch (std::bad_alloc& ba) {
      // Caller should call Valid() to ensure filter is built
    }
  }

  explicit CuckooFilter(void *addr, size_t length) : table_(nullptr), num_items_(0), victim_(), hasher_(), readbuf_(nullptr) {
    // Load the filter from the specified buffer. We will not own the data we
    // read in, so the caller better not free it...
    struct save_header *sh = reinterpret_cast<struct save_header *>(addr);
    num_items_ = sh->num_items_;
    victim_ = sh->victim_;
    hasher_ = sh->hasher_;
    char *data = (char *)addr + sizeof(struct save_header);
    length = length - sizeof(struct save_header);
    try {
      table_ = new TableType<bits_per_item>(data, length);
    } catch (std::bad_alloc& ba) {
      // Caller should call Valid() to ensure filter is built
    }
  }

  explicit CuckooFilter(const std::string &path) : table_(nullptr), num_items_(0), victim_(), hasher_(), readbuf_(nullptr) {
    // Read the saved filter from the specified path. We will own the data we
    // read in and free it in the destructor.
    std::ifstream rf(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!rf) {
      return;
    }
    size_t size = rf.tellg();
    readbuf_ = new char[size];
    if (!readbuf_) {
      return;
    }
    rf.seekg(0);
    if (!rf.read(readbuf_, size)) {
      return;
    }
    rf.close();

    struct save_header *sh = reinterpret_cast<struct save_header *>(readbuf_);
    num_items_ = sh->num_items_;
    victim_ = sh->victim_;
    hasher_ = sh->hasher_;
    char *data = readbuf_ + sizeof(struct save_header);
    size_t length = size - sizeof(struct save_header);
    try {
      table_ = new TableType<bits_per_item>(data, length);
    } catch (std::bad_alloc& ba) {
      // Caller should call Valid() to ensure filter is built
    }
  }

  ~CuckooFilter() { delete table_; delete readbuf_; }

  // Add an item to the filter.
  Status Add(const ItemType &item);

  // Report if the item is inserted, with false positive rate.
  Status Contain(const ItemType &item) const;

  // Delete an key from the filter
  Status Delete(const ItemType &item);

  /* methods for providing stats  */
  // summary infomation
  std::string Info() const;

  // number of current inserted items;
  size_t Size() const { return num_items_; }

  // size of the filter in bytes.
  size_t SizeInBytes() const { return table_->SizeInBytes(); }

  // save the filter to a file
  bool Save(const std::string path) const {
    // Build the header
    struct save_header sh;
    sh.num_buckets_ = table_->NumBuckets();
    sh.num_items_ = Size();
    sh.victim_ = victim_;
    sh.hasher_ = hasher_;

    const unsigned char *data = table_->Data();
    size_t length = table_->SizeInBytes();
    std::ofstream wf(path, std::ios::out | std::ios::binary);
    if(!wf) {
      return false;
    }
    wf.write(reinterpret_cast<const char*>(&sh), sizeof(sh));
    wf.write(reinterpret_cast<const char*>(data), length);
    wf.close();
    if(!wf.good()) {
      return false;
    }

    return true;
  }

  bool Valid() const {
    // Valid means we have a table loaded
    return table_ != nullptr;
  }

};

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Add(
    const ItemType &item) {
  size_t i;
  uint32_t tag;

  if (victim_.used) {
    return NotEnoughSpace;
  }

  GenerateIndexTagHash(item, &i, &tag);
  return AddImpl(i, tag);
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::AddImpl(
    const size_t i, const uint32_t tag) {
  size_t curindex = i;
  uint32_t curtag = tag;
  uint32_t oldtag;

  for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
    bool kickout = count > 0;
    oldtag = 0;
    if (table_->InsertTagToBucket(curindex, curtag, kickout, oldtag)) {
      num_items_++;
      return Ok;
    }
    if (kickout) {
      curtag = oldtag;
    }
    curindex = AltIndex(curindex, curtag);
  }

  return Ok;
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Contain(
    const ItemType &key) const {
  bool found = false;
  size_t i1, i2;
  uint32_t tag;

  GenerateIndexTagHash(key, &i1, &tag);
  i2 = AltIndex(i1, tag);

  assert(i1 == AltIndex(i2, tag));

  found = victim_.used && (tag == victim_.tag) &&
          (i1 == victim_.index || i2 == victim_.index);

  if (found || table_->FindTagInBuckets(i1, i2, tag)) {
    return Ok;
  } else {
    return NotFound;
  }
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Delete(
    const ItemType &key) {
  size_t i1, i2;
  uint32_t tag;

  GenerateIndexTagHash(key, &i1, &tag);
  i2 = AltIndex(i1, tag);

  if (table_->DeleteTagFromBucket(i1, tag)) {
    num_items_--;
    goto TryEliminateVictim;
  } else if (table_->DeleteTagFromBucket(i2, tag)) {
    num_items_--;
    goto TryEliminateVictim;
  } else if (victim_.used && tag == victim_.tag &&
             (i1 == victim_.index || i2 == victim_.index)) {
    // num_items_--;
    victim_.used = false;
    return Ok;
  } else {
    return NotFound;
  }
TryEliminateVictim:
  if (victim_.used) {
    victim_.used = false;
    size_t i = victim_.index;
    uint32_t tag = victim_.tag;
    AddImpl(i, tag);
  }
  return Ok;
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
std::string CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Info() const {
  std::stringstream ss;
  ss << "CuckooFilter Status:\n"
     << "\t\t" << table_->Info() << "\n"
     << "\t\tKeys stored: " << Size() << "\n"
     << "\t\tLoad factor: " << LoadFactor() << "\n"
     << "\t\tHashtable size: " << (table_->SizeInBytes()) << " bytes\n";
  if (Size() > 0) {
    ss << "\t\tbit/key:   " << BitsPerItem() << "\n";
  } else {
    ss << "\t\tbit/key:   N/A\n";
  }
  return ss.str();
}
}  // namespace cuckoofilter
#endif  // CUCKOO_FILTER_CUCKOO_FILTER_H_
