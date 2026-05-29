// add_link_command.cpp
#include "add_link_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    AddLinkCommand::AddLinkCommand(WarLink link)
        : link_(std::move(link))
        , linkId_(link_.id)
    {}

    void AddLinkCommand::execute(WarRoomModel& model) {
        if (executed_) return;
        linkId_ = model.addLink(std::move(link_));
        executed_ = true;
    }

    void AddLinkCommand::undo(WarRoomModel& model) {
        model.removeLink(linkId_);
    }

    std::string AddLinkCommand::description() const {
        return "添加连线: " + link_.label;
    }

} // namespace warroom