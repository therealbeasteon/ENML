#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <os/core/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>
namespace { void error_envelope_round_trips(){std::array<std::byte,8> storage{};os::ipc::Encoder encoder{storage};assert(encoder.write_u16_le(static_cast<std::uint16_t>(os::core::ErrorDomain::service)));assert(encoder.write_u16_le(0U));assert(encoder.write_u32_le(os::core::errors::service::unknown_operation));os::core::Error decoded{};auto result=os::ipc::decode_rpc_error(encoder.written(),decoded);assert(result);assert(decoded.domain==os::core::ErrorDomain::service);assert(decoded.code==os::core::errors::service::unknown_operation);} void invalid_error_envelopes_are_rejected(){ {std::array<std::byte,8> s{};os::ipc::Encoder e{s};assert(e.write_u16_le(99U));assert(e.write_u16_le(0U));assert(e.write_u32_le(1U));os::core::Error d{};auto r=os::ipc::decode_rpc_error(e.written(),d);assert(!r);assert(r.error().code==os::ipc::errors::protocol_violation);} {std::array<std::byte,8> s{};os::ipc::Encoder e{s};assert(e.write_u16_le(static_cast<std::uint16_t>(os::core::ErrorDomain::service)));assert(e.write_u16_le(1U));assert(e.write_u32_le(1U));os::core::Error d{};auto r=os::ipc::decode_rpc_error(e.written(),d);assert(!r);assert(r.error().code==os::ipc::errors::protocol_violation);} {std::array<std::byte,8> s{};os::ipc::Encoder e{s};assert(e.write_u16_le(static_cast<std::uint16_t>(os::core::ErrorDomain::service)));assert(e.write_u16_le(0U));assert(e.write_u32_le(0U));os::core::Error d{};auto r=os::ipc::decode_rpc_error(e.written(),d);assert(!r);assert(r.error().code==os::ipc::errors::protocol_violation);} } }
int main(){error_envelope_round_trips();invalid_error_envelopes_are_rejected();return 0;}
