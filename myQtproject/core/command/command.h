#pragma once
#include <string>
#include <chrono>
#include <memory>

namespace warroom {

    class WarRoomModel;

    class Command {
    public:
        virtual ~Command() = default;
        virtual void execute(WarRoomModel& model) = 0;
        virtual void undo(WarRoomModel& model) = 0;
        virtual std::string description() const = 0;

        std::chrono::system_clock::time_point timestamp() const { return timestamp_; }

    protected:
        std::chrono::system_clock::time_point timestamp_ =
            std::chrono::system_clock::now();
    };

} // namespace warroom