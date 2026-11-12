// Copyright 2016-2026 the openage authors. See copying.md for legal info.

#include "coord_xy.h"

#include <cstdint>

#include "../testing/testing.h"
#include "coord_xyz.h"
#include "phys.h"
#include "term.h"
#include "tile.h"
#include "../util/stringformatter.h"

namespace openage {
namespace coord {
namespace tests {

namespace {

// pack these test types into an anonynous namespace to prevent them from
// polluting the rest of the project.

struct TestCoordsRelative;
struct TestCoordsAbsolute;

struct TestCoordsAbsolute : CoordXYAbsolute<int, TestCoordsAbsolute, TestCoordsRelative> {
    using CoordXYAbsolute<int, TestCoordsAbsolute, TestCoordsRelative>::CoordXYAbsolute;
};

struct TestCoordsRelative : CoordXYRelative<int, TestCoordsAbsolute, TestCoordsRelative> {
    using CoordXYRelative<int, TestCoordsAbsolute, TestCoordsRelative>::CoordXYRelative;
};

struct TestCoords3Relative;
struct TestCoords3Absolute;

struct TestCoords3Absolute : CoordXYZAbsolute<int, TestCoords3Absolute, TestCoords3Relative> {
    using CoordXYZAbsolute<int, TestCoords3Absolute, TestCoords3Relative>::CoordXYZAbsolute;
};

struct TestCoords3Relative : CoordXYZRelative<int, TestCoords3Absolute, TestCoords3Relative> {
    using CoordXYZRelative<int, TestCoords3Absolute, TestCoords3Relative>::CoordXYZRelative;
};

/**
 * Compound assignment must match the binary operators and return the
 * assigned object, so that chained assignments accumulate.
 */
template <typename Absolute, typename Relative>
void check_compound_assignment(const Absolute &origin, const Relative &step) {
    Absolute absolute{origin};
    TESTEQUALS(&(absolute += step), &absolute);
    TESTEQUALS(absolute, origin + step);
    TESTEQUALS(&(absolute -= step), &absolute);
    TESTEQUALS(absolute, origin);
    (absolute += step) -= step + step;
    TESTEQUALS(absolute, origin - step);

    Relative relative{step};
    TESTEQUALS(&(relative += step), &relative);
    TESTEQUALS(relative, step + step);
    TESTEQUALS(&(relative -= step), &relative);
    TESTEQUALS(relative, step);
    TESTEQUALS(&(relative *= 6), &relative);
    TESTEQUALS(relative, step * 6);
    TESTEQUALS(&(relative /= 3), &relative);
    TESTEQUALS(relative, step * 2);
    ((relative += step) *= 2) /= 3;
    TESTEQUALS(relative, step * 2);
}

} // anonymous namespace


/**
 * test method for the base CoordXY* classes.
 */
void coord() {
    // test comparison
    TESTEQUALS(TestCoordsAbsolute(3, 4) == TestCoordsAbsolute(3, 4), true);
    TESTEQUALS(TestCoordsAbsolute(3, 4) != TestCoordsAbsolute(3, 4), false);
    TESTEQUALS(TestCoordsAbsolute(3, 4) == TestCoordsAbsolute(3, 5), false);
    TESTEQUALS(TestCoordsAbsolute(3, 4) == TestCoordsAbsolute(4, 4), false);
    TESTEQUALS(TestCoordsAbsolute(3, 4) != TestCoordsAbsolute(3, 5), true);

    TESTEQUALS(TestCoordsRelative(3, 4) == TestCoordsRelative(3, 4), true);
    TESTEQUALS(TestCoordsRelative(3, 4) != TestCoordsRelative(3, 4), false);
    TESTEQUALS(TestCoordsRelative(3, 4) == TestCoordsRelative(3, 5), false);
    TESTEQUALS(TestCoordsRelative(3, 4) == TestCoordsRelative(4, 4), false);
    TESTEQUALS(TestCoordsRelative(3, 4) != TestCoordsRelative(3, 5), true);

    // test copy constructor / assignment operator
    TestCoordsAbsolute a0(1, 2);
    TestCoordsRelative r0(3, 4);

    TESTEQUALS(a0, TestCoordsAbsolute(1, 2));
    TESTEQUALS(r0, TestCoordsRelative(3, 4));

    TestCoordsAbsolute a1{a0};
    TestCoordsRelative r1{r0};

    TESTEQUALS(a0, a1);
    TESTEQUALS(r0, r1);

    a1 = a0;
    r1 = r0;

    TESTEQUALS(a0, a1);
    TESTEQUALS(r0, r1);

    // test unary +/-
    TESTEQUALS(-r0, TestCoordsRelative(-3, -4));
    TESTEQUALS(+r0, TestCoordsRelative(3, 4));

    // test addition
    TESTEQUALS(r0 + r0, TestCoordsRelative(6, 8));
    TESTEQUALS(a0 + r0, TestCoordsAbsolute(4, 6));
    TESTEQUALS(r0 + a0, TestCoordsAbsolute(4, 6));

    // test scalar multiplication and division
    TESTEQUALS(r0 * 3, TestCoordsRelative(9, 12));
    TESTEQUALS(r0 / 3, TestCoordsRelative(1, 1));

    // test compound assignment for every coordinate family
    check_compound_assignment(TestCoordsAbsolute(1, 2), TestCoordsRelative(3, -4));
    check_compound_assignment(TestCoords3Absolute(1, 2, 3), TestCoords3Relative(3, -4, 5));
    check_compound_assignment(term(1, 2), term_delta(3, -4));
    check_compound_assignment(tile(1, 2), tile_delta(3, -4));
    check_compound_assignment(tile3(1, 2, 3), tile3_delta(3, -4, 5));
    check_compound_assignment(phys3(1.5, 2, 3), phys3_delta(3, -4.25, 5));

    // test string representation
    util::FString s;
    s << TestCoordsAbsolute(1, 2);
    TESTEQUALS(std::string(s), "[x: 1, y: 2]");

    s.reset();
    s << TestCoordsRelative(3, 4);
    TESTEQUALS(std::string(s), "(x: 3, y: 4)");
}

}}} // openage::coord::tests
