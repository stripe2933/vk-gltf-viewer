export module imgui.math;

export import imgui;

export [[nodiscard]] ImVec2 operator*(const ImVec2 &lhs, const float rhs) { return ImVec2(lhs.x * rhs, lhs.y * rhs); }
export [[nodiscard]] ImVec2 operator/(const ImVec2 &lhs, const float rhs) { return ImVec2(lhs.x / rhs, lhs.y / rhs); }

export [[nodiscard]] ImVec2 operator+(const ImVec2 &lhs, const ImVec2 &rhs) {
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

export [[nodiscard]] ImVec2 operator-(const ImVec2 &lhs, const ImVec2 &rhs) {
    return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

export [[nodiscard]] ImVec2 operator*(const ImVec2 &lhs, const ImVec2 &rhs) {
    return ImVec2(lhs.x * rhs.x, lhs.y * rhs.y);
}

export [[nodiscard]] ImVec2 operator/(const ImVec2 &lhs, const ImVec2 &rhs) {
    return ImVec2(lhs.x / rhs.x, lhs.y / rhs.y);
}

export [[nodiscard]] ImVec2 operator-(const ImVec2 &lhs) { return ImVec2(-lhs.x, -lhs.y); }

export ImVec2 &operator*=(ImVec2 &lhs, const float rhs) {
    lhs.x *= rhs;
    lhs.y *= rhs;
    return lhs;
}

export ImVec2 &operator/=(ImVec2 &lhs, const float rhs) {
    lhs.x /= rhs;
    lhs.y /= rhs;
    return lhs;
}

export ImVec2 &operator+=(ImVec2 &lhs, const ImVec2 &rhs) {
    lhs.x += rhs.x;
    lhs.y += rhs.y;
    return lhs;
}

export ImVec2 &operator-=(ImVec2 &lhs, const ImVec2 &rhs) {
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;
    return lhs;
}

export ImVec2 &operator*=(ImVec2 &lhs, const ImVec2 &rhs) {
    lhs.x *= rhs.x;
    lhs.y *= rhs.y;
    return lhs;
}

export ImVec2 &operator/=(ImVec2 &lhs, const ImVec2 &rhs) {
    lhs.x /= rhs.x;
    lhs.y /= rhs.y;
    return lhs;
}

export bool operator==(const ImVec2 &lhs, const ImVec2 &rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
export bool operator!=(const ImVec2 &lhs, const ImVec2 &rhs) { return lhs.x != rhs.x || lhs.y != rhs.y; }

export [[nodiscard]] ImVec4 operator+(const ImVec4 &lhs, const ImVec4 &rhs) {
    return ImVec4(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
}

export [[nodiscard]] ImVec4 operator-(const ImVec4 &lhs, const ImVec4 &rhs) {
    return ImVec4(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
}

export [[nodiscard]] ImVec4 operator*(const ImVec4 &lhs, const ImVec4 &rhs) {
    return ImVec4(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w);
}

export [[nodiscard]] bool operator==(const ImVec4 &lhs, const ImVec4 &rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

export [[nodiscard]] bool operator!=(const ImVec4 &lhs, const ImVec4 &rhs) {
    return lhs.x != rhs.x || lhs.y != rhs.y || lhs.z != rhs.z || lhs.w != rhs.w;
}
