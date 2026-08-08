#include <cassert>
#include <type_traits>

#include <os/core/strong_id.hpp>

using namespace os::core;

static_assert(!std::is_same_v<ProcessId, RequestId>);
static_assert(!std::is_convertible_v<ServiceId, ProcessId>);
static_assert(!std::is_convertible_v<std::uint64_t, ProcessId>);

int main() {
    const ProcessId process{12};
    const ProcessId process2{12};
    const RequestId request{12};

    assert(process == process2);
    assert(process.value() == 12U);
    assert(request.value() == 12U);

    const PrincipalId principal{1U, 2U};
    assert(principal.high == 1U);
    assert(principal.low == 2U);

    return 0;
}
