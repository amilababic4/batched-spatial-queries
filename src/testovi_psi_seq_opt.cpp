#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

#include "psi/dependence/splitter.h"
#include "psi/kd_tree.h"
#include "psi_seq_buffer.h"

struct TestId {
    using id_type = int;
    int id = 0;
    bool operator<(const TestId& b) const { return id < b.id; }
    bool operator==(const TestId& b) const { return id == b.id; }
    friend std::ostream& operator<<(std::ostream& out, const TestId& a) { return out << a.id; }
};
using TestPoint = psi::aug_point<double, 2, TestId>;
using TestSplit = psi::orthogonal_split_rule<psi::max_stretch_dim<TestPoint>,
                                             psi::object_median<TestPoint>>;
using TestTree = psi::kd_tree<psi::tree_traits<TestPoint, TestSplit>>;

static bool provjeri(const std::vector<p07::Point>& points, const std::vector<p07::Rect>& queries,
                     size_t initial, bool expect_growth = false) {
    parlay::sequence<TestPoint> data(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        data[i][0] = points[i].x; data[i][1] = points[i].y; data[i].aug.id = points[i].id;
    }
    TestTree tree;
    if (!points.empty()) tree.build(parlay::make_slice(data));
    p07::seq_buffer::Scratch<TestPoint> scratch(points.size(), initial);
    const size_t first_capacity = scratch.buffer.size();
    std::vector<int> actual{12345};
    for (int rep = 0; rep < 2; ++rep) {
        for (const auto& r : queries) {
            TestTree::box_type box;
            box.first[0] = r.xmin; box.first[1] = r.ymin;
            box.second[0] = r.xmax; box.second[1] = r.ymax;
            p07::seq_buffer::upit_sa_baferom(tree, box, r, p07::ispravan(r), scratch, actual);
            std::vector<int> expected;
            if (p07::ispravan(r))
                for (const auto& p : points)
                    if (p07::unutra(p.x, p.y, r)) expected.push_back(p.id);
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (actual != expected) {
                std::cerr << "Razlika ID-jeva za upit; ocekivano " << expected.size()
                          << ", dobijeno " << actual.size() << '\n';
                return false;
            }
        }
    }
    if (expect_growth && scratch.buffer.size() <= first_capacity) {
        std::cerr << "Bafer se nije prosirio.\n";
        return false;
    }
    return true;
}

int main() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double pred = std::nextafter(0.25, 0.0);
    const double poslije = std::nextafter(0.25, 1.0);
    std::vector<p07::Point> edges{{0.25, 0.25, 0}, {0.25, 0.25, 1},
                                  {pred, 0.25, 2}, {poslije, 0.25, 3},
                                  {0.0, 1.0, 4}, {1.0, 0.0, 5}};
    std::vector<p07::Rect> rectangles{{0.25,0.25,0.25,0.25}, {0,0,1,1},
        {0,1,0,1}, {0.25,0,0.25,1}, {0.8,0.8,0.2,0.2}, {nan,0,1,1}};
    if (!provjeri(edges, rectangles, 2, true)) return 1;

    std::vector<p07::Point> many;
    for (int i = 0; i < 131; ++i)
        many.push_back({i < 64 ? 0.25 : 0.75, i < 64 ? 0.25 : 0.75, i});
    // Prvi upit puni pocetni bafer tacno do kraja; drugi prelazi njegovu velicinu.
    if (!provjeri(many, {{0.25,0.25,0.25,0.25}, {0,0,1,1}}, 64, true)) return 1;
    if (!provjeri(std::vector<p07::Point>(many.begin(), many.begin() + 4), {{0,0,1,1}}, 4)) return 1;
    if (!provjeri({}, {{0,0,1,1}}, 64)) return 1;
    if (!provjeri(edges, {}, 64)) return 1;
    std::cout << "PSI buffer helper: svi rubni testovi su prosli.\n";
}
