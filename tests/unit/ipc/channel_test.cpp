#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace {

[[nodiscard]] os::core::ByteSpan bytes_of(const char* text, std::size_t size) {
    return {reinterpret_cast<const std::byte*>(text), size};
}

[[nodiscard]] os::ipc::WireHeaderV1 request_header(std::uint32_t payload_size, std::uint16_t handle_count = 0) {
    std::uint32_t flags = os::ipc::flag_value(os::ipc::WireFlag::request);
    if (handle_count > 0) flags |= os::ipc::flag_value(os::ipc::WireFlag::has_handles);
    return {.flags=flags,.service_id=os::core::ServiceId{0x0000F001U},.operation_id=1U,.request_id=os::core::RequestId{7U},.payload_size=payload_size,.handle_count=handle_count,.payload_checksum=0U};
}

void round_trip_payload() {
    auto pair_result=os::ipc::Channel::create_local_pair(); assert(pair_result); auto pair=std::move(pair_result).value();
    constexpr char payload[]="hello"; const auto header=request_header(5U); assert(pair[0].send(header,bytes_of(payload,5U)));
    std::array<std::byte,os::ipc::max_wire_packet_size> receive_buffer{}; auto receive_result=pair[1].receive(receive_buffer); assert(receive_result); auto message=std::move(receive_result).value();
    assert(message.header()==header); assert(message.payload().size()==5U); assert(std::memcmp(message.payload().data(),payload,5U)==0); assert(message.handle_count()==0U);
    assert(message.sender_credentials().process_id==static_cast<std::int64_t>(::getpid())); assert(message.sender_credentials().user_id==static_cast<std::uint32_t>(::getuid())); assert(message.sender_credentials().group_id==static_cast<std::uint32_t>(::getgid()));
}

void descriptor_transfer() {
    auto pair_result=os::ipc::Channel::create_local_pair(); assert(pair_result); auto pair=std::move(pair_result).value();
    int pipe_descriptors[2]{-1,-1}; assert(::pipe2(pipe_descriptors,O_CLOEXEC)==0); os::core::NativeHandle read_end{pipe_descriptors[0]}; os::core::NativeHandle write_end{pipe_descriptors[1]};
    const auto header=request_header(0U,1U); const std::span<const os::core::NativeHandle> handles{&read_end,1U}; assert(pair[0].send(header,{},handles));
    std::array<std::byte,os::ipc::max_wire_packet_size> receive_buffer{}; auto receive_result=pair[1].receive(receive_buffer); assert(receive_result); auto message=std::move(receive_result).value(); assert(message.handle_count()==1U); assert(message.handles()[0].valid());
    const int flags=::fcntl(message.handles()[0].native(),F_GETFD); assert(flags>=0); assert((flags&FD_CLOEXEC)!=0); constexpr char marker='X'; assert(::write(write_end.native(),&marker,1U)==1); char received='\0'; assert(::read(message.handles()[0].native(),&received,1U)==1); assert(received==marker);
}

void send_rejects_shape_mismatch(){auto pair_result=os::ipc::Channel::create_local_pair(); assert(pair_result); auto pair=std::move(pair_result).value(); const auto bad_payload_header=request_header(9U); auto result=pair[0].send(bad_payload_header,{}); assert(!result); assert(result.error().domain==os::core::ErrorDomain::ipc); assert(result.error().code==os::ipc::errors::packet_size_mismatch);}
void small_receive_buffer_is_rejected_before_read(){auto pair_result=os::ipc::Channel::create_local_pair(); assert(pair_result); auto pair=std::move(pair_result).value(); std::array<std::byte,128> too_small{}; auto result=pair[1].receive(too_small); assert(!result); assert(result.error().code==os::ipc::errors::buffer_too_small);}
void peer_credentials_are_kernel_supplied(){auto pair_result=os::ipc::Channel::create_local_pair(); assert(pair_result); auto pair=std::move(pair_result).value(); auto credentials_result=pair[0].peer_credentials(); assert(credentials_result); const auto credentials=credentials_result.value(); assert(credentials.process_id==static_cast<std::int64_t>(::getpid())); assert(credentials.user_id==static_cast<std::uint32_t>(::getuid())); assert(credentials.group_id==static_cast<std::uint32_t>(::getgid()));}
} // namespace
int main(){round_trip_payload();descriptor_transfer();send_rejects_shape_mismatch();small_receive_buffer_is_rejected_before_read();peer_credentials_are_kernel_supplied();return 0;}
