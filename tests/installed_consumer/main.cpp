#include <hardrtpp.hpp>

#include <cstdint>
#include <cstring>
#include <type_traits>

static_assert(std::is_same<hardrt::Queue<std::uint32_t, 4>,
                           hardrt::StaticQueue<std::uint32_t, 4>>::value,
              "installed Queue alias must match StaticQueue");

int main() {
    hardrt::Queue<std::uint32_t, 4> queue;
    (void)queue;
    return std::strcmp(hardrt::System::version(),
                       hardrt::System::version_string()) == 0 ? 0 : 1;
}
