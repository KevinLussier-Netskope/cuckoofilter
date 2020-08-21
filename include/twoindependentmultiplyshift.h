#ifndef CUCKOO_FILTER_TWOINDEPENDENTMULTIPLESHIFT_H_
#define CUCKOO_FILTER_TWOINDEPENDENTMULTIPLESHIFT_H_

#include <random>

namespace cuckoofilter {

// See Martin Dietzfelbinger, "Universal hashing and k-wise independent random
// variables via integer arithmetic without primes".
class TwoIndependentMultiplyShift {
  unsigned __int128 multiply_, add_;

 public:
  TwoIndependentMultiplyShift() {
    ::std::random_device random;
    for (auto v : {&multiply_, &add_}) {
      *v = random();
      for (int i = 1; i <= 4; ++i) {
        *v = *v << 32;
        *v |= random();
      }
    }
  }

  TwoIndependentMultiplyShift(TwoIndependentMultiplyShift &src) {
    multiply_ = src.multiply_;
    add_ = src.add_;
  }

  uint64_t operator()(uint64_t key) const {
    return (add_ + multiply_ * static_cast<decltype(multiply_)>(key)) >> 64;
  }

  bool save(unsigned char *buf, size_t len) const {
    if (len < 2 * sizeof(__int128)) {
      return false;
    }
    memcpy(buf, &multiply_, sizeof(__int128));
    memcpy(buf + sizeof(__int128), &add_, sizeof(__int128));
    return true;
  }

  bool load(unsigned char *buf, size_t len) {
    if (len < 2 * sizeof(__int128)) {
      return false;
    }
    memcpy(&multiply_, buf, sizeof(__int128));
    memcpy(&add_, buf + sizeof(__int128), sizeof(__int128));
    return true;
  }
};

}
#endif // CUCKOO_FILTER_TWOINDEPENDENTMULTIPLESHIFT_H_
