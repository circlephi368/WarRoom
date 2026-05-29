#pragma once
#include "command.h"
#include "core/warroom/war_link.h"

namespace warroom {

    class DeleteLinkCommand : public Command {
    public:
        // 保存必要数据而非整个 WarLink
        DeleteLinkCommand(const Uuid& linkId,
            const Uuid& startNodeId, const Uuid& endNodeId,
            LinkType type, const std::string& label, const Color& color);

        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

    private:
        Uuid linkId_;
        Uuid startNodeId_;
        Uuid endNodeId_;
        LinkType type_;
        std::string label_;
        Color color_;
        bool executed_ = false;
    };

} // namespace warroom