// Copyright 2016-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <ostream>

#include "coord/compound_ops.h"

namespace openage {
namespace coord {

/**
 * Template class for all coordinate types that have the members (ne, se, up).
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
struct CoordNeSeUpAbsolute : AbsoluteCompoundOps<Absolute, Relative> {

	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType ne;
	CoordType se;
	CoordType up;

	/**
	 * We don't want to have a default constructor,
	 * because zero-initialization of absolute coordinates is
	 * origin-sensitive and thus non-sensical.
	 */
	CoordNeSeUpAbsolute() = delete;

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordNeSeUpAbsolute(const CoordType &ne, const CoordType &se, const CoordType &up) : ne{ne}, se{se}, up{up} {}

	// copy constructor + assignment operator
	constexpr CoordNeSeUpAbsolute(const CoordNeSeUpAbsolute &other) = default;
	constexpr CoordNeSeUpAbsolute &operator =(const CoordNeSeUpAbsolute &other) = default;

	constexpr Absolute operator +(const Relative &other) const {
		return Absolute(this->ne + other.ne, this->se + other.se, this->up + other.up);
	}

	constexpr Relative operator -(const Absolute &other) const {
		return Relative(this->ne - other.ne, this->se - other.se, this->up - other.up);
	}

	constexpr Absolute operator -(const Relative &other) const {
		return Absolute(this->ne - other.ne, this->se - other.se, this->up - other.up);
	}

	friend constexpr bool operator ==(const Absolute &lhs, const Absolute &rhs) {
		return (lhs.ne == rhs.ne) && (lhs.se == rhs.se) && (lhs.up == rhs.up);
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
struct CoordNeSeUpRelative : RelativeCompoundOps<Relative> {
	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType ne;
	CoordType se;
	CoordType up;

	/**
	 * Default constructor: sets the values to their defaults.
	 */
	CoordNeSeUpRelative() : ne{}, se{}, up{} {}

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordNeSeUpRelative(const CoordType &ne, const CoordType &se, const CoordType &up) : ne{ne}, se{se}, up{up} {}

	// copy constructor and assignment operator
	constexpr CoordNeSeUpRelative(const CoordNeSeUpRelative &other) = default;
	constexpr CoordNeSeUpRelative &operator =(const CoordNeSeUpRelative &other) = default;

	constexpr Relative operator +() const {
		return Relative(+this->ne, +this->se, +this->up);
	}

	constexpr Relative operator -() const {
		return Relative(-this->ne, -this->se, -this->up);
	}

	constexpr Absolute operator +(const Absolute &other) const {
		return Absolute(this->ne + other.ne, this->se + other.se, this->up + other.up);
	}

	constexpr Relative operator +(const Relative &other) const {
		return Relative(this->ne + other.ne, this->se + other.se, this->up + other.up);
	}

	constexpr Relative operator -(const Relative &other) const {
		return Relative(this->ne - other.ne, this->se - other.se, this->up - other.up);
	}

	template<typename ScalarType>
	constexpr Relative operator *(const ScalarType &scalar) const {
		return Relative(this->ne * scalar, this->se * scalar, this->up * scalar);
	}

	template<typename ScalarType>
	constexpr Relative operator /(const ScalarType &scalar) const {
		return Relative(this->ne / scalar, this->se / scalar, this->up / scalar);
	}

	friend constexpr bool operator ==(const Relative &lhs, const Relative &rhs) {
		return (lhs.ne == rhs.ne) && (lhs.se == rhs.se) && (lhs.up == rhs.up);
	}
};


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordNeSeUpAbsolute<CoordType, Absolute, Relative> coord) {
	os << "[ne: " << coord.ne << ", se: " << coord.se << ", up: " << coord.up << "]";
	return os;
}


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordNeSeUpRelative<CoordType, Absolute, Relative> coord) {
	os << "(ne: " << coord.ne << ", se: " << coord.se << ", up: " << coord.up << ")";
	return os;
}


} // namespace coord
} // namespace openage
