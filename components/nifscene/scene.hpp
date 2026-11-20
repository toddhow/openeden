#ifndef OPENEDEN_COMPONENTS_NIFSCENE_SCENE_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_SCENE_HPP

// Nif::Scene: the content of a NIF file as plain arrays (task 0082, docs/design/game-packs.md section 3).
// No renderer, physics or NIF record type appears here. A consumer (the renderer, the body merger, a converter) reads
// this and decides what to keep: the scene keeps every influence the file has.
//
// Conventions, all stated by tests (apps/components_tests/nifscene):
// - Units, axes and winding are the file's. Nothing is converted to metres or flipped here.
// - Texture coordinates are as stored (V is not flipped).
// - A transform's rotation is nine floats in the order the file stores them (row-major, the NIF convention).
// - Tangents are glTF-style: xyz is the unit tangent and w is the sign such that the bitangent is
//   cross(normal, xyz) * w. The NIF stores two vectors it calls tangent and bitangent; the NIF "bitangent" is the
//   glTF tangent (research note 0044, item 5).
// - Triangle indices are 32-bit, three per triangle, into the geometry's own vertex arrays.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "havok.hpp"

namespace NifScene
{
    using Vec2 = std::array<float, 2>;
    using Vec3 = std::array<float, 3>;
    using Vec4 = std::array<float, 4>;

    constexpr std::uint32_t sNone = 0xFFFFFFFFu;

    struct Transform
    {
        Vec3 mTranslation{ 0.f, 0.f, 0.f };
        std::array<float, 9> mRotation{ 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };
        float mScale = 1.f;
    };

    /// Task 0359: the keys of a BSEffectShaderPropertyFloatController on one of the UV variables of an effect shader
    /// (U offset, V offset, U scale, V scale), with the controller's own clock. Interpolation codes are the NIF's: 1
    /// linear, 2 quadratic (Hermite with the stored tangents), 3 TCB (the reader turns it into tangents too), 5
    /// constant. A key's tangents are the file's per-second slopes.
    struct FloatKey
    {
        float mTime = 0.f;
        float mValue = 0.f;
        float mInTan = 0.f;
        float mOutTan = 0.f;
    };

    struct FloatTrack
    {
        std::vector<FloatKey> mKeys; ///< sorted by time; empty: the controller has no usable keys
        std::uint32_t mInterpolation = 1;
        float mFrequency = 1.f;
        float mPhase = 0.f;
        float mStart = 0.f; ///< the controller's time window (start/stop)
        float mStop = 0.f;
        /// NiTimeController extrapolation: 0 cycle (loop), 1 reverse (ping-pong), 2 constant (clamp).
        std::uint32_t mCycle = 0;
        bool empty() const { return mKeys.empty(); }
    };

    /// What the controllers of one effect shader do to its texture coordinates: uv' = uv * scale(t) + offset(t), each
    /// of the four components from its own track, else from the property's stored value. Empty (the default) for a
    /// shader no controller touches.
    struct UvAnimation
    {
        std::array<FloatTrack, 2> mOffset; ///< U then V
        std::array<FloatTrack, 2> mScale;
        Vec2 mBaseOffset{ 0.f, 0.f }; ///< the property's own UV offset and scale
        Vec2 mBaseScale{ 1.f, 1.f };
        bool any() const
        {
            return !mOffset[0].empty() || !mOffset[1].empty() || !mScale[0].empty() || !mScale[1].empty();
        }
    };

    /// A node of the scene graph: every NiAVObject of the file, in record order. A shape is a node with a geometry.
    struct Node
    {
        std::string mName;
        std::string mRecordType;
        std::uint32_t mRecordIndex = 0;
        std::uint32_t mParent = sNone; // index into Scene::mNodes
        std::uint32_t mFlags = 0;
        Transform mTransform;
        std::uint32_t mGeometry = sNone; // index into Scene::mGeometries
    };

    struct Influence
    {
        std::uint32_t mBone = 0; // index into the geometry's skin's bones
        float mWeight = 0.f;
    };

