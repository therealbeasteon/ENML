#include <cassert>

#include <os/keys/random_id_source.hpp>

int main() {
    os::keys::RandomKeyIdSource source;
    auto first = source.next();
    auto second = source.next();
    assert(first);
    assert(second);
    assert(first.value().valid());
    assert(second.value().valid());
    assert(first.value() != second.value());
    return 0;
}
