#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <os/core/error.hpp>
#include <os/core/span.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
namespace { template <std::size_t N> os::core::ByteSpan bytes(const std::array<std::uint8_t,N>& input){return {reinterpret_cast<const std::byte*>(input.data()),input.size()};} }
int main(){using namespace os; const std::array<std::uint8_t,19> encoded{0x34,0x12,0xEF,0xCD,0xAB,0x89,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x01,0x00,0x00,0x00,0x00}; ipc::Decoder decoder{bytes(encoded)}; assert(decoder.read_u16_le().value()==0x1234U); assert(decoder.read_u32_le().value()==0x89ABCDEFU); assert(decoder.read_u64_le().value()==0x0102030405060708ULL); assert(decoder.read_bool().value()); assert(decoder.read_bytes(8U).value().empty()); assert(decoder.require_end()); const std::array<std::uint8_t,1> invalid_bool{2U}; ipc::Decoder bool_decoder{bytes(invalid_bool)}; const auto bool_result=bool_decoder.read_bool(); assert(!bool_result); assert(bool_result.error()==core::make_error(core::ErrorDomain::ipc,ipc::errors::malformed_message)); const std::array<std::uint8_t,3> truncated{0x01,0x02,0x03}; ipc::Decoder truncated_decoder{bytes(truncated)}; assert(!truncated_decoder.read_u32_le()); const std::array<std::uint8_t,6> invalid_utf8{0x02,0x00,0x00,0x00,0xED,0xA0}; ipc::Decoder utf8_decoder{bytes(invalid_utf8)}; assert(!utf8_decoder.read_utf8(8U)); const std::array<std::uint8_t,5> trailing{0x00,0x00,0x00,0x00,0xAA}; ipc::Decoder trailing_decoder{bytes(trailing)}; assert(trailing_decoder.read_bytes(4U)); assert(!trailing_decoder.require_end()); return 0;}
