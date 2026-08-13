#include <os/supervisor/subsystem_lease.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::supervisor;

    const LeaseCount none{};
    QuiesceContext ready{};

    auto wifi = plan_idle_transition(SubsystemDomain::network, none, ready);
    require(wifi.may_quiesce);
    require(wifi.revoke_client_capabilities);
    require(wifi.revoke_dma_and_irqs);
    require(wifi.zero_ephemeral_secrets);
    require(wifi.stop_service);
    require(wifi.power_gate_hardware);

    auto leased = plan_idle_transition(
        SubsystemDomain::camera,
        LeaseCount{.interactive = 1U},
        ready);
    require(!leased.may_quiesce);

    auto wake_required = ready;
    wake_required.wake_source_required = true;
    auto network_wake = plan_idle_transition(SubsystemDomain::network, none, wake_required);
    require(network_wake.may_quiesce);
    require(network_wake.stop_service);
    require(!network_wake.power_gate_hardware);

    auto call = ready;
    call.emergency_operation_active = true;
    require(!plan_idle_transition(SubsystemDomain::telephony, none, call).may_quiesce);

    auto telephony_idle = plan_idle_transition(SubsystemDomain::telephony, none, ready);
    require(telephony_idle.may_quiesce);
    require(!telephony_idle.stop_service);
    require(!telephony_idle.power_gate_hardware);

    require(!plan_idle_transition(SubsystemDomain::storage, none, ready).may_quiesce);
    require(!plan_idle_transition(SubsystemDomain::key_service, none, ready).may_quiesce);

    auto updating = ready;
    updating.update_in_progress = true;
    require(!plan_idle_transition(SubsystemDomain::usb_data, none, updating).may_quiesce);

    return 0;
}
