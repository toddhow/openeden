#include "documents.hpp"

#include "property_commands.hpp"

#include <opas/rig/Profile.hpp>
#include <opas/rig/ProfileFile.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace creativemotion_editor
{
    using namespace openeden::editor;

    namespace
    {
        // Merge IDs of profile properties: kMergeBase + an index, one per property (distinct from the rig's).
        constexpr int kMergeBase = 0x3000;
        constexpr int kMotorFieldCount = 3;

        struct MotorField
        {
            const char* mName;
            const char* mUnit;
            const char* mTooltip;
            double opas::rig::MotorGains::*mMember;
            bool mPositive; // true: the value must be > 0, false: >= 0
        };

        // docs/formats/profile.md: torque = stiffness x angle error - damping x relative angular velocity, clamped to
        // the maximum torque; the first two are >= 0, the maximum is > 0.
        const MotorField kMotorFields[kMotorFieldCount] = {
            { "stiffnessNmPerRad", "N*m/rad", "Torque per radian of angle error (>= 0)",
                &opas::rig::MotorGains::stiffnessNmPerRad, false },
            { "dampingNmsPerRad", "N*m*s/rad", "Torque per radian per second of relative angular velocity (>= 0)",
                &opas::rig::MotorGains::dampingNmsPerRad, false },
            { "maxTorqueNm", "N*m", "The largest torque the motor applies (> 0)", &opas::rig::MotorGains::maxTorqueNm,
                true },
        };

        class ProfileDocument final : public AssetDocument, public PropertySource
        {
        public:
            ProfileDocument(std::filesystem::path path, opas::rig::Profile profile)
                : mPath(std::move(path))
                , mAssetId(mPath.string())
                , mProfile(std::move(profile))
            {
            }

            std::string_view assetId() const override { return mAssetId; }
            PropertySource& properties() override { return *this; }
            bool isDirty() const override { return mDirty; }

            bool save(std::string& error) override
            {
                const std::string text = opas::rig::writeProfile(mProfile);
                std::ofstream out(mPath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    error = "cannot write '" + mPath.string() + "'";
                    return false;
                }
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.close();
                if (!out)
                {
                    error = "writing '" + mPath.string() + "' failed";
                    return false;
                }
                error.clear();
                mDirty = false;
                return true;
            }

            std::vector<PropertyDesc> describe() const override
            {
                std::vector<PropertyDesc> descs;
                PropertyDesc rig;
                rig.id = "profile.rig";
                rig.label = "Rig";
                rig.type = PropertyType::String;
                rig.readOnly = true;
                rig.group = "Profile";
                descs.push_back(std::move(rig));
                addMotor(descs, "default.", "Default motor");
                for (const opas::rig::JointOverride& joint : mProfile.joints)
                    addMotor(descs, "joint." + joint.bone + ".", joint.bone);
                return descs;
            }

            PropertyValue get(std::string_view id) const override
            {
                if (id == "profile.rig")
                    return mProfile.rig;
                const Located found = locate(id);
                if (found.mMotor == nullptr)
                    return 0.0;
                return (*found.mMotor).*kMotorFields[found.mField].mMember;
            }

            std::unique_ptr<Command> makeSetCommand(std::string_view id, const PropertyValue& value) override
            {
                const double* number = std::get_if<double>(&value);
                if (number == nullptr || !std::isfinite(*number))
                    return nullptr;
                const Located found = locate(id);
                if (found.mMotor == nullptr)
                    return nullptr;
                const MotorField& field = kMotorFields[found.mField];
                if (*number < 0.0 || (field.mPositive && *number <= 0.0))
                    return nullptr;
                return std::make_unique<SetNumberCommand>("Change " + std::string(id), &(found.mMotor->*field.mMember),
                    &mDirty, *number, kMergeBase + found.mIndex * kMotorFieldCount + found.mField);
            }

        private:
            struct Located
            {
                opas::rig::MotorGains* mMotor = nullptr;
                int mField = 0;
                int mIndex = 0; // 0: the default, 1 + n: override n
            };

            static void addMotor(std::vector<PropertyDesc>& descs, const std::string& prefix, const std::string& group)
            {
                for (const MotorField& field : kMotorFields)
                {
                    PropertyDesc d;
                    d.id = prefix + field.mName;
                    d.label = field.mName;
                    d.type = PropertyType::Float;
                    d.unit = field.mUnit;
                    d.tooltip = field.mTooltip;
                    d.min = 0.0;
                    d.step = 1.0;
                    d.group = group;
                    descs.push_back(std::move(d));
                }
            }

            Located locate(std::string_view id)
            {
                return locateIn(id);
            }
            Located locate(std::string_view id) const
            {
                return const_cast<ProfileDocument*>(this)->locateIn(id);
            }

            Located locateIn(std::string_view id)
            {
                const auto match = [&](const std::string& prefix, opas::rig::MotorGains& motor, int index) -> Located {
                    if (id.substr(0, prefix.size()) != prefix)
                        return {};
                    const std::string_view rest = id.substr(prefix.size());
                    for (int f = 0; f < kMotorFieldCount; ++f)
                        if (rest == kMotorFields[f].mName)
                            return { &motor, f, index };
                    return {};
                };
                if (Located found = match("default.", mProfile.defaultMotor, 0); found.mMotor != nullptr)
                    return found;
                for (std::size_t j = 0; j < mProfile.joints.size(); ++j)
                    if (Located found = match("joint." + mProfile.joints[j].bone + ".", mProfile.joints[j].motor,
                            1 + static_cast<int>(j));
                        found.mMotor != nullptr)
                        return found;
                return {};
            }

            std::filesystem::path mPath;
            std::string mAssetId;
            opas::rig::Profile mProfile;
            bool mDirty = false;
        };
    }

    std::unique_ptr<AssetDocument> openProfile(const std::filesystem::path& path, std::string& error)
    {
        const opas::rig::ProfileLoadResult result = opas::rig::loadProfileFile(path);
        if (!result.ok())
        {
            error = result.error.toString();
            if (error.empty())
                error = "cannot read '" + path.string() + "'";
            return nullptr;
        }
        error.clear();
        return std::make_unique<ProfileDocument>(path, *result.value);
    }
}