    /// One skin partition: a part of the triangle list that uses a subset of the bones.
    struct Partition
    {
        std::vector<std::uint16_t> mBones; // the partition's own bone list, as stored
        std::uint32_t mFirstTriangle = 0; // into Geometry::mTriangles, counted in triangles
        std::uint32_t mTriangleCount = 0;
        std::uint8_t mLodLevel = 0;
        bool mGlobalVB = false;
    };

    struct Skin
    {
        std::vector<std::string> mBoneNames;
        std::vector<std::uint32_t> mBoneNodes; // index into Scene::mNodes, or sNone
        Transform mSkinTransform; // the skin data's own transform, identity when the file has none
        std::vector<Transform> mBindTransforms; // per bone, as stored by the file
        std::vector<Partition> mPartitions;
        /// Partitions whose bone list is not 0, 1, 2, ... (research note 0036: the packed vertex indices are skin
        /// bone indices all the same, so this is a count and not an error).
        std::uint32_t mNonIdentityPartitionBoneMaps = 0;
        /// Influences (weight above zero) of a vertex of a partition whose bone is not in the partition's list.
        std::uint32_t mInfluencesOutsidePartitionBones = 0;
    };

    /// A BSShaderTextureSet: the texture paths by slot, as stored. Skyrim: 0 diffuse, 1 normal, 2 glow or skin, 3
    /// height, 4 environment, 5 environment mask, 6 subsurface or inner layer, 7 specular or back light (the file may
    /// store 8 or 9). Fallout 4 stores 9 (the last one smoothness/spec). Paths are not normalised.
    struct TextureSet
    {
        std::vector<std::string> mTextures;
    };

    enum class ShaderKind : std::uint8_t
    {
        Lighting, // BSLightingShaderProperty
        Effect, // BSEffectShaderProperty
    };

    /// BSLightingShaderProperty and BSEffectShaderProperty as plain values. Only the fields the file stores for its
    /// stream version (and, for a lighting shader, its shader type) are filled; every other field keeps its default,
    /// so the same file always gives the same values. Fallout 4 material files (.bgsm, .bgem) are named, never read:
    /// mName holds the path the shader property is called, mRootMaterial the "root material" string.
    struct ShaderMaterial
    {
        ShaderKind mKind = ShaderKind::Lighting;
        std::string mRecordType;
        std::string mName; // Fallout 4: the .bgsm or .bgem path when the property is named after one
        std::string mRootMaterial; // Fallout 4 lighting shaders only
        std::uint32_t mShaderType
            = 0; // BSLightingShaderType (lighting shaders only); 0 default, 1 env map, 5 skin tint, ...
        std::uint32_t mFlags1 = 0;
        std::uint32_t mFlags2 = 0;
        Vec2 mUVOffset{ 0.f, 0.f };
        Vec2 mUVScale{ 1.f, 1.f };
        std::uint32_t mTextureSet = sNone; // index into Scene::mTextureSets (lighting shaders)
        /// Texture clamp mode as stored: 0 clamp/clamp, 1 clamp S wrap T, 2 wrap S clamp T, 3 wrap/wrap.
        std::uint32_t mClampMode = 3;
        float mAlpha = 1.f;

        // Lighting shader, common to Skyrim and Fallout 4
        Vec3 mEmissiveColor{ 0.f, 0.f, 0.f };
        float mEmissiveMultiple = 1.f;
        float mRefractionStrength = 0.f;
        Vec3 mSpecularColor{ 0.f, 0.f, 0.f };
        float mSpecularStrength = 1.f;
        // Skyrim (stream < 130)
        float mGlossiness = 0.f;
        Vec2 mLightingEffects{ 0.f, 0.f };
        // Fallout 4 (stream 130 to 139)
        float mSmoothness = 0.f;
        float mSubsurfaceRolloff = 0.f;
        float mRimlightPower = 0.f;
        float mBacklightPower = 0.f; // only stored when the rim light power is the float maximum
        float mGrayscaleToPaletteScale = 1.f;
        float mFresnelPower = 5.f;
        float mWetnessSpecScale = 0.f;
        float mWetnessSpecPower = 0.f;
        float mWetnessMinVar = 0.f;
        float mWetnessEnvMapScale = 0.f; // stream 130 only
        float mWetnessFresnelPower = 0.f;
        float mWetnessMetalness = 0.f;

