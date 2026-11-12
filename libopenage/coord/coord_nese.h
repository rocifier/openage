// Copyright 2016-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <ostream>

#include "coord/compound_ops.h"

namespace openage {
namespace coord {

/**
 * Template class for all coordinate types that have the members (ne, se).
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
struct CoordNeSeAbsolute : AbsoluteCompoundOps<Absolute, Relative> {

	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType ne;
	CoordType se;

	/**
	 * We don't want to have a default constructor,
	 * because zero-initialization of absolute coordinates is
	 * origin-sensitive and thus non-sensical.
	 */
	CoordNeSeAbsolute() = delete;

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordNeSeAbsolute(const CoordType &ne, const CoordType &se) : ne{ne}, se{se} {}

	// copy constructor + assignment operator
	constexpr CoordNeSeAbsolute(const CoordNeSeAbsolute &other) = default;
	constexpr CoordNeSeAbsolute &operator =(const CoordNeSeAbsolute &other) = default;

	constexpr Absolute operator +(const Relative &other) const {
		return Absolute(this->ne + other.ne, this->se + other.se);
	}

	constexpr Relative operator -(const Absolute &other) const {
		return Relative(this->ne - other.ne, this->se - other.se);
	}

	constexpr Absolute operator -(const Relative &other) const {
		return Absolute(this->ne - other.ne, this->se - other.se);
	}

	friend constexpr bool operator ==(const Absolute &lhs, const Absolute &rhs) {
		return (lhs.ne == rhs.ne) && (lhs.se == rhs.se);
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
struct CoordNeSeRelative : RelativeCompoundOps<Relative> {
	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType ne;
	CoordType se;

	/**
	 * Default constructor: sets the values to their defaults.
	 */
	CoordNeSeRelative() : ne{}, se{} {}

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordNeSeRelative(const CoordType &ne, const CoordType &se) : ne{ne}, se{se} {}

	// copy constructor and assignment operator
	constexpr CoordNeSeRelative(const CoordNeSeRelative &other) = default;
	constexpr CoordNeSeRelative &operator =(const CoordNeSeRelative &other) = default;

	constexpr Relative operator +() const {
		return Relative(+this->ne, +this->se);
	}

	constexpr Relative operator -() const {
		return Relative(-this->ne, -this->se);
	}

	constexpr Absolute operator +(const Absolute &other) const {
		return Absolute(this->ne + other.ne, this->se + other.se);
	}

	constexpr Relative operator +(const Relative &other) const {
		return Relative(this->ne + other.ne, this->se + other.se);
	}

	constexpr Relative operator -(const Relative &other) const {
		return Relative(this->ne - other.ne, this->se - other.se);
	}

	template<typename ScalarType>
	constexpr Relative operator *(const ScalarType &scalar) const {
		return Relative(this->ne * scalar, this->se * scalar);
	}

	template<typename ScalarType>
	constexpr Relative operator /(const ScalarType &scalar) const {
		return Relative(this->ne / scalar, this->se / scalar);
	}

	friend constexpr bool operator ==(const Relative &lhs, const Relative &rhs) {
		return (lhs.ne == rhs.ne) && (lhs.se == rhs.se);
	}
};


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordNeSeAbsolute<CoordType, Absolute, Relative> coord) {
	os << "[ne: " << coord.ne << ", se: " << coord.se << "]";
	return os;
}


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordNeSeRelative<CoordType, Absolute, Relative> coord) {
	os << "(ne: " << coord.ne << ", se: " << coord.se << ")";
	return os;
}


} // namespace coord
} // namespace openage
