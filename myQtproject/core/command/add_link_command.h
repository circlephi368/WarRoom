// add_link_command.h
#pragma once
#include "command.h"
#include "core/warroom/war_link.h"

namespace warroom {

    class AddLinkCommand : public Command {
    public:
        explicit AddLinkCommand(WarLink link);

        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

        // 获取添加后的连线 ID
        const Uuid& getLinkId() const { return linkId_; }

    private:
        Uuid linkId_;
        WarLink link_;
        bool executed_ = false;
    };

} // namespace warroom