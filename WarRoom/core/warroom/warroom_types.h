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
#include <random>
#include <sstream>
#include <iomanip>

namespace warroom {

	// 使用真正的 UUID v4 格式
	using Uuid = std::string;

	inline Uuid generateUuid() {
		static std::random_device rd;
		static std::mt19937_64 gen(rd());
		static std::uniform_int_distribution<uint64_t> dis;

		uint64_t a = dis(gen);
		uint64_t b = dis(gen);

		// 设置版本号 (4) 和变体位
		a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x4000;  // version 4
		b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;  // variant

		std::stringstream ss;
		ss << std::hex << std::setfill('0');
		ss << std::setw(16) << a << std::setw(16) << b;

		std::string uuid = ss.str();
		// 插入连字符：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
		return uuid.substr(0, 8) + "-" +
			uuid.substr(8, 4) + "-" +
			uuid.substr(12, 4) + "-" +
			uuid.substr(16, 4) + "-" +
			uuid.substr(20, 12);
	}

	// 从字符串解析 UUID（验证格式）
	inline bool isValidUuid(const std::string& uuid) {
		if (uuid.length() != 36) return false;
		for (size_t i = 0; i < 36; ++i) {
			if (i == 8 || i == 13 || i == 18 || i == 23) {
				if (uuid[i] != '-') return false;
			}
			else {
				if (!std::isxdigit(uuid[i])) return false;
			}
		}
		return true;
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
	// 注：避免使用接近中灰(128)的值——Windows 鼠标光标/caret 颜色为背景反色，
	// 中灰反色后仍为灰且亮度接近，导致光标几乎与背景融为一体。
	constexpr const char* kDefaultNodeColor = "#FF606060";
	constexpr const char* kDefaultLinkColor = "#FFaaaaaa";

	// 时间戳
	using Timestamp = std::chrono::system_clock::time_point;

} // namespace warroom