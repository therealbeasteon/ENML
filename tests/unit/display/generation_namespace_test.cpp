#include <cassert>
#include <cstdint>

#include <os/display/buffer.hpp>
#include <os/display/compositor.hpp>
#include <os/display/error.hpp>

namespace {

constexpr os::core::PrincipalId app_principal{0x47454E4150500001ULL, 1U};
constexpr os::core::PrincipalId shell_principal{0x47454E5348454C4CULL, 1U};
constexpr os::core::PrincipalId secure_principal{0x47454E5345435552ULL, 1U};
constexpr os::core::PeerIdentity app{
    app_principal,
    os::core::UserId{41U},
    os::core::ProcessId{4101U},
};

os::display::Compositor make_compositor(std::uint64_t generation) {
    return os::display::Compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .right = 0U, .bottom = 100U, .left = 0U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        os::display::TrustedUiPrincipals{
            .shell = shell_principal,
            .secure_ui = secure_principal,
        },
        generation,
    };
}

os::display::SurfaceDescriptor create_root(os::display::Compositor& compositor) {
    auto created = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 64U, 64U},
        .accepts_input = true,
    });
    assert(created);
    return created.value();
}

} // namespace

int main() {
    auto invalid = make_compositor(0U);
    assert(!invalid.valid());
    os::display::SharedBufferPool invalid_pool{0U};
    assert(!invalid_pool.valid());

    constexpr std::uint64_t first_generation = 7U;
    constexpr std::uint64_t second_generation = 8U;
    auto first_compositor = make_compositor(first_generation);
    auto second_compositor = make_compositor(second_generation);
    assert(first_compositor.valid() && second_compositor.valid());

    const auto first_surface = create_root(first_compositor);
    const auto second_surface = create_root(second_compositor);
    assert(first_surface.id != second_surface.id);
    assert(os::display::display_object_generation(first_surface.id.value()) == first_generation);
    assert(os::display::display_object_generation(second_surface.id.value()) == second_generation);
    assert(os::display::display_object_serial(first_surface.id.value()) == 1U);
    assert(os::display::display_object_serial(second_surface.id.value()) == 1U);

    auto stale_surface = second_compositor.lookup(first_surface.id);
    assert(!stale_surface);
    assert(stale_surface.error().domain == os::core::ErrorDomain::display);
    assert(stale_surface.error().code == os::display::errors::unknown_surface);

    os::display::SharedBufferPool first_pool{first_generation};
    os::display::SharedBufferPool second_pool{second_generation};
    auto first_buffer_result = first_pool.allocate(
        app, {64U, 64U}, os::display::PixelFormat::rgba8888);
    auto second_buffer_result = second_pool.allocate(
        app, {64U, 64U}, os::display::PixelFormat::rgba8888);
    assert(first_buffer_result && second_buffer_result);
    auto first_buffer = std::move(first_buffer_result).value();
    auto second_buffer = std::move(second_buffer_result).value();

    assert(first_buffer.descriptor.id != second_buffer.descriptor.id);
    assert(os::display::display_object_generation(first_buffer.descriptor.id.value()) == first_generation);
    assert(os::display::display_object_generation(second_buffer.descriptor.id.value()) == second_generation);
    assert(os::display::display_object_serial(first_buffer.descriptor.id.value()) == 1U);
    assert(os::display::display_object_serial(second_buffer.descriptor.id.value()) == 1U);

    auto stale_buffer = second_pool.lookup_owned(app, first_buffer.descriptor.id);
    assert(!stale_buffer);
    assert(stale_buffer.error().domain == os::core::ErrorDomain::display);
    assert(stale_buffer.error().code == os::display::errors::invalid_buffer);

    const os::display::SurfaceDescriptor legacy_surface{
        .id = os::display::SurfaceId{1U},
        .owner = app,
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 64U, 64U},
    };
    assert(!legacy_surface.valid());
    return 0;
}