        // The tail by shader type
        float mEnvMapScale = 0.f; // type 1 (env map), Skyrim
        bool mUseSSR = false; // type 1, Fallout 4
        bool mWetnessUseSSR = false; // type 1, Fallout 4
        Vec3 mSkinTintColor{ 0.f, 0.f, 0.f }; // type 5
        float mSkinTintAlpha = 1.f; // type 5, Fallout 4
        Vec3 mHairTintColor{ 0.f, 0.f, 0.f }; // type 6
        float mParallaxMaxPasses = 0.f; // type 7
        float mParallaxScale = 0.f; // type 7
        float mInnerLayerThickness = 0.f; // type 11
        float mInnerRefractionScale = 0.f; // type 11
        Vec2 mInnerLayerTextureScale{ 0.f, 0.f }; // type 11
        float mInnerEnvMapScale = 0.f; // type 11
        Vec4 mSparkle{ 0.f, 0.f, 0.f, 0.f }; // type 14
        float mEyeCubeMapScale = 0.f; // type 16
        Vec3 mLeftEyeReflectionCenter{ 0.f, 0.f, 0.f }; // type 16
        Vec3 mRightEyeReflectionCenter{ 0.f, 0.f, 0.f }; // type 16

        // Effect shader
        std::string mSourceTexture;
        std::string mGreyscaleTexture;
        std::string mEnvMapTexture; // Fallout 4
        std::string mNormalTexture; // Fallout 4
        std::string mEnvMaskTexture; // Fallout 4
        std::uint8_t mLightingInfluence = 0;
        std::uint8_t mEnvMapMinLod = 0;
        Vec4 mFalloff{ 0.f, 0.f, 0.f, 0.f }; // start angle, stop angle, start opacity, stop opacity
        Vec4 mBaseColor{ 0.f, 0.f, 0.f, 0.f };
        float mBaseColorScale = 0.f;
        float mSoftFalloffDepth = 0.f;
        float mEffectEnvMapScale = 0.f; // Fallout 4
        /// Task 0359: the shader's UV controllers (effect shaders only; empty when none).
        UvAnimation mUvAnimation;
    };

    /// A NiAlphaProperty. The blend factors are the property's own codes: 0 one, 1 zero, 2 source colour, 3 inverse
    /// source colour, 4 destination colour, 5 inverse destination colour, 6 source alpha, 7 inverse source alpha,
    /// 8 destination alpha, 9 inverse destination alpha, 10 source alpha saturate. Test modes: 0 always, 1 less,
    /// 2 equal, 3 less or equal, 4 greater, 5 not equal, 6 greater or equal, 7 never.
    struct AlphaProperty
    {
        std::uint16_t mFlags = 0;
        std::uint8_t mThreshold = 0;
        bool mBlend = false;
        bool mTest = false;
        bool mNoSorter = false;
        std::uint8_t mSourceBlend = 0;
        std::uint8_t mDestinationBlend = 0;
        std::uint8_t mTestMode = 0;
    };

    enum class GeometryKind : std::uint8_t
    {
        TriShape, // BSTriShape and its plain subclasses
        DynamicTriShape, // BSDynamicTriShape: positions from the dynamic array
        SubIndexTriShape, // BSSubIndexTriShape
    };

    struct Geometry
    {
        std::string mName;
        GeometryKind mKind = GeometryKind::TriShape;
        std::uint32_t mNode = sNone; // index into Scene::mNodes
        std::vector<Vec3> mPositions;
        std::vector<Vec3> mNormals; // empty, or one per vertex
        std::vector<Vec4> mTangents; // empty, or one per vertex: glTF-style
        std::vector<Vec2> mUV0; // empty, or one per vertex
        std::vector<Vec4> mColors; // empty, or one per vertex: RGBA in [0, 1]
        std::vector<std::uint32_t> mTriangles; // three indices per triangle
        /// Influences of vertex v are mInfluences[mInfluenceStart[v] .. mInfluenceStart[v + 1]); both are empty when
        /// the shape is not skinned. Zero weights (the packed format's padding) are not stored.
        std::vector<std::uint32_t> mInfluenceStart;
        std::vector<Influence> mInfluences;
        std::uint32_t mSkin = sNone; // index into Scene::mSkins
        std::uint32_t mMaterial = sNone; // index into Scene::mMaterials, or none
        std::uint32_t mAlpha = sNone; // index into Scene::mAlphaProperties, or none
        /// The shape stores no vertex data of its own (Data Size 0): the vertices came from the skin partition.
        bool mVerticesFromSkinPartition = false;
        /// There was no vertex data anywhere: the arrays are empty.
        bool mNoVertexData = false;
    };

