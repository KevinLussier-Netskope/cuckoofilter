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
};

}
#endif // CUCKOO_FILTER_TWOINDEPENDENTMULTIPLESHIFT_H_
