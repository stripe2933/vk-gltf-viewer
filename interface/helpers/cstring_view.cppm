export module vk_gltf_viewer:helpers.cstring_view;

import std;

export template <class CharT, class Traits = std::char_traits<CharT>>
class basic_cstring_view {
    using view_type = std::basic_string_view<CharT, Traits>;

public:
    using traits_type = view_type::traits_type;
    using value_type = view_type::value_type;
    using pointer = view_type::pointer;
    using const_pointer = view_type::const_pointer;
    using reference = view_type::reference;
    using const_reference = view_type::const_reference;
    using const_iterator = view_type::const_iterator;
    using iterator = view_type::iterator;
    using const_reverse_iterator = view_type::const_reverse_iterator;
    using reverse_iterator = view_type::reverse_iterator;
    using size_type = view_type::size_type;
    using difference_type = view_type::difference_type;

    static constexpr size_type npos = view_type::npos;

    template <std::size_t N>
    constexpr basic_cstring_view(const CharT (&str)[N]) : view { str, N - 1 } { }
    constexpr basic_cstring_view(const std::string &str) noexcept : view { str } { }
    constexpr basic_cstring_view(const std::pmr::string &str) noexcept : view { str } { }
    constexpr basic_cstring_view(const basic_cstring_view&) noexcept = default;
    constexpr auto operator=(const basic_cstring_view&) noexcept -> basic_cstring_view& = default;

    // --------------------
    // Conversions.
    // --------------------

    [[nodiscard]] operator std::basic_string_view<CharT, Traits>() const noexcept { return view; }

    [[nodiscard]] auto c_str() const noexcept -> const CharT* { return view.data(); }

    // --------------------
    // Iterators.
    // --------------------

