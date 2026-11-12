// Copyright 2016-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <ostream>

#include "coord/compound_ops.h"

namespace openage {
namespace coord {

/**
 * Template class for all coordinate types that have the members (x, y, z).
 *
 * There is a distinction between relative and absolute coordinate values
 * to disallow semantically-nonsensical origin-sensitive operations:
 *
 * absolute value + relative value -> absolute value
 * absolute value - absolute value -> relative value
 * relative value + relative value -> relative value
 * absolute value + absolute value -> undefined
 * relative value * scalar -> relative value
 * absolute value * scalar -> undefined
 *
 * This is the 'Absolute' version.
 *
 * 'Absolute' and 'Relative' are the absolute and relative types of the
 * derived class (CRTP).
 *
 * If you change this class, remember to update the gdb pretty printers
 * in etc/gdb_pretty/printers.py.
 */
template<typename CoordType, typename Absolute, typename Relative>
struct CoordXYZAbsolute : AbsoluteCompoundOps<Absolute, Relative> {

	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType x;
	CoordType y;
	CoordType z;

	/**
	 * We don't want to have a default constructor,
	 * because zero-initialization of absolute coordinates is
	 * origin-sensitive and thus non-sensical.
	 */
	CoordXYZAbsolute() = delete;

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordXYZAbsolute(const CoordType &x, const CoordType &y, const CoordType &z) : x{x}, y{y}, z{z} {}

	// copy constructor + assignment operator
	constexpr CoordXYZAbsolute(const CoordXYZAbsolute &other) = default;
	constexpr CoordXYZAbsolute &operator =(const CoordXYZAbsolute &other) = default;

	constexpr Absolute operator +(const Relative &other) const {
		return Absolute(this->x + other.x, this->y + other.y, this->z + other.z);
	}

	constexpr Relative operator -(const Absolute &other) const {
		return Relative(this->x - other.x, this->y - other.y, this->z - other.z);
	}

	constexpr Absolute operator -(const Relative &other) const {
		return Absolute(this->x - other.x, this->y - other.y, this->z - other.z);
	}

	friend constexpr bool operator ==(const Absolute &lhs, const Absolute &rhs) {
		return (lhs.x == rhs.x) && (lhs.y == rhs.y) && (lhs.z == rhs.z);
	}
};


/**
 * Template class for all coordinate types that have the members x and y.
 * This is the 'Relative' version.
 *
 * 'Absolute' and 'Relative' are the absolute and relative types of the
 * derived class (CRTP).
 *
 * If you change this class, remember to update the gdb pretty printers
 * in etc/gdb_pretty/printers.py.
 */
template<typename CoordType, typename Absolute, typename Relative>
struct CoordXYZRelative : RelativeCompoundOps<Relative> {
	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType x;
	CoordType y;
	CoordType z;

	/**
	 * Default constructor: sets the values to their defaults.
	 */
	CoordXYZRelative() : x{}, y{}, z{} {}

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordXYZRelative(const CoordType &x, const CoordType &y, const CoordType &z) : x{x}, y{y}, z{z} {}

	// copy constructor and assignment operator
	constexpr CoordXYZRelative(const CoordXYZRelative &other) = default;
	constexpr CoordXYZRelative &operator =(const CoordXYZRelative &other) = default;

	constexpr Relative operator +() const {
		return Relative(+this->x, +this->y, +this->z);
	}

	constexpr Relative operator -() const {
		return Relative(-this->x, -this->y, -this->z);
	}

	constexpr Absolute operator +(const Absolute &other) const {
		return Absolute(this->x + other.x, this->y + other.y, this->z + other.z);
	}

	constexpr Relative operator +(const Relative &other) const {
		return Relative(this->x + other.x, this->y + other.y, this->z + other.z);
	}

	constexpr Relative operator -(const Relative &other) const {
		return Relative(this->x - other.x, this->y - other.y, this->z - other.z);
	}

	template<typename ScalarType>
	constexpr Relative operator *(const ScalarType &scalar) const {
		return Relative(this->x * scalar, this->y * scalar, this->z * scalar);
	}

	template<typename ScalarType>
	constexpr Relative operator /(const ScalarType &scalar) const {
		return Relative(this->x / scalar, this->y / scalar, this->z / scalar);
	}

	friend constexpr bool operator ==(const Relative &lhs, const Relative &rhs) {
		return (lhs.x == rhs.x) && (lhs.y == rhs.y) && (lhs.z == rhs.z);
	}
};


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordXYZAbsolute<CoordType, Absolute, Relative> coord) {
	os << "[x: " << coord.x << ", y: " << coord.y << ", z: " << coord.z << "]";
	return os;
}


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordXYZRelative<CoordType, Absolute, Relative> coord) {
	os << "(x: " << coord.x << ", y: " << coord.y << ", z: " << coord.z << ")";
	return os;
}


} // namespace coord
} // namespace openage
