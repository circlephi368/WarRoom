// scout_action.h
#pragma once
#include "warroom_types.h"

namespace warroom {

	enum class ScoutResult {
		Success,
		PartialSuccess,
		Failure
	};

	struct ScoutAction {
		Uuid id;
		Timestamp timestamp;
		Uuid related_node_id;
		std::string hypothesis;
		std::string action_desc;
		ScoutResult result = ScoutResult::Success;
		std::string failure_reason;
	};

} // namespace warroom