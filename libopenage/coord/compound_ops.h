// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once


namespace openage::coord {

/**
 * Compound assignment for absolute coordinate types, defined through the
 * binary operators of the derived class (CRTP).
 *
 * @tparam Absolute Absolute coordinate type deriving from this class.
 * @tparam Relative Matching relative coordinate type.
 */
template <typename Absolute, typename Relative>
struct AbsoluteCompoundOps {
	constexpr Absolute &operator+=(const Relative &other) {
		auto &self = static_cast<Absolute &>(*this);
		self = self + other;
		return self;
	}

	constexpr Absolute &operator-=(const Relative &other) {
		auto &self = static_cast<Absolute &>(*this);
		self = self - other;
		return self;
	}
};


/**
 * Compound assignment for relative coordinate types, defined through the
 * binary operators of the derived class (CRTP).
 *
 * @tparam Relative Relative coordinate type deriving from this class.
 */
template <typename Relative>
struct RelativeCompoundOps {
	constexpr Relative &operator+=(const Relative &other) {
		auto &self = static_cast<Relative &>(*this);
		self = self + other;
		return self;
	}

	constexpr Relative &operator-=(const Relative &other) {
		auto &self = static_cast<Relative &>(*this);
		self = self - other;
		return self;
	}

	template <typename ScalarType>
	constexpr Relative &operator*=(const ScalarType &scalar) {
		auto &self = static_cast<Relative &>(*this);
		self = self * scalar;
		return self;
	}

	template <typename ScalarType>
	constexpr Relative &operator/=(const ScalarType &scalar) {
		auto &self = static_cast<Relative &>(*this);
		self = self / scalar;
		return self;
	}
};

} // namespace openage::coord
