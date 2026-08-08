#include <cassert>
#include <memory>

#include <os/core/result.hpp>

using namespace os::core;

int main() {
    Result<int> ok{42};
    assert(ok);
    assert(ok.value() == 42);

    const Error expected = core_error(errors::core::invalid_argument);
    Result<int> failed{expected};
    assert(!failed);
    assert(failed.error() == expected);

    Result<std::unique_ptr<int>> move_only{std::make_unique<int>(7)};
    assert(move_only);
    assert(*move_only.value() == 7);

    Result<void> void_ok{};
    assert(void_ok);

    Result<void> void_failed{expected};
    assert(!void_failed);
    assert(void_failed.error() == expected);

    return 0;
}
