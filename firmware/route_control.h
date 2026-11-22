#ifndef RPICO_BRIDGE_ROUTE_CONTROL_H
#define RPICO_BRIDGE_ROUTE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "bridge.h"

void route_control_init(bridge_t *bridge);
/* Accept one complete bounded ASCII command without blocking. */
void route_control_feed_line(const char *line, uint8_t length);
/* Drop incomplete transactions when a CDC session ends or changes. */
void route_control_reset_session(void);
void route_control_start_session(void);
/* Pass complete non-machine lines to the embedded shell. */
void route_control_feed_human_line(const char *line, uint8_t length);
/* Send at most bounded response work through CDC, retrying under backpressure. */
void route_control_task(void);

#endif