    /// Task 0232: one key of a transform track. Rotations are quaternions as x, y, z, w.
    struct KeyVec3
    {
        float mTime = 0.f;
        Vec3 mValue{ 0.f, 0.f, 0.f };
        Vec3 mInTan{ 0.f, 0.f, 0.f };
        Vec3 mOutTan{ 0.f, 0.f, 0.f };
    };
    struct KeyQuat
    {
        float mTime = 0.f;
        Vec4 mValue{ 0.f, 0.f, 0.f, 1.f };
    };
    struct KeyFloat
    {
        float mTime = 0.f;
        float mValue = 1.f;
        float mInTan = 0.f;
        float mOutTan = 0.f;
    };

    /// What a NiControllerSequence does to one named node (a ControlledBlock with a NiTransformInterpolator).
    /// Interpolation codes are the NIF's (1 linear, 2 quadratic/Bezier, 3 TCB (tangents already generated), 5
    /// constant).
    struct Track
    {
        std::string mNodeName;
        bool mHasDefault = false; // the interpolator's own value, used for a channel with no keys
        Vec3 mDefaultTranslation{ 0.f, 0.f, 0.f };
        Vec4 mDefaultRotation{ 0.f, 0.f, 0.f, 1.f };
        float mDefaultScale = 1.f;
        std::uint32_t mTranslationInterpolation = 1;
        std::vector<KeyVec3> mTranslations;
        std::vector<KeyQuat> mRotations;
        /// Euler rotation keys (the file stores rotation as three angle tracks, radians; Skyrim doors do). Used only
        /// when mRotations is empty. Order of the turns: about X first, then Y, then Z (R = Rz * Ry * Rx).
        std::array<std::vector<KeyFloat>, 3> mEuler;
        std::array<std::uint32_t, 3> mEulerInterpolation{ 1, 1, 1 };
        std::uint32_t mScaleInterpolation = 1;
        std::vector<KeyFloat> mScales;
    };

    /// A NiControllerSequence (Skyrim door and chest models name them "Open" and "Close").
    struct Sequence
    {
        std::string mName;
        float mStart = 0.f;
        float mStop = 0.f;
        float mFrequency = 1.f;
        std::uint32_t mCycleType = 2; // NiTimeController extrapolation: 0 loop, 1 reverse, 2 clamp
        std::vector<Track> mTracks;
        std::uint32_t mUnsupportedBlocks = 0; // controlled blocks that were not a plain transform interpolator
    };

    /// Task 0359 stage b: one NiParticleSystem (a Skyrim SE flame, smoke or spark system) as plain numbers. Only what a
    /// static model's analytic drawing needs is kept (docs/research/0148-skyrim-fire-effects.md): the emitter, the birth
    /// rate from the system's NiPSysEmitterCtlr, and the colour, scale, sub-texture, gravity, drag and rotation
    /// modifiers. BSStripParticleSystem, NiMeshParticleSystem and systems driven by a master system are not read
    /// (counted in Scene::mWarnings).
    struct ParticleSystem
    {
        enum class Emitter : std::uint8_t
        {
            Point, // mesh emitters, and any emitter this reader does not know: every particle starts at the emitter node
            Box,
            Cylinder,
            Sphere,
        };

