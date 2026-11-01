#include "ragdollrigdata.hpp"

// The two files below are copies of CreativeMotion's data/rigs/humanoid.rig.json and
// data/profiles/humanoid-default.profile.json, taken from CreativeMotion commit 00751a6a7edb. They are embedded so that
// the debug ragdoll (task 0035, stage b) needs no data file installed. When CreativeMotion changes them, refresh
// these copies by hand.

namespace MWPhysics
{
    std::string_view humanoidRigJson()
    {
        return R"JSON(
{
  "format": "opas.rig",
  "version": 2,
  "name": "humanoid",
  "bones": [
    {"name": "Pelvis", "parent": "", "translationM": [0, 0, 0.95], "rotation": [0, 0, 0, 1]},
    {"name": "Spine", "parent": "Pelvis", "translationM": [0, 0, 0.12], "rotation": [0, 0, 0, 1]},
    {"name": "Chest", "parent": "Spine", "translationM": [0, 0, 0.2], "rotation": [0, 0, 0, 1]},
    {"name": "Neck", "parent": "Chest", "translationM": [0, 0, 0.22], "rotation": [0, 0, 0, 1]},
    {"name": "Head", "parent": "Neck", "translationM": [0, 0, 0.07], "rotation": [0, 0, 0, 1]},
    {"name": "LeftUpperArm", "parent": "Chest", "translationM": [-0.21, 0, 0.17], "rotation": [0, 0, 0, 1]},
    {"name": "LeftForearm", "parent": "LeftUpperArm", "translationM": [0, 0, -0.29], "rotation": [0, 0, 0, 1]},
    {"name": "LeftHand", "parent": "LeftForearm", "translationM": [0, 0, -0.26], "rotation": [0, 0, 0, 1]},
    {"name": "LeftFingers", "parent": "LeftHand", "translationM": [0, 0, -0.1], "rotation": [0, 0, 0, 1]},
    {"name": "RightUpperArm", "parent": "Chest", "translationM": [0.21, 0, 0.17], "rotation": [0, 0, 0, 1]},
    {"name": "RightForearm", "parent": "RightUpperArm", "translationM": [0, 0, -0.29], "rotation": [0, 0, 0, 1]},
    {"name": "RightHand", "parent": "RightForearm", "translationM": [0, 0, -0.26], "rotation": [0, 0, 0, 1]},
    {"name": "RightFingers", "parent": "RightHand", "translationM": [0, 0, -0.1], "rotation": [0, 0, 0, 1]},
    {"name": "LeftThigh", "parent": "Pelvis", "translationM": [-0.09, 0, -0.01], "rotation": [0, 0, 0, 1]},
    {"name": "LeftShin", "parent": "LeftThigh", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]},
    {"name": "LeftFoot", "parent": "LeftShin", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]},
    {"name": "RightThigh", "parent": "Pelvis", "translationM": [0.09, 0, -0.01], "rotation": [0, 0, 0, 1]},
    {"name": "RightShin", "parent": "RightThigh", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]},
    {"name": "RightFoot", "parent": "RightShin", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]}
  ],
  "bodies": [
    {
      "bone": "Pelvis",
      "shape": {"type": "capsule", "halfHeightM": 0.05, "radiusM": 0.11},
      "translationM": [0, 0, 0.04],
      "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
      "massKg": 8.3775
    },
    {
      "bone": "Spine",
      "shape": {"type": "capsule", "halfHeightM": 0.04, "radiusM": 0.11},
      "translationM": [0, 0, 0.1],
      "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
      "massKg": 12.2475
    },
    {
      "bone": "Chest",
      "shape": {"type": "capsule", "halfHeightM": 0.04, "radiusM": 0.12},
      "translationM": [0, 0, 0.11],
      "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
      "massKg": 11.97
    },
    {
      "bone": "Head",
      "shape": {"type": "capsule", "halfHeightM": 0.02, "radiusM": 0.095},
      "translationM": [0, 0, 0.08],
      "rotation": [0, 0, 0, 1],
      "massKg": 5.205
    },
    {
      "bone": "LeftUpperArm",
      "shape": {"type": "capsule", "halfHeightM": 0.1, "radiusM": 0.045},
      "translationM": [0, 0, -0.145],
      "rotation": [0, 0, 0, 1],
      "massKg": 2.0325
    },
    {
      "bone": "LeftForearm",
      "shape": {"type": "capsule", "halfHeightM": 0.09, "radiusM": 0.04},
      "translationM": [0, 0, -0.13],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.215
    },
    {
      "bone": "LeftHand",
      "shape": {"type": "box", "halfExtentsM": [0.02, 0.045, 0.09]},
      "translationM": [0, 0, -0.08],
      "rotation": [0, 0, 0, 1],
      "massKg": 0.4575
    },
    {
      "bone": "RightUpperArm",
      "shape": {"type": "capsule", "halfHeightM": 0.1, "radiusM": 0.045},
      "translationM": [0, 0, -0.145],
      "rotation": [0, 0, 0, 1],
      "massKg": 2.0325
    },
    {
      "bone": "RightForearm",
      "shape": {"type": "capsule", "halfHeightM": 0.09, "radiusM": 0.04},
      "translationM": [0, 0, -0.13],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.215
    },
    {
      "bone": "RightHand",
      "shape": {"type": "box", "halfExtentsM": [0.02, 0.045, 0.09]},
      "translationM": [0, 0, -0.08],
      "rotation": [0, 0, 0, 1],
      "massKg": 0.4575
    },
    {
      "bone": "LeftThigh",
      "shape": {"type": "capsule", "halfHeightM": 0.145, "radiusM": 0.07},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 10.62
    },
    {
      "bone": "LeftShin",
      "shape": {"type": "capsule", "halfHeightM": 0.165, "radiusM": 0.05},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 3.2475
    },
    {
      "bone": "LeftFoot",
      "shape": {"type": "box", "halfExtentsM": [0.045, 0.12, 0.035]},
      "translationM": [0, 0.06, -0.045],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.0275
    },
    {
      "bone": "RightThigh",
      "shape": {"type": "capsule", "halfHeightM": 0.145, "radiusM": 0.07},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 10.62
    },
    {
      "bone": "RightShin",
      "shape": {"type": "capsule", "halfHeightM": 0.165, "radiusM": 0.05},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 3.2475
    },
    {
      "bone": "RightFoot",
      "shape": {"type": "box", "halfExtentsM": [0.045, 0.12, 0.035]},
      "translationM": [0, 0.06, -0.045],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.0275
    }
  ],
  "joints": [
    {
      "bone": "Spine",
      "twistAxis": [0, 0, 1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.305,
      "planeSwingMaxRad": 0.305,
      "normalSwingMinRad": -0.698,
      "normalSwingMaxRad": 0.218,
      "twistMinRad": -0.39,
      "twistMaxRad": 0.39
    },
    {
      "bone": "Chest",
      "twistAxis": [0, 0, 1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.305,
      "planeSwingMaxRad": 0.305,
      "normalSwingMinRad": -0.698,
      "normalSwingMaxRad": 0.218,
      "twistMinRad": -0.39,
      "twistMaxRad": 0.39
    },
    {
      "bone": "Head",
      "twistAxis": [0, 0, 1],
      "planeAxis": [1, 0, 0],
      "swing": "cone",
      "normalHalfConeRad": 0.785,
      "planeHalfConeRad": 0.785,
      "twistMinRad": -1.047,
      "twistMaxRad": 1.047
    },
    {
      "bone": "LeftUpperArm",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.524,
      "planeSwingMaxRad": 2.967,
      "normalSwingMinRad": -1.047,
      "normalSwingMaxRad": 2.967,
      "twistMinRad": -1.571,
      "twistMaxRad": 1.222
    },
    {
      "bone": "LeftForearm",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": 0,
      "twistMaxRad": 2.618
    },
    {
      "bone": "LeftHand",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.524,
      "planeSwingMaxRad": 0.349,
      "normalSwingMinRad": -1.222,
      "normalSwingMaxRad": 1.396,
      "twistMinRad": -1.396,
      "twistMaxRad": 1.396
    },
    {
      "bone": "RightUpperArm",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -2.967,
      "planeSwingMaxRad": 0.524,
      "normalSwingMinRad": -1.047,
      "normalSwingMaxRad": 2.967,
      "twistMinRad": -1.222,
      "twistMaxRad": 1.571
    },
    {
      "bone": "RightForearm",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": 0,
      "twistMaxRad": 2.618
    },
    {
      "bone": "RightHand",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.349,
      "planeSwingMaxRad": 0.524,
      "normalSwingMinRad": -1.222,
      "normalSwingMaxRad": 1.396,
      "twistMinRad": -1.396,
      "twistMaxRad": 1.396
    },
    {
      "bone": "LeftThigh",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.524,
      "planeSwingMaxRad": 0.785,
      "normalSwingMinRad": -0.524,
      "normalSwingMaxRad": 2.094,
      "twistMinRad": -0.785,
      "twistMaxRad": 0.785
    },
    {
      "bone": "LeftShin",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": -2.356,
      "twistMaxRad": 0
    },
    {
      "bone": "LeftFoot",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.611,
      "planeSwingMaxRad": 0.262,
      "normalSwingMinRad": -0.873,
      "normalSwingMaxRad": 0.349,
      "twistMinRad": -0.175,
      "twistMaxRad": 0.175
    },
    {
      "bone": "RightThigh",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.785,
      "planeSwingMaxRad": 0.524,
      "normalSwingMinRad": -0.524,
      "normalSwingMaxRad": 2.094,
      "twistMinRad": -0.785,
      "twistMaxRad": 0.785
    },
    {
      "bone": "RightShin",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": -2.356,
      "twistMaxRad": 0
    },
    {
      "bone": "RightFoot",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.262,
      "planeSwingMaxRad": 0.611,
      "normalSwingMinRad": -0.873,
      "normalSwingMaxRad": 0.349,
      "twistMinRad": -0.175,
      "twistMaxRad": 0.175
    }
  ]
}
)JSON";
    }

    std::string_view humanoidProfileJson()
    {
        return R"JSON(
{
  "format": "opas.profile",
  "version": 2,
  "rig": "humanoid",
  "defaults": {
    "motor": {"stiffnessNmPerRad": 450, "dampingNmsPerRad": 30, "maxTorqueNm": 500},
    "material": {"friction": 0.8, "restitution": 0, "linearDamping": 0.05, "angularDamping": 0.05}
  },
  "joints": [
    {"bone": "Spine", "motor": {"stiffnessNmPerRad": 900, "dampingNmsPerRad": 60, "maxTorqueNm": 900}},
    {"bone": "Chest", "motor": {"stiffnessNmPerRad": 900, "dampingNmsPerRad": 60, "maxTorqueNm": 900}},
    {"bone": "LeftHand", "motor": {"stiffnessNmPerRad": 50, "dampingNmsPerRad": 5, "maxTorqueNm": 60}},
    {"bone": "RightHand", "motor": {"stiffnessNmPerRad": 50, "dampingNmsPerRad": 5, "maxTorqueNm": 60}}
  ],
  "bodies": [
    {"bone": "LeftFoot", "material": {"friction": 1, "restitution": 0, "linearDamping": 0.05, "angularDamping": 0.05}},
    {"bone": "RightFoot", "material": {"friction": 1, "restitution": 0, "linearDamping": 0.05, "angularDamping": 0.05}}
  ],
  "feet": ["LeftFoot", "RightFoot"],
  "balance": {
    "legMotor": {"stiffnessNmPerRad": 2000, "dampingNmsPerRad": 200, "maxTorqueNm": 5000},
    "ankleGainRadPerM": 2,
    "hipGainRadPerM": 1.5,
    "maxLeanRad": 0.3
  },
  "stepping": {
    "armTimeS": 0.25,
    "stepTimeS": 0.25,
    "maxSteps": 4,
    "liftM": 0.08,
    "stanceHalfWidthM": 0.09,
    "maxStepLegFraction": 0.6,
    "velocityTuning": {"gainNsPerM": 1000, "maxTorqueNm": 400, "afterTouchdownS": 2, "trunkBones": ["Spine", "Chest", "Head"], "ankleOffTiltRad": 0.17},
    "swingGravityCompensation": true
  },
  "hitReaction": {"dropFraction": 0.7, "spread": 0.5, "recoverTimeS": 0.4, "reachJoints": 2},
  "arbiter": {"blendTimeS": 0.3, "fallTiltRad": 0.8, "standingSpeedMps": 0.05, "standingTimeS": 0.25, "restSpeedMps": 0.05, "restTimeS": 0.5},
  "death": {"jointPositionSteps": 2, "linearDampingPerS": 2, "angularDampingPerS": 10}
}
)JSON";
    }
}
