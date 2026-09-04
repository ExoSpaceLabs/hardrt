#include "hardrtpp.hpp"

#include <cstdint>

hardrt::StaticQueue<std::uint32_t, 65536> g_too_large;
