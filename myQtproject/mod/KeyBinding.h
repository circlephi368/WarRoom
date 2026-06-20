#pragma once

#include <string>
#include <vector>
#include <map>
#include <QKeySequence>
#include <QSettings>

namespace warroom {

struct KeyBinding {
	std::string id;
	std::string name;
	std::string category;
	std::string defaultKey;
	std::string currentKey;

	KeyBinding() = default;
	KeyBinding(std::string id_, std::string name_, std::string category_, std::string defaultKey_)
		: id(std::move(id_)), name(std::move(name_)), category(std::move(category_)),
		  defaultKey(std::move(defaultKey_)), currentKey(defaultKey_) {}

	bool isModified() const { return currentKey != defaultKey; }
	void resetToDefault() { currentKey = defaultKey; }

	QKeySequence toQKeySequence() const {
		return QKeySequence::fromString(QString::fromStdString(currentKey));
	}
};

class KeyBindingRegistry {
public:
	static KeyBindingRegistry& instance() {
		static KeyBindingRegistry instance;
		return instance;
	}

	void registerBinding(const KeyBinding& binding) {
		if (m_bindings.find(binding.id) == m_bindings.end()) {
			m_bindings[binding.id] = binding;
		}
	}

	void registerBindings(const std::vector<KeyBinding>& bindings) {
		for (const auto& b : bindings) {
			registerBinding(b);
		}
	}

	bool hasBinding(const std::string& id) const {
		return m_bindings.find(id) != m_bindings.end();
	}

	std::string getKey(const std::string& id) const {
		auto it = m_bindings.find(id);
		return it != m_bindings.end() ? it->second.currentKey : "";
	}

	QKeySequence getQKeySequence(const std::string& id) const {
		auto it = m_bindings.find(id);
		if (it == m_bindings.end()) return QKeySequence();
		return it->second.toQKeySequence();
	}

	void setKey(const std::string& id, const std::string& keySeq) {
		auto it = m_bindings.find(id);
		if (it != m_bindings.end()) {
			it->second.currentKey = keySeq;
		}
	}

	void resetToDefault(const std::string& id) {
		auto it = m_bindings.find(id);
		if (it != m_bindings.end()) {
			it->second.resetToDefault();
		}
	}

	void resetAllToDefault() {
		for (auto& [id, binding] : m_bindings) {
			binding.resetToDefault();
		}
	}

	std::vector<KeyBinding> getAllBindings() const {
		std::vector<KeyBinding> result;
		for (const auto& [id, binding] : m_bindings) {
			result.push_back(binding);
		}
		return result;
	}

	std::vector<KeyBinding> getBindingsByCategory(const std::string& category) const {
		std::vector<KeyBinding> result;
		for (const auto& [id, binding] : m_bindings) {
			if (binding.category == category) {
				result.push_back(binding);
			}
		}
		return result;
	}

	std::vector<std::string> getAllCategories() const {
		std::vector<std::string> categories;
		std::map<std::string, bool> seen;
		for (const auto& [id, binding] : m_bindings) {
			if (!seen[binding.category]) {
				categories.push_back(binding.category);
				seen[binding.category] = true;
			}
		}
		return categories;
	}

	void loadFromSettings(QSettings& settings) {
		settings.beginGroup("KeyBindings");
		for (auto& [id, binding] : m_bindings) {
			QString saved = settings.value(QString::fromStdString(id)).toString();
			if (!saved.isEmpty()) {
				binding.currentKey = saved.toStdString();
			}
		}
		settings.endGroup();
	}

	void saveToSettings(QSettings& settings) {
		settings.beginGroup("KeyBindings");
		for (const auto& [id, binding] : m_bindings) {
			settings.setValue(QString::fromStdString(id), QString::fromStdString(binding.currentKey));
		}
		settings.endGroup();
	}

private:
	KeyBindingRegistry() = default;
	~KeyBindingRegistry() = default;
	KeyBindingRegistry(const KeyBindingRegistry&) = delete;
	KeyBindingRegistry& operator=(const KeyBindingRegistry&) = delete;

	std::map<std::string, KeyBinding> m_bindings;
};

} // namespace warroom
