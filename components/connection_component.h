#ifndef CONNECTION_COMPONENT_H
#define CONNECTION_COMPONENT_H
#include <cstdint>

struct ConnectionComponent {
	int conv = 0;
	int health = 10;
	uint64_t last_heartbeat = 0;
	uint32_t last_processed_seq = 0;
	bool is_loaded = false;
	int frameid = 0;
};

#endif