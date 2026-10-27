#ifndef TRANSFORM_COMPONENT_H
#define TRANSFORM_COMPONENT_H
#include <joy/jmath.h>

struct TransformComponent {
	vec3_t position;
	float yaw;
	vec3_t scale;
};

#endif
