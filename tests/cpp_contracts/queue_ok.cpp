#include "hardrtpp.hpp"

#include <cstdint>
#include <type_traits>

struct Packet {
    std::uint32_t id;
    std::uint16_t flags;
};

static_assert(std::is_trivially_copyable<Packet>::value,
              "test payload must be trivially copyable");
static_assert(std::is_same<hardrt::Queue<Packet, 4>,
                           hardrt::StaticQueue<Packet, 4>>::value,
              "Queue<T,N> must remain an alias for StaticQueue<T,N>");

hardrt::QueueRef<Packet> g_ref;
hardrt::StaticQueue<Packet, 4> g_static;
hardrt::Queue<Packet, 4> g_alias;

int main() {
    const char *canonical = hardrt::System::version_string();
    const char *alias = hardrt::System::version();
    (void)canonical;
    (void)alias;
    return 0;
}
