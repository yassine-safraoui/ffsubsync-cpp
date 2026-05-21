#include "ffsubsync/aligner.h"
#include <cmath>
#include <algorithm>

namespace ffsubsync {

std::pair<double, double> golden_section_search(
    std::function<double(double, bool)> objective,
    double lower_bound, double upper_bound, double tolerance) {

    // Golden ratio constants
    static const double inverse_phi   = (std::sqrt(5.0) - 1.0) / 2.0;  // 1 / phi ≈ 0.618
    static const double inverse_phi_2 = (3.0 - std::sqrt(5.0)) / 2.0;  // 1 / phi² ≈ 0.382

    lower_bound = std::min(lower_bound, upper_bound);
    upper_bound = std::max(lower_bound, upper_bound);
    double interval_size = upper_bound - lower_bound;

    if (interval_size <= tolerance) {
        return {lower_bound, upper_bound};
    }

    // Compute how many iterations are needed to shrink the interval to <= tolerance
    int num_iterations = static_cast<int>(
        std::ceil(std::log(tolerance / interval_size) / std::log(inverse_phi)));

    auto evaluate = [&](double x, bool is_last_iteration) -> double {
        try {
            return objective(x, is_last_iteration);
        } catch (...) {
            // If the 2-argument version isn't supported, fall back to 1-arg
            return objective(x, false);
        }
    };

    // Initial interior points
    double left_interior  = lower_bound + inverse_phi_2 * interval_size;
    double right_interior = lower_bound + inverse_phi   * interval_size;
    double left_value  = evaluate(left_interior,  num_iterations == 1);
    double right_value = evaluate(right_interior, num_iterations == 1);

    for (int iteration = 0; iteration < num_iterations - 1; ++iteration) {
        bool is_last_iteration = (iteration == num_iterations - 2);

        if (left_value < right_value) {
            // Minimum lies in [lower_bound, right_interior]
            upper_bound   = right_interior;
            right_interior = left_interior;
            right_value   = left_value;
            interval_size *= inverse_phi;
            left_interior  = lower_bound + inverse_phi_2 * interval_size;
            left_value     = evaluate(left_interior, is_last_iteration);
        } else {
            // Minimum lies in [left_interior, upper_bound]
            lower_bound    = left_interior;
            left_interior  = right_interior;
            left_value     = right_value;
            interval_size *= inverse_phi;
            right_interior = lower_bound + inverse_phi * interval_size;
            right_value    = evaluate(right_interior, is_last_iteration);
        }
    }

    if (left_value < right_value) {
        return {lower_bound, right_interior};
    } else {
        return {left_interior, upper_bound};
    }
}

} // namespace ffsubsync
