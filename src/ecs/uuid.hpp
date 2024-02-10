#pragma once
#include "pch.hpp"

struct UUID {
	UUID();
	explicit UUID(u64 _uuid);
	UUID(const UUID&) = default;

	explicit operator u64() const { return uuid; }
	u64 uuid;
};


namespace std {
	template <typename T> struct hash;

	template<>
	struct hash<UUID> {
		auto operator()(const UUID& uuid) const -> std::size_t {
			return *reinterpret_cast<const u64*>(&uuid);
		}
	};

}