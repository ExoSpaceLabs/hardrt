#include "hardrtpp.hpp"

#include <cstdint>

struct Packet {
    std::uint32_t id;
    std::uint16_t flags;
};

static_assert(std::is_trivially_copyable<Packet>::value, "test payload must be trivially copyable");

hardrt::QueueRef<Packet> g_ref;
hardrt::StaticQueue<Packet, 4> g_static;

int main() {
    return 0;
}
