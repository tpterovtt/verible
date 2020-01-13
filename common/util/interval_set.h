// Copyright 2017-2020 The Verible Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VERIBLE_COMMON_UTIL_INTERVAL_SET_H_
#define VERIBLE_COMMON_UTIL_INTERVAL_SET_H_

#include <initializer_list>
#include <iostream>
#include <map>
#include <sstream>
#include <utility>

#include "absl/strings/str_join.h"
#include "common/util/auto_iterator.h"
#include "common/util/interval.h"
#include "common/util/logging.h"

namespace verible {

// Prints a sequence of intervals to stream.
// Iter can point to any Interval<> or any type constructible to Interval<>
// (like std::pair).
template <class Iter>
std::ostream& FormatIntervals(std::ostream& stream, Iter begin, Iter end) {
  typedef typename std::iterator_traits<Iter>::value_type value_type;
  return stream << absl::StrJoin(
             begin, end, ", ",
             [](std::string* out, const value_type& interval) {
               std::ostringstream temp_stream;
               temp_stream << AsInterval(interval);
               out->append(temp_stream.str());
             });
}

// Non-template private implementation class of IntervalSet.
class _IntervalSetImpl {
 protected:
  // Returns the first (interval) iterator that spans or follows 'value'.
  // Implementing this way avoids duplication between the const_iterator and
  // iterator variants.
  template <typename M>                            // M is a map-type
  static typename auto_iterator_selector<M>::type  // const_iterator or iterator
  FindLowerBound(M& intervals, const typename M::mapped_type& value) {
    const auto iter = intervals.lower_bound(value);
    if (iter == intervals.begin()) {
      return iter;
    }

    // Check whether the previous interval's .max spans 'value'.
    const auto prev = std::prev(iter);
    if (AsInterval(*prev).contains(value)) {
      return prev;
    }
    return iter;
  }

  template <typename M>                            // M is a map-type
  static typename auto_iterator_selector<M>::type  // const_iterator or iterator
  FindSpanningInterval(M& intervals,
                       const Interval<typename M::mapped_type>& interval) {
    CHECK(interval.valid());
    // Nothing 'contains' an empty interval.
    if (interval.empty()) return intervals.end();

    auto iter = intervals.upper_bound(interval.min);
    // lower_bound misses equality condition
    if (iter != intervals.begin()) {
      const auto prev = std::prev(iter);  // look at interval before
      CHECK_LE(prev->first, interval.min);
      if (AsInterval(*prev).contains(interval)) {
        return prev;
      }
    }
    // else interval.min is already less than lowest interval
    return intervals.end();
  }

  // Variant that finds an interval that covers a single value.
  // This is more efficient than checking for [value, value +1).
  template <typename M>                            // M is a map-type
  static typename auto_iterator_selector<M>::type  // const_iterator or iterator
  FindSpanningInterval(M& intervals, const typename M::mapped_type& value) {
    const auto iter = intervals.upper_bound(value);
    // lower_bound misses equality condition
    if (iter != intervals.begin()) {
      const auto prev = std::prev(iter);  // look at interval before
      if (AsInterval(*prev).contains(value)) {
        return prev;
      }
      // else value is greater than upper bound of this interval
    }
    // else value is less than lower bound of first interval
    return intervals.end();
  }
};

// IntervalSet represents a set of integral values.
// Set membership is efficiently represented as a collection of
// non-overlapping [min, max) intervals.
// Mutating operations will automatically merge abutting intervals.
template <typename T>
class IntervalSet : private _IntervalSetImpl {
 private:
  typedef std::map<T, T> impl_type;

 protected:
  typedef typename impl_type::iterator iterator;
  typedef typename impl_type::reverse_iterator reverse_iterator;

 public:
  typedef typename impl_type::value_type value_type;
  typedef typename impl_type::const_iterator const_iterator;
  typedef typename impl_type::const_reverse_iterator const_reverse_iterator;
  typedef typename impl_type::size_type size_type;

 public:
  IntervalSet() = default;
  IntervalSet(std::initializer_list<Interval<T>> ranges) {
    // Add-ing will properly fuse overlapping intervals and maintain intervals_'
    // invariants.
    for (const auto& range : ranges) {
      Add(range);
    }
  }

  IntervalSet(const IntervalSet<T>&) = default;
  IntervalSet(IntervalSet<T>&&) = default;
  ~IntervalSet() { CheckIntegrity(); }

  IntervalSet<T>& operator=(const IntervalSet<T>&) = default;
  IntervalSet<T>& operator=(IntervalSet<T>&&) = default;

 public:
  const_iterator begin(void) const { return intervals_.begin(); }

  const_iterator end(void) const { return intervals_.end(); }

  // Returns the number of disjoint intervals that compose this set.
  size_type size(void) const { return intervals_.size(); }

  // Returns sum of sizes of all intervals.
  size_type sum_of_sizes(void) const;

  // Returns true if the set contains no intervals/values.
  bool empty(void) const { return intervals_.empty(); }

  // Remove all intervals from the set.
  void clear(void) { intervals_.clear(); }

  void swap(IntervalSet<T>& other) { intervals_.swap(other.intervals_); }

  bool operator==(const IntervalSet<T>& other) const {
    return intervals_ == other.intervals_;
  }

