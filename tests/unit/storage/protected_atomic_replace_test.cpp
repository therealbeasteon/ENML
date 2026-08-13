#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/storage/protected_atomic_replace.hpp>
#include <os/storage/error.hpp>

namespace {

enum class Step : std::uint8_t {
    key = 1U,
    ciphertext = 2U,
    commit = 3U,
    publish = 4U,
    retire = 5U,
};

class RecordingBackend final : public os::storage::ProtectedPublicationBackend {
public:
    explicit RecordingBackend(std::uint8_t fail_step = 0U) noexcept : fail_step_(fail_step) {}

    os::core::Result<void>
    persist_wrapped_key(const os::storage::ProtectedObjectVersion&) noexcept override {
        return record(Step::key);
    }

    os::core::Result<void>
    persist_ciphertext(
        const os::storage::ProtectedObjectVersion&,
        os::core::ByteSpan) noexcept override {
        return record(Step::ciphertext);
    }

    os::core::Result<void>
    persist_commit_record(
        const os::storage::ProtectedObjectVersion&,
        const os::storage::ProtectedObjectVersion&) noexcept override {
        return record(Step::commit);
    }

    os::core::Result<void>
    publish_namespace(const os::storage::ProtectedObjectVersion&) noexcept override {
        return record(Step::publish);
    }

    os::core::Result<void>
    retire_generation(const os::storage::ProtectedObjectVersion&) noexcept override {
        return record(Step::retire);
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] Step at(std::size_t i) const noexcept { return steps_[i]; }

private:
    os::core::Result<void> record(Step step) noexcept {
        if (count_ < steps_.size()) steps_[count_] = step;
        ++count_;
        if (fail_step_ == static_cast<std::uint8_t>(step)) {
            return os::storage::storage_error(os::storage::errors::io_failure);
        }
        return {};
    }

    std::array<Step, 5U> steps_{};
    std::size_t count_{0U};
    std::uint8_t fail_step_{0U};
};

constexpr os::storage::ProtectedObjectVersion previous{
    .user = os::core::UserId{42U},
    .object_id = os::storage::ProtectedObjectId{0x1111U, 0x2222U},
    .generation = 7U,
};
constexpr os::storage::ProtectedObjectVersion next{
    .user = os::core::UserId{42U},
    .object_id = os::storage::ProtectedObjectId{0x1111U, 0x2222U},
    .generation = 8U,
};
constexpr std::array<std::byte, 4U> payload{
    std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
};

} // namespace

int main() {
    static_assert(previous.valid());
    static_assert(next.valid());

    RecordingBackend good;
    os::storage::ProtectedAtomicReplace replace{good};
    assert(replace.replace(previous, next, payload));
    assert(good.count() == 5U);
    assert(good.at(0U) == Step::key);
    assert(good.at(1U) == Step::ciphertext);
    assert(good.at(2U) == Step::commit);
    assert(good.at(3U) == Step::publish);
    assert(good.at(4U) == Step::retire);

    for (std::uint8_t fail = 1U; fail <= 5U; ++fail) {
        RecordingBackend backend{fail};
        os::storage::ProtectedAtomicReplace attempt{backend};
        assert(!attempt.replace(previous, next, payload));
        assert(backend.count() == fail);
        // A failure before retirement proves retire was not called. A failure at
        // retirement proves every durable publication step completed first.
        if (fail < static_cast<std::uint8_t>(Step::retire)) {
            for (std::size_t i = 0U; i < backend.count(); ++i) {
                assert(backend.at(i) != Step::retire);
            }
        }
    }

    auto stale = next;
    stale.generation = previous.generation;
    RecordingBackend invalid_backend;
    os::storage::ProtectedAtomicReplace invalid{invalid_backend};
    assert(!invalid.replace(previous, stale, payload));
    assert(invalid_backend.count() == 0U);

    auto wrong_object = next;
    wrong_object.object_id = os::storage::ProtectedObjectId{0x3333U, 0x4444U};
    assert(!invalid.replace(previous, wrong_object, payload));
    assert(invalid_backend.count() == 0U);

    return 0;
}
