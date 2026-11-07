// Copyright 2016-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <ostream>

namespace openage {
namespace coord {

/**
 * Template class for all coordinate types that have the members (x, y).
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
struct CoordXYAbsolute {

	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType x;
	CoordType y;

	/**
	 * We don't want to have a default constructor,
	 * because zero-initialization of absolute coordinates is
	 * origin-sensitive and thus non-sensical.
	 */
	CoordXYAbsolute() = delete;

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordXYAbsolute(const CoordType &x, const CoordType &y) : x{x}, y{y} {}

	// copy constructor + assignment operator
	constexpr CoordXYAbsolute(const CoordXYAbsolute &other) = default;
	constexpr CoordXYAbsolute &operator =(const CoordXYAbsolute &other) = default;

	constexpr Absolute operator +(const Relative &other) const {
		return Absolute(this->x + other.x, this->y + other.y);
	}

	constexpr Relative operator -(const Absolute &other) const {
		return Relative(this->x - other.x, this->y - other.y);
	}

	constexpr Absolute operator -(const Relative &other) const {
		return Absolute(this->x - other.x, this->y - other.y);
	}

	constexpr Absolute &operator +=(const Relative &other) {
		*this = *this + other;
		return static_cast<Absolute &>(*this);
	}

	constexpr Absolute &operator -=(const Relative &other) {
		*this = *this - other;
		return static_cast<Absolute &>(*this);
	}

	friend constexpr bool operator ==(const Absolute &lhs, const Absolute &rhs) {
		return (lhs.x == rhs.x) && (lhs.y == rhs.y);
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
struct CoordXYRelative {
	using elem_t = CoordType;

	// The member variables to store the actual data.
	CoordType x;
	CoordType y;

	/**
	 * Default constructor: sets the values to their defaults.
	 */
	CoordXYRelative() : x{}, y{} {}

	/**
	 * From-individual-values constructor
	 */
	constexpr CoordXYRelative(const CoordType &x, const CoordType &y) : x{x}, y{y} {}

	// copy constructor and assignment operator
	constexpr CoordXYRelative(const CoordXYRelative &other) = default;
	constexpr CoordXYRelative &operator =(const CoordXYRelative &other) = default;

	constexpr Relative operator +() const {
		return Relative(+this->x, +this->y);
	}

	constexpr Relative operator -() const {
		return Relative(-this->x, -this->y);
	}

	constexpr Absolute operator +(const Absolute &other) const {
		return Absolute(this->x + other.x, this->y + other.y);
	}

	constexpr Relative operator +(const Relative &other) const {
		return Relative(this->x + other.x, this->y + other.y);
	}

	constexpr Relative operator -(const Relative &other) const {
		return Relative(this->x - other.x, this->y - other.y);
	}

	template<typename ScalarType>
	constexpr Relative operator *(const ScalarType &scalar) const {
		return Relative(this->x * scalar, this->y * scalar);
	}

	template<typename ScalarType>
	constexpr Relative operator /(const ScalarType &scalar) const {
		return Relative(this->x / scalar, this->y / scalar);
	}

	constexpr Relative &operator +=(const Relative &other) {
		*this += other;
		return static_cast<Relative &>(*this);
	}

	constexpr Relative &operator -=(const Relative &other) {
		*this -= other;
		return static_cast<Relative &>(*this);
	}

	template<typename ScalarType>
	constexpr Relative &operator *=(const ScalarType &scalar) {
		*this *= scalar;
		return static_cast<Relative &>(*this);
	}

	template<typename ScalarType>
	constexpr Relative &operator /=(const ScalarType &scalar) {
		*this /= scalar;
		return static_cast<Relative &>(*this);
	}

	friend constexpr bool operator ==(const Relative &lhs, const Relative &rhs) {
		return (lhs.x == rhs.x) && (lhs.y == rhs.y);
	}
};


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordXYAbsolute<CoordType, Absolute, Relative> coord) {
	os << "[x: " << coord.x << ", y: " << coord.y << "]";
	return os;
}


template<typename CoordType, typename Absolute, typename Relative>
std::ostream &operator <<(std::ostream &os, CoordXYRelative<CoordType, Absolute, Relative> coord) {
	os << "(x: " << coord.x << ", y: " << coord.y << ")";
	return os;
}


} // namespace coord
} // namespace openage
