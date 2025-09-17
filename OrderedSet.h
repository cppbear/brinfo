#pragma once

#include <unordered_set>
#include <vector>

using namespace std;

namespace BrInfo {

template <typename T> class OrderedSet {
  vector<T> Vec;
  unordered_set<T> Set;

public:
  void insert(const T &Value) {
    if (Set.find(Value) != Set.end()) {
      return;
    }
    Vec.push_back(Value);
    Set.insert(Value);
  }

  bool contains(const T &Value) const { return Set.find(Value) != Set.end(); }

  const vector<T> &orderedElements() const { return Vec; }

  void merge(const OrderedSet<T> &Other) {
    for (const T &Value : Other.orderedElements()) {
      insert(Value);
    }
  }

  bool empty() const { return Vec.empty(); }
};
} // namespace BrInfo