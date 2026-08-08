#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/encoder.hpp>
int main(){using namespace os; std::array<std::byte,64> storage{}; ipc::Encoder encoder{storage}; assert(encoder.write_u16_le(0x1234U)); assert(encoder.write_u32_le(0x89ABCDEFU)); assert(encoder.write_u64_le(0x0102030405060708ULL)); assert(encoder.write_bool(true)); assert(encoder.write_utf8("hello",16)); constexpr std::array<std::uint8_t,20> expected{0x34,0x12,0xEF,0xCD,0xAB,0x89,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x01,0x05,0x00,0x00,0x00,static_cast<std::uint8_t>('h')}; const auto written=encoder.written(); assert(written.size()==24U); for(std::size_t i=0;i<expected.size();++i) assert(std::to_integer<std::uint8_t>(written[i])==expected[i]); assert(std::to_integer<char>(written[20])=='e'); assert(std::to_integer<char>(written[21])=='l'); assert(std::to_integer<char>(written[22])=='l'); assert(std::to_integer<char>(written[23])=='o'); std::array<std::byte,1> tiny{}; ipc::Encoder too_small{tiny}; const auto too_small_result=too_small.write_u32_le(1U); assert(!too_small_result); assert(too_small_result.error()==core::make_error(core::ErrorDomain::ipc,ipc::errors::buffer_too_small)); const std::array<std::byte,2> invalid_utf8{std::byte{0xC0},std::byte{0xAF}}; std::string_view invalid{reinterpret_cast<const char*>(invalid_utf8.data()),invalid_utf8.size()}; ipc::Encoder utf8_encoder{storage}; const auto utf8_result=utf8_encoder.write_utf8(invalid,16U); assert(!utf8_result); assert(utf8_result.error()==core::make_error(core::ErrorDomain::ipc,ipc::errors::invalid_utf8)); return 0;}
