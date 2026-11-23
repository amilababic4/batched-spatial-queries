#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "common/podaci.h"
#include "parlay/sequence.h"

#if defined(P07_PROFILE_MARKERS) && (defined(__GNUC__) || defined(__clang__))
#define P07_BUFFER_FAZA __attribute__((noinline))
#elif defined(P07_PROFILE_MARKERS) && defined(_MSC_VER)
#define P07_BUFFER_FAZA __declspec(noinline)
#else
#define P07_BUFFER_FAZA
#endif

namespace p07::seq_buffer {

// Bafer zivi kroz sve upite i ponavljanja; ogranicen je brojem ulaznih tacaka.
template<class Point>
struct Scratch {
    size_t max_points;
    parlay::sequence<Point> buffer;
    explicit Scratch(size_t n, size_t pocetni = 64)
        : max_points(n), buffer(std::min(n, std::max(size_t{1}, pocetni))) {}
};

template<class Point>
P07_BUFFER_FAZA void tacni_filter(const parlay::sequence<Point>& kandidati,
                                  size_t broj, const Rect& r, std::vector<int>& ids) {
    for (size_t i = 0; i < broj; ++i) {
        const auto& p = kandidati[i];
        if (p07::unutra(p[0], p[1], r)) ids.push_back(p.aug.id);
    }
}

// validan se racuna jednom prije mjerenja; neispravne kutije se ne salju PSI-ju.
template<class Tree, class Point>
P07_BUFFER_FAZA void upit_sa_baferom(const Tree& tree, const typename Tree::box_type& box,
                                     const Rect& r, bool validan, Scratch<Point>& scratch,
                                     std::vector<int>& ids) {
    ids.clear();
    if (!validan || scratch.max_points == 0) return;
    size_t broj;
    for (;;) {
        broj = tree.range_query(box, parlay::make_slice(scratch.buffer)).first;
        const size_t kapacitet = scratch.buffer.size();
        if (broj < kapacitet || kapacitet == scratch.max_points) break;
        const size_t novi = kapacitet > scratch.max_points / 2
                          ? scratch.max_points : kapacitet * 2;
        scratch.buffer = parlay::sequence<Point>(novi);
    }
    ids.reserve(broj);
    tacni_filter(scratch.buffer, broj, r, ids);
}

}  // namespace p07::seq_buffer

#undef P07_BUFFER_FAZA
