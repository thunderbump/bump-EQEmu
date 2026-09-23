#pragma once

class Client;

namespace ZoneBotAidedTrackingRuntime {

void RunBotAidedTracking(
	Client *client,
	const char *command_name,
	const char *requested_scope
);

}
