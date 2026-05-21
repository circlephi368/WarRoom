// war_zone.h
#pragma once
#include "warroom_types.h"

namespace warroom {

    struct WarZone {
        Uuid id;
        std::string name;
        Color background_color = "#ffffff00";
        Color border_color = "#cccccc";
        Rect boundary;
        std::vector<Uuid> member_ids;
        bool collapsed = false;
    };

} // namespace warroom


