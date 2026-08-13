#include <budget/budget.hpp>

#include <budget/measure.hpp>

namespace budget {

os::core::Result<ResourceBudget> find_budget(std::string_view name) noexcept {
    for (const auto& entry : service_budgets) {
        if (entry.name == name) {
            return entry;
        }
    }
    return os::core::make_error(
        os::core::ErrorDomain::core, errors::unknown_budget);
}

} // namespace budget
