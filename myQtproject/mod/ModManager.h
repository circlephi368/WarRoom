// mod/ModManager.h
//
// 节点模组管理器（单例）。
//
// 设计要点：
//   1. WarNode 只持有可序列化的"模组类型 + json 数据"，运行时的 void* 私有
//      指针完全由 ModManager 维护，避免污染 WarNode 的拷贝 / 移动语义。
//   2. 节点没有绑定任何模组时，所有 API 都是 no-op，原版纯文本节点行为
//      不受任何影响。
//   3. 当前阶段优先静态注册（registerMod），动态库加载机制保留但默认不
//      使用。
//
#pragma once

#include "NodeMod.h"
#include "core/warroom/war_node.h"

#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QJsonDocument>
#include <QJsonObject>

namespace warroom {

	class ModManager {
	public:
		static ModManager& instance() {
			static ModManager manager;
			return manager;
		}

		// 禁止拷贝
		ModManager(const ModManager&) = delete;
		ModManager& operator=(const ModManager&) = delete;

		// ========== 模组注册 ==========
		void registerMod(std::unique_ptr<NodeMod> mod) {
			if (!mod) return;
			std::lock_guard<std::mutex> lock(m_mutex);
			ModInfo info = mod->getInfo();
			m_modInfo[info.id] = info;
			m_mods[info.id] = std::move(mod);
		}

		void registerMod(NodeMod* mod) {
			registerMod(std::unique_ptr<NodeMod>(mod));
		}

		// ========== 模组查询 ==========
		NodeMod* getMod(const std::string& id) {
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_mods.find(id);
			return it != m_mods.end() ? it->second.get() : nullptr;
		}

		const ModInfo* getModInfo(const std::string& id) {
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_modInfo.find(id);
			return it != m_modInfo.end() ? &it->second : nullptr;
		}

		std::unordered_map<std::string, ModInfo> getAllModInfo() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_modInfo;
		}

