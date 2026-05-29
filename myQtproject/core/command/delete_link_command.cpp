#include "delete_link_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    DeleteLinkCommand::DeleteLinkCommand(const Uuid& linkId,
        const Uuid& startNodeId, const Uuid& endNodeId,
        LinkType type, const std::string& label, const Color& color)
        : linkId_(linkId)
        , startNodeId_(startNodeId)
        , endNodeId_(endNodeId)
        , type_(type)
        , label_(label)
        , color_(color)
    {}

    void DeleteLinkCommand::execute(WarRoomModel& model) {
        if (executed_) return;
        model.removeLink(linkId_);
        executed_ = true;
    }

    void DeleteLinkCommand::undo(WarRoomModel& model) {
        WarLink link = WarLink::makeNodeToNode(startNodeId_, -1, endNodeId_, -1, type_);
        link.id = linkId_;
        link.label = label_;
        link.color = color_;
        model.addLink(std::move(link));
    }

    std::string DeleteLinkCommand::description() const {
        return "删除连线: " + label_;
    }

} // namespace warroom