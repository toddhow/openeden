#ifdef VERTEX
const int VERTEX_LOCATION=0;
layout(location=VERTEX_LOCATION) in vec3 inVertex;
#else
const int VERTEX_LOCATION=-1;
#endif

#ifdef NORMAL
const int NORMAL_LOCATION=VERTEX_LOCATION+1;
layout(location=NORMAL_LOCATION) in vec3 inNormal;
#else
const int NORMAL_LOCATION=VERTEX_LOCATION;
#endif

#ifdef COLOR
const int COLOR_LOCATION=NORMAL_LOCATION+1;
layout(location=COLOR_LOCATION) in vec4 inColor;
#else
const int COLOR_LOCATION=NORMAL_LOCATION;
#endif

#ifdef SKIN
const int BONE_INDICES_LOCATION=COLOR_LOCATION+1;
layout(location=BONE_INDICES_LOCATION) in vec4 inBoneIndices;
const int BONE_WEIGHTS_LOCATION=BONE_INDICES_LOCATION+1;
layout(location=BONE_WEIGHTS_LOCATION) in vec4 inBoneWeights;
layout(std430, set=TEXTURE_SET, binding=BONE_BINDING) readonly buffer BoneMatrices {
    mat4 boneMatrices[];
};
#else
const int BONE_WEIGHTS_LOCATION=COLOR_LOCATION;
#endif

// OpenEden: the tangent (xyz) and handedness (w) for normal maps (task 0012, V4h).
#ifdef NORMAL_MAP
const int TANGENT_LOCATION=BONE_WEIGHTS_LOCATION+1;
layout(location=TANGENT_LOCATION) in vec4 inTangent;
#else
const int TANGENT_LOCATION=BONE_WEIGHTS_LOCATION;
#endif

// OpenEden: per-instance offset (xyz) and scale (w), and rotation (Euler angles), for groundcover (task 0012).
#ifdef INSTANCE
const int INSTANCE_OFFSET_LOCATION=TANGENT_LOCATION+1;
layout(location=INSTANCE_OFFSET_LOCATION) in vec4 inInstanceOffset;
const int INSTANCE_ROTATION_LOCATION=INSTANCE_OFFSET_LOCATION+1;
layout(location=INSTANCE_ROTATION_LOCATION) in vec3 inInstanceRotation;
// OpenEden (task 0153 follow-up 2): a third per-instance input, a colour (rgb) and a sway speed (w); with it the
// rotation input holds (tilt x, tilt y, yaw). Used only by the neutral grass.
 #ifdef INSTANCE_TINT
const int INSTANCE_TINT_LOCATION=INSTANCE_ROTATION_LOCATION+1;
layout(location=INSTANCE_TINT_LOCATION) in vec4 inInstanceTint;
 #else
const int INSTANCE_TINT_LOCATION=INSTANCE_ROTATION_LOCATION;
 #endif
#else
const int INSTANCE_ROTATION_LOCATION=TANGENT_LOCATION;
const int INSTANCE_TINT_LOCATION=INSTANCE_ROTATION_LOCATION;
#endif

#ifdef TEXCOORD
const int TEXCOORD_LOCATION=INSTANCE_TINT_LOCATION+1;
layout(location=TEXCOORD_LOCATION) in vec2 inTexCoord[numUvSets];
#endif
