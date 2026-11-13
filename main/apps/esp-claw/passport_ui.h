#pragma once

#include "passport_agent.h"
#include "passport_network.h"

bool passport_ui_init(void);
void passport_ui_update(bool connection, const passport_network_status_t *network,
                        const passport_agent_status_t *agent);
