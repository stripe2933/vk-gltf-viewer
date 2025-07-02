export module vk_gltf_viewer.helpers.algorithm;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

/**
 * @brief Get exclusive scan of the given range, and append the total summation at the end.
 *
 *  Input: [1, 3, 5, 2, 4]
 * Output: [0, 1, 4, 9, 11, 15] (15 = 1 + 3 + 5 + 2 + 4)
 *
 * @tparam R Input range type. Must be a sized range.
 * @param r Input range.
 * @return Exclusive scan result.
 */
export template <typename R>
    requires std::ranges::input_range<R>
        && std::ranges::sized_range<R>
        && requires(std::ranges::range_value_t<R> e) {
            { e + e } -> std::same_as<decltype(e)>;
        } // Must be additive.
[[nodiscard]] std::vector<std::ranges::range_value_t<R>> exclusive_scan_with_count(R &&r) {
    std::vector<std::ranges::range_value_t<R>> result;
    result.reserve(std::ranges::size(r) + 1);
    result.append_range(FWD(r));
    result.push_back(0);

    std::exclusive_scan(result.begin(), result.end(), result.begin(), 0);

    return result;
}