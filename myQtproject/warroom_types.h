// warroom_types.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <memory>
#include <chrono>
#include <cstdint>

namespace warroom {

    // 唯一标识符 — MVP阶段用字符串，方便调试和序列化
    using Uuid = std::string;
    inline Uuid generateUuid() {
        // 简化实现，实际项目可用 QUuid
        static uint64_t counter = 0;
        return "node_" + std::to_string(++counter);
    }

    // 二维坐标
    struct Point2D {
        float x = 0.0f;
        float y = 0.0f;
    };

    // 矩形
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool intersects(const Rect& other) const {
            return !(x + width < other.x || other.x + other.width < x ||
                y + height < other.y || other.y + other.height < y);
        }

        bool contains(const Point2D& pt) const {
            return pt.x >= x && pt.x <= x + width &&
                pt.y >= y && pt.y <= y + height;
        }
    };

    // 颜色（简化为十六进制字符串）
    using Color = std::string;
    constexpr const char* kDefaultNodeColor = "#888888";
    constexpr const char* kDefaultLinkColor = "#aaaaaa";

    // 时间戳
    using Timestamp = std::chrono::system_clock::time_point;

} // namespace warroom