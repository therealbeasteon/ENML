#include <cassert>
#include <cstddef>
#include <limits>

#include <os/core/checked.hpp>

using namespace os::core;

int main() {
    auto add = checked_add(10U, 20U);
    assert(add);
    assert(add.value() == 30U);

    auto add_overflow = checked_add(std::numeric_limits<std::size_t>::max(), 1U);
    assert(!add_overflow);
    assert(add_overflow.error() == core_error(errors::core::overflow));

    auto mul = checked_mul(12U, 3U);
    assert(mul);
    assert(mul.value() == 36U);

    auto mul_overflow = checked_mul(std::numeric_limits<std::size_t>::max(), 2U);
    assert(!mul_overflow);

    assert(range_fits(10U, 0U, 10U));
    assert(range_fits(10U, 10U, 0U));
    assert(!range_fits(10U, 11U, 0U));
    assert(!range_fits(10U, 5U, 6U));

    return 0;
}