		std::vector<std::string> getPrimaryMods() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			std::vector<std::string> result;
			for (const auto& kv : m_mods) {
				if (kv.second && kv.second->isPrimary()) result.push_back(kv.first);
			}
			return result;
		}

		std::vector<std::string> getAuxiliaryMods() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			std::vector<std::string> result;
			for (const auto& kv : m_mods) {
				if (kv.second && !kv.second->isPrimary()) result.push_back(kv.first);
			}
			return result;
		}

		// ========== 节点级运行时数据访问 ==========
		// 取节点的某个模组的私有 void* 数据（不存在时返回 nullptr）
		void* getNodePrivate(const WarNode* node, const std::string& modId) const {
			if (!node) return nullptr;
			std::lock_guard<std::mutex> lock(m_mutex);
			auto nit = m_nodePrivates.find(node->id);
			if (nit == m_nodePrivates.end()) return nullptr;
			auto mit = nit->second.find(modId);
			return mit == nit->second.end() ? nullptr : mit->second;
		}

		void* getPrimaryPrivate(const WarNode* node) const {
			if (!node || node->primary_mod_type.empty()) return nullptr;
			return getNodePrivate(node, node->primary_mod_type);
		}

		// ========== 节点生命周期钩子 ==========
		// 节点创建/加载完成后调用：根据 WarNode 中保存的模组类型与数据，
		// 实例化每个模组的私有数据。
		void initNodeModData(WarNode* node, NodeGraphicsItem* item) {
			if (!node) return;

			// 主模组
			if (!node->primary_mod_type.empty()) {
				if (NodeMod* mod = getMod(node->primary_mod_type)) {
					void* data = mod->onCreateNode(node, item);
					if (!node->primary_mod_data.is_null() && data) {
						mod->deserialize(data, node->primary_mod_data);
					}
					setNodePrivate(node->id, node->primary_mod_type, data);
					mod->onNodeLoaded(node, data);
				}
			}

			// 辅助模组
			for (const auto& modType : node->auxiliary_mod_types) {
				if (NodeMod* mod = getMod(modType)) {
					void* data = mod->onCreateNode(node, item);
					auto it = node->auxiliary_mod_data.find(modType);
					if (it != node->auxiliary_mod_data.end() && data) {
						mod->deserialize(data, it->second);
					}
					setNodePrivate(node->id, modType, data);
					mod->onNodeLoaded(node, data);
				}
			}
		}

		// 在保存到磁盘前调用：把模组的运行时数据回写到 WarNode 的 json 字段
		void saveNodeModData(WarNode* node) {
			if (!node) return;

			if (!node->primary_mod_type.empty()) {
				if (NodeMod* mod = getMod(node->primary_mod_type)) {
					void* data = getNodePrivate(node, node->primary_mod_type);
					if (data) {
						mod->onNodeSaved(node, data);
						node->primary_mod_data = mod->serialize(data);
					}
				}
			}

			for (const auto& modType : node->auxiliary_mod_types) {
				if (NodeMod* mod = getMod(modType)) {
					void* data = getNodePrivate(node, modType);
					if (data) {
						mod->onNodeSaved(node, data);
						node->auxiliary_mod_data[modType] = mod->serialize(data);
					}
				}
			}
		}

		// 节点销毁前调用：清理所有模组私有数据
		void cleanupNodeModData(WarNode* node) {
			if (!node) return;
			cleanupNodeModData(node->id);
		}

		void cleanupNodeModData(const std::string& nodeId) {
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_nodePrivates.find(nodeId);
			if (it == m_nodePrivates.end()) return;

			for (auto& kv : it->second) {
				auto mit = m_mods.find(kv.first);
				if (mit != m_mods.end() && mit->second && kv.second) {
					mit->second->onDestroyNode(kv.second);
				}
			}
			m_nodePrivates.erase(it);
		}

		// ========== 推荐尺寸 ==========
		QSizeF getModPreferredSize(const WarNode* node) const {
			if (!node) return QSizeF(160, 60);

			if (!node->primary_mod_type.empty()) {
				NodeMod* mod = const_cast<ModManager*>(this)->getMod(node->primary_mod_type);
				void* data = getPrimaryPrivate(node);
				if (mod && data) {
					return mod->getPreferredSize(node, data);
				}
			}
			QSizeF size(160, 60);
			for (const auto& modType : node->auxiliary_mod_types) {
				NodeMod* mod = const_cast<ModManager*>(this)->getMod(modType);
				void* data = getNodePrivate(node, modType);
				if (mod && data) {
					size = size.expandedTo(mod->getPreferredSize(node, data));
				}
			}
			return size;
		}

		// ========== 模组加载（动态库） ==========
		bool loadModFromLibrary(const QString& path) {
			auto library = std::make_unique<QLibrary>(path);
			if (!library->load()) return false;

			typedef NodeMod* (*CreateModFunc)();
			CreateModFunc createMod = (CreateModFunc)library->resolve("createMod");
			if (!createMod) {
				library->unload();
				return false;
			}

			NodeMod* mod = createMod();
			if (!mod) {
				library->unload();
				return false;
			}

			ModInfo info = mod->getInfo();
			registerMod(std::unique_ptr<NodeMod>(mod));

			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_loadedLibraries[info.id] = std::move(library);
			}
			return true;
		}

		void loadModsFromDirectory(const QString& modPath) {
			QDir dir(modPath);
			if (!dir.exists()) {
				dir.mkpath(".");
				return;
			}

			for (const QString& modDir : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
				QString manifestPath = modPath + "/" + modDir + "/manifest.json";
				if (QFile::exists(manifestPath)) {
					loadModFromManifest(manifestPath);
				}
			}

			QStringList nameFilters;
#ifdef Q_OS_WIN
			nameFilters << "*.dll";
#elif defined(Q_OS_MAC)
			nameFilters << "*.dylib" << "*.so";
#else
			nameFilters << "*.so";
#endif

			for (const QString& file : dir.entryList(nameFilters, QDir::Files)) {
				loadModFromLibrary(dir.absoluteFilePath(file));
			}
		}

		bool loadModFromManifest(const QString& manifestPath) {
			QFile file(manifestPath);
			if (!file.open(QIODevice::ReadOnly)) return false;

			QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
			if (doc.isNull()) return false;

			QJsonObject obj = doc.object();
			QString id = obj["id"].toString();
			QString entry = obj["entry"].toString();
			if (id.isEmpty() || entry.isEmpty()) return false;

			QString basePath = QFileInfo(manifestPath).absolutePath();
			QString libPath = basePath + "/" + entry;
			return loadModFromLibrary(libPath);
		}

	private:
		ModManager() = default;
		~ModManager() {
			// 析构所有节点私有数据（应用退出时兜底）
			for (auto& np : m_nodePrivates) {
				for (auto& kv : np.second) {
					auto mit = m_mods.find(kv.first);
					if (mit != m_mods.end() && mit->second && kv.second) {
						mit->second->onDestroyNode(kv.second);
					}
				}
			}
		}

		void setNodePrivate(const std::string& nodeId,
			const std::string& modId, void* ptr) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_nodePrivates[nodeId][modId] = ptr;
		}

		std::unordered_map<std::string, std::unique_ptr<NodeMod>> m_mods;
		std::unordered_map<std::string, ModInfo> m_modInfo;

		// 使用 unique_ptr<QLibrary> 避免 QLibrary 的拷贝问题
		std::unordered_map<std::string, std::unique_ptr<QLibrary>> m_loadedLibraries;

		// nodeId -> (modId -> void*)
		std::unordered_map<std::string,
			std::unordered_map<std::string, void*>> m_nodePrivates;

		mutable std::mutex m_mutex;
	};

} // namespace warroom