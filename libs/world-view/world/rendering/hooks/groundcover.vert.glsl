// groundcover.vert.glsl - the groundcover deform, a hook in salal's base shader (salal's
// docs/shader-hooks.md; the engine assembles it in through salal_base_shader()).
//
// A clump rests compact, scaled by `openness` so top-down it reads small, and within
// `cursorRadius` of `cursorWorld` it opens toward full size and bends away, like
// groundcover parting as a colonist walks through. Off unless `enabled` is 1, so trees
// and every other instanced draw pass through untouched.
//
// The block is GroundcoverUniforms in world/rendering/GroundcoverUniforms.h, member for
// member; change the two together.

layout(std140) uniform GroundcoverUniforms {
	vec2  cursorWorld;	  //  0 interaction centre, world space
	int   enabled;		  //  8 1 applies the deform
	float openness;		  // 12 resting clump scale 0..1 (1 = full size)
	float reach;		  // 16 max local vertex distance from a clump's base (meters)
	float cursorRadius;	  // 20 interaction radius, world space
	float cursorStrength; // 24 push distance at a clump's outer tips, world space
} groundcover;

// Cursor influence on the clump at `instancePosition`: 0 far, 1 at the centre.
float groundcover_influence(vec2 instancePosition) {
	float dC = distance(instancePosition, groundcover.cursorWorld);
	return 1.0 - smoothstep(0.0, max(groundcover.cursorRadius, 0.0001), dC);
}

// Compact at rest, full size where the cursor reveals it.
vec2 groundcover_instanceLocal(vec2 position, vec2 instancePosition) {
	if (groundcover.enabled != 1) {
		return position;
	}
	float openness = mix(groundcover.openness, 1.0, groundcover_influence(instancePosition));
	return position * openness;
}

// Bend away from the cursor: outer tips move most, the rooted base not at all.
vec2 groundcover_instanceWorld(vec2 position, vec2 localPosition, vec2 instancePosition) {
	float infl = groundcover.enabled == 1 ? groundcover_influence(instancePosition) : 0.0;
	if (infl <= 0.0) {
		return position;
	}
	float reachFrac = clamp(length(localPosition) / max(groundcover.reach, 0.0001), 0.0, 1.0); // 0 at the base, 1 at the tips
	vec2  away = position - groundcover.cursorWorld;
	float d = length(away);
	vec2  pushDir = d > 0.0001 ? away / d : vec2(0.0, -1.0);
	return position + pushDir * (infl * reachFrac * groundcover.cursorStrength);
}
