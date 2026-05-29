#include "edit_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    EditNodeCommand::EditNodeCommand(Uuid nodeId,
        std::string oldTitle, std::string newTitle,
        std::string oldFullText, std::string newFullText)
        : nodeId_(std::move(nodeId))
        , oldTitle_(std::move(oldTitle))
        , newTitle_(std::move(newTitle))
        , oldFullText_(std::move(oldFullText))
        , newFullText_(std::move(newFullText))
    {}

    void EditNodeCommand::execute(WarRoomModel& model) {
        WarNode* node = model.getNodeMutable(nodeId_);
        if (node) {
            node->title = newTitle_;
            node->full_text = newFullText_;
        }
    }

    void EditNodeCommand::undo(WarRoomModel& model) {
        WarNode* node = model.getNodeMutable(nodeId_);
        if (node) {
            node->title = oldTitle_;
            node->full_text = oldFullText_;
        }
    }

    std::string EditNodeCommand::description() const {
        return "编辑节点: " + oldTitle_ + " → " + newTitle_;
    }

} // namespace warroom