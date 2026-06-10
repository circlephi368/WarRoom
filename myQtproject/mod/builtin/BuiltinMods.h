// mod/builtin/BuiltinMods.h
//
// 内置模组的集中静态注册入口。
// 在主程序启动时调用一次 warroom::registerBuiltinMods() 即可。
//
#pragma once

#include "mod/ModManager.h"
#include "mod/builtin/ImageMod.h"
#include "mod/builtin/VideoMod.h"
//#include "mod/builtin/WebMod.h"

namespace warroom {

    inline void registerBuiltinMods() {
        static bool registered = false;
        if (registered) return;
        registered = true;

        ModManager::instance().registerMod(std::make_unique<ImageMod>());
        ModManager::instance().registerMod(std::make_unique<VideoMod>());
        //ModManager::instance().registerMod(std::make_unique<WebMod>());
        // 后续在这里追加其它内置 mod，例如：
        // ModManager::instance().registerMod(std::make_unique<TableMod>());
    }

} // namespace warroom
