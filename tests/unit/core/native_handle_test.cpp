#include <cassert>

#include <fcntl.h>
#include <unistd.h>
#include <utility>

#include <os/core/native_handle.hpp>

using namespace os::core;

int main() {
    const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);

    NativeHandle a{fd};
    assert(a.valid());

    NativeHandle b{std::move(a)};
    assert(!a.valid());
    assert(b.valid());

    const int released = b.release_native();
    assert(!b.valid());
    assert(released >= 0);

    assert(::close(released) == 0);
    return 0;
}