  bool operator!=(const IntervalSet<T>& other) const {
    return !(*this == other);
  }

  // Returns true if value is a member of an interval in the set.
  bool Contains(const T& value) const {
    return Find(value) != intervals_.end();
  }

  // Returns true if interval is entirely contained by an interval in the set.
  // If interval is empty, return false.
  bool Contains(const Interval<T>& interval) const {
    return Find(interval) != intervals_.end();
  }

  // TODO(fangism): bool Contains(const IntervalSet<T>& interval) const;

  // Returns the first (interval) iterator that spans or follows 'value'.
  const_iterator LowerBound(const T& value) const {
    return _IntervalSetImpl::FindLowerBound(intervals_, value);
  }

  // Returns the first (interval) iterator that follows 'value'.
  const_iterator UpperBound(const T& value) const {
    return intervals_.upper_bound(value);
  }

  // Returns an iterator to the interval that entirely contains [min,max),
  // or the end iterator if no such interval exists, or the input is empty.
  const_iterator Find(const Interval<T>& interval) const {
    return _IntervalSetImpl::FindSpanningInterval(intervals_, interval);
  }

  // Returns an iterator to the interval that contains 'value',
  // or the end iterator if no such interval exists.
  const_iterator Find(const T& value) const {
    return _IntervalSetImpl::FindSpanningInterval(intervals_, value);
  }

  // Adds an interval to the interval set.
  // Also fuses any intervals that may result from the addition.
  void Add(const Interval<T>& interval) {
    CHECK(interval.valid());
    if (interval.empty()) return;  // adding empty interval changes nothing
    const auto& min = interval.min;
    const auto& max = interval.max;

    T new_max = max;
    iterator erase_end;
    const auto max_lb = LowerBound(max);
    if (max_lb == intervals_.end()) {
      // erase all the way to the end
      erase_end = max_lb;
    } else if (AsInterval(*max_lb).contains(max)) {
      // erase this interval, but use its max (.second)
      erase_end = std::next(max_lb);
      new_max = max_lb->second;
    } else {
      // erase up to the interval before this one
      erase_end = max_lb;
    }

    iterator erase_begin;
    const auto p = intervals_.insert({min, new_max});  // (bool, iterator)
    if (p.second) {
      // new interval was successfully inserted at p.first
      // If this abuts or overlaps with the previous interval, update that
      // one with new_max, and discard this one.
      if (p.first == intervals_.begin()) {
        erase_begin = std::next(p.first);
      } else {
        const auto prev = std::prev(p.first);
        if (prev->second >= min) {
          prev->second = new_max;
          erase_begin = p.first;
        } else {
          erase_begin = std::next(p.first);
        }
      }
    } else {
      // new interval was not inserted because there already exist key=min.
      // Re-use that interval, but update its max.
      p.first->second = new_max;
      // Erase intervals starting after that one.
      erase_begin = std::next(p.first);
    }

    // Finally erase range of obsolete intervals to maintain invariants.
    intervals_.erase(erase_begin, erase_end);

    CheckIntegrity();
  }

  // Adds a single value to the interval set.
  void Add(const T& value) { Add({value, value + 1}); }

  // TODO(fangism): other set-operations (union,intersection,difference)

 protected:
  // This operation is only intended for constructing test expect values.
  // It does not guarantee any invariants of intervals_.
  void AddUnsafe(const Interval<T>& interval) {
    intervals_[interval.min] = interval.max;
  }

  // Checks invariant properties described in class description.
  void CheckIntegrity(void) const {
    typedef Interval<T> interval_type;
    if (intervals_.empty()) return;

    // Check front outside of loop.
    const_iterator iter(intervals_.begin());
    const const_iterator end(intervals_.end());
    {
      const interval_type ii(*iter);
      CHECK(ii.valid());
      CHECK(!ii.empty());
    }
    // Track previous max, and check the rest in loop.
    T prev_max = iter->second;
    for (++iter; iter != end; ++iter) {
      {
        const interval_type ii(*iter);
        CHECK(ii.valid());
        CHECK(!ii.empty());
      }
      CHECK_LT(prev_max, iter->first);
      prev_max = iter->second;
    }
  }

  // Mutable variants of Find(), LowerBound() are protected to preserve
  // invariants.
  iterator Find(const Interval<T>& interval) {
    return _IntervalSetImpl::FindSpanningInterval(intervals_, interval);
  }
  iterator Find(const T& value) {
    return _IntervalSetImpl::FindSpanningInterval(intervals_, value);
  }
  iterator LowerBound(const T& value) {
    return _IntervalSetImpl::FindLowerBound(intervals_, value);
  }

 private:
  // Internal storage of intervals.
  // Invariants: all intervals are
  //   * non-overlapping
  //   * non-empty
  //   * ordered (by interval.min).
  impl_type intervals_;
};  // class IntervalSet

template <typename T>
void swap(IntervalSet<T>& t1, IntervalSet<T>& t2) {
  t1.swap(t2);
}

template <typename T>
std::ostream& operator<<(std::ostream& stream, const IntervalSet<T>& iset) {
  // Format each IntervalSet internal interval as an Interval<T>.
  return FormatIntervals(stream, iset.begin(), iset.end());
}

}  // namespace verible

#endif  // VERIBLE_COMMON_UTIL_INTERVAL_SET_H_