        std::string mName;
        std::uint32_t mNode = sNone; // the system's own node
        std::uint32_t mEmitterNode = sNone; // the emitter object's node (position, orientation); sNone: the system's own
        std::uint32_t mMaterial = sNone; // index into Scene::mMaterials (a BSEffectShaderProperty), or none
        std::uint32_t mAlpha = sNone; // index into Scene::mAlphaProperties, or none
        bool mWorldSpace = true;
        std::uint32_t mMaxParticles = 0; // the system's data vertex count
        float mBirthRate = 0.f; // particles a second (the emitter controller's value, the mean of its keys)

        Emitter mEmitter = Emitter::Point;
        Vec3 mEmitterSize{ 0.f, 0.f, 0.f }; // box width, height, depth; cylinder radius, height; sphere radius
        float mSpeed = 0.f, mSpeedVariation = 0.f;
        float mDeclination = 0.f, mDeclinationVariation = 0.f; // radians from the emitter's Z axis
        float mPlanarAngle = 0.f, mPlanarAngleVariation = 0.f; // radians about it
        float mRadius = 1.f, mRadiusVariation = 0.f; // a particle's half size at birth
        float mLife = 1.f, mLifeVariation = 0.f; // seconds
        Vec4 mInitialColor{ 1.f, 1.f, 1.f, 1.f };

        std::vector<float> mScales; // BSPSysScaleModifier: the size factor over a life, evenly spaced; empty: 1

        bool mHasColor = false; // BSPSysSimpleColorModifier
        float mFadeIn = 0.f, mFadeOut = 0.f; // shares of the life over which alpha rises and falls
        Vec4 mColorStops{ 0.f, 0.5f, 0.5f, 1.f }; // ascending: colour 1 until [0], blends to colour 2 by [1], holds to [2], blends to colour 3 by [3]
        std::array<Vec4, 3> mColors{ Vec4{ 1.f, 1.f, 1.f, 1.f }, Vec4{ 1.f, 1.f, 1.f, 1.f }, Vec4{ 1.f, 1.f, 1.f, 1.f } };

        bool mHasSubTexture = false; // BSPSysSubTexModifier (a flipbook atlas)
        std::vector<Vec4> mSubTextures; // the data's rectangles as stored: left, width, top, height (UV)
        float mSubStart = 0.f, mSubStartFudge = 0.f, mSubEnd = 0.f, mSubLoopStart = 0.f, mSubCount = 0.f,
              mSubCountFudge = 0.f;
        float mAspectRatio = 1.f; // the data's width over height

        Vec3 mGravity{ 0.f, 0.f, 0.f }; // the sum of the planar gravity modifiers (axis times strength), units a second squared
        bool mGravityWorldAligned = true; // false: the axis is in the system's own frame
        float mDrag = 0.f; // the sum of the drag modifiers' percentages

        float mRotationSpeed = 0.f, mRotationSpeedVariation = 0.f; // radians a second
        float mRotationAngle = 0.f, mRotationAngleVariation = 0.f; // radians at birth
        bool mRandomRotationSign = false;
    };

    struct Scene
    {
        std::uint32_t mVersion = 0;
        std::uint32_t mUserVersion = 0;
        std::uint32_t mBethVersion = 0;
        std::vector<Node> mNodes;
        std::vector<Geometry> mGeometries;
        std::vector<Skin> mSkins;
        std::vector<TextureSet> mTextureSets;
        std::vector<ShaderMaterial> mMaterials;
        std::vector<AlphaProperty> mAlphaProperties;
        std::vector<std::uint32_t> mRoots; // indices into mNodes
        /// Task 0232: the controller sequences of the file's NiControllerManager(s), in file order.
        std::vector<Sequence> mSequences;
        /// Task 0359 stage b: the NiParticleSystems of the file (Skyrim SE only), in file order.
        std::vector<ParticleSystem> mParticleSystems;
        /// The bhk collision data (task 0144), only filled when SceneOptions::mReadHavok is set.
        Havok::Collision mHavok;
        /// Things a consumer may want to log: a vertex layout that disagrees with its declared size, partition bone
        /// lists that are not the identity. Deterministic, in the order found.
        std::vector<std::string> mWarnings;
    };

    /// A text form of a scene with every value, for tests and comparisons: the same scene gives the same text.
    std::string dump(const Scene& scene);
}

#endif