    [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator { return view.begin(); }
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return view.cbegin(); }
    [[nodiscard]] constexpr auto end() const noexcept -> const_iterator { return view.end(); }
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return view.cend(); }
    [[nodiscard]] constexpr auto rbegin() const noexcept -> const_reverse_iterator { return view.rbegin(); }
    [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator { return view.crbegin(); }
    [[nodiscard]] constexpr auto rend() const noexcept -> const_reverse_iterator { return view.rend(); }
    [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator { return view.crend(); }

    // --------------------
    // Element access.
    // --------------------

    constexpr auto operator[](size_type pos) const { return view[pos]; }
    [[nodiscard]] constexpr auto at(size_type pos) const -> const_reference { return view.at(pos); }
    [[nodiscard]] constexpr auto front() const -> const_reference { return view.front(); }
    [[nodiscard]] constexpr auto back() const -> const_reference { return view.back(); }
    [[nodiscard]] constexpr auto data() const noexcept -> const_pointer { return view.data(); }

    // --------------------
    // Capacity.
    // --------------------

    [[nodiscard]] constexpr auto size() const noexcept -> size_type { return view.size(); }
    [[nodiscard]] constexpr auto length() const noexcept -> size_type { return view.length(); }
    [[nodiscard]] constexpr auto max_size() const noexcept -> size_type { return view.max_size(); }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return view.empty(); }

    // --------------------
    // Modifiers.
    // --------------------

    [[nodiscard]] constexpr auto remove_prefix(size_type n) -> basic_cstring_view& {
        view.remove_prefix(n);
        return *this;
    }

    constexpr auto swap(basic_cstring_view &s) noexcept -> void { view.swap(s.view); }

    // --------------------
    // Operations.
    // --------------------

    auto copy(CharT *dest, size_type count, size_type pos = 0) const -> size_type;

    constexpr auto substr(size_type pos = 0, size_type count = npos) const -> basic_cstring_view;

    constexpr auto compare(std::basic_string_view<CharT, Traits> v) const noexcept -> int { return view.compare(v); }
    constexpr auto compare(size_type pos1, size_type count1, std::basic_string_view<CharT, Traits> v) const -> int { return view.compare(pos1, count1, v); }
    constexpr auto compare(size_type pos1, size_type count1, std::basic_string_view<CharT, Traits> v, size_type pos2, size_type count2) const -> int { return view.compare(pos1, count1, v, pos2, count2); }
    constexpr auto compare(const CharT *s) const -> int { return view.compare(s); }
    constexpr auto compare(size_type pos1, size_type count1, const CharT *s) const -> int { return view.compare(pos1, count1, s); }
    constexpr auto compare(size_type pos1, size_type count1, const CharT *s, size_type count2) const -> int { return view.compare(pos1, count1, s, count2); }

    constexpr auto starts_with(std::basic_string_view<CharT, Traits> sv) const noexcept { return view.starts_with(sv); }
    constexpr auto starts_with(CharT c) const noexcept { return view.starts_with(c); }
    constexpr auto starts_with(const CharT *s) const { return view.starts_with(s); }

    constexpr auto ends_with(std::basic_string_view<CharT, Traits> sv) const noexcept -> bool { return view.ends_with(sv); }
    constexpr auto ends_with(CharT c) const noexcept -> bool { return view.ends_with(c); }
    constexpr auto ends_with(const CharT *s) const -> bool { return view.ends_with(s); }

    constexpr auto contains(std::basic_string_view<CharT, Traits> sv) const noexcept -> bool { return view.find(sv) != npos; }
    constexpr auto contains(CharT c) const noexcept -> bool { return view.find(c) != npos; }
    constexpr auto contains(const CharT *s) const -> bool { return view.find(s) != npos; }

    constexpr auto find(std::basic_string_view<CharT, Traits> v, size_type pos = 0) const noexcept { return view.find(v, pos); }
    constexpr auto find(CharT c, size_type pos = 0) const noexcept { return view.find(c, pos); }
    constexpr auto find(const CharT *s, size_type pos, size_type count) const -> size_type;
    constexpr auto find(const CharT *s, size_type pos = 0) const -> size_type;

    constexpr auto rfind(std::basic_string_view<CharT, Traits> v, size_type pos = npos) const noexcept -> size_type { return view.rfind(v, pos); }
    constexpr auto rfind(CharT c, size_type pos = npos) const noexcept -> size_type { return view.rfind(c, pos); }
    constexpr auto rfind(const CharT *s, size_type pos, size_type count) const -> size_type;
    constexpr auto rfind(const CharT *s, size_type pos = npos) const -> size_type;

    constexpr auto find_first_of(std::basic_string_view<CharT, Traits> v, size_type pos = 0) const noexcept -> size_type { return view.find_first_of(v, pos); }
    constexpr auto find_first_of(CharT c, size_type pos = 0) const noexcept -> size_type { return view.find_first_of(c, pos); }
    constexpr auto find_first_of(const CharT *s, size_type pos, size_type count) const -> size_type;
    constexpr auto find_first_of(const CharT *s, size_type pos = 0) const -> size_type;

    constexpr auto find_last_of(std::basic_string_view<CharT, Traits> v, size_type pos = npos) const noexcept -> size_type { return view.find_last_of(v, pos); }
    constexpr auto find_last_of(CharT c, size_type pos = npos) const noexcept -> size_type { return view.find_last_of(c, pos); }
    constexpr auto find_last_of(const CharT *s, size_type pos, size_type count) const -> size_type;
    constexpr auto find_last_of(const CharT *s, size_type pos = npos) const -> size_type;

    constexpr auto find_first_not_of(std::basic_string_view<CharT, Traits> v, size_type pos = 0) const noexcept -> size_type { return view.find_first_not_of(v, pos); }
    constexpr auto find_first_not_of(CharT c, size_type pos = 0) const noexcept -> size_type { return view.find_first_not_of(c, pos); }
    constexpr auto find_first_not_of(const CharT *s, size_type pos, size_type count) const -> size_type;
    constexpr auto find_first_not_of(const CharT *s, size_type pos = 0) const -> size_type;

    constexpr auto find_last_not_of(std::basic_string_view<CharT, Traits> v, size_type pos = npos) const noexcept -> size_type { return view.find_last_not_of(v, pos); }
    constexpr auto find_last_not_of(CharT c, size_type pos = npos) const noexcept -> size_type { return view.find_last_not_of(c, pos); }
    constexpr auto find_last_not_of(const CharT *s, size_type pos, size_type count) const -> size_type;
    constexpr auto find_last_not_of(const CharT *s, size_type pos = npos) const -> size_type;

private:
    view_type view;
};

export using cstring_view = basic_cstring_view<char>;
export using wcstring_view = basic_cstring_view<wchar_t>;
export using u8cstring_view = basic_cstring_view<char8_t>;
export using u16cstring_view = basic_cstring_view<char16_t>;
export using u32cstring_view = basic_cstring_view<char32_t>;