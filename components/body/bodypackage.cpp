#include "bodypackage.hpp"

namespace Body
{
    namespace
    {
        // The path and id limits are the ones bodypackage.hpp states.
        constexpr std::size_t sMaxPathBytes = 512;
        constexpr std::size_t sMaxIdBytes = 64;
    }

    std::string_view toString(Origin value) noexcept
    {
        switch (value)
        {
            case Origin::Authored:
                return "authored";
            case Origin::Derived:
                return "derived";
            case Origin::Missing:
                return "missing";
        }
        return "authored";
    }

    std::string_view toString(MeshFormat value) noexcept
    {
        switch (value)
        {
            case MeshFormat::Nif:
                return "nif";
            case MeshFormat::Gltf:
                return "gltf";
            case MeshFormat::Summary:
                return "summary";
        }
        return "nif";
    }

    std::string_view toString(MeshRole value) noexcept
    {
        switch (value)
        {
            case MeshRole::Skin:
                return "skin";
            case MeshRole::Head:
                return "head";
            case MeshRole::Hair:
                return "hair";
            case MeshRole::GarmentBase:
                return "garmentBase";
            case MeshRole::Other:
                return "other";
        }
        return "skin";
    }

    std::string_view toString(AgeClass value) noexcept
    {
        switch (value)
        {
            case AgeClass::Unspecified:
                return "unspecified";
            case AgeClass::Minor:
                return "minor";
            case AgeClass::Adult:
                return "adult";
        }
        return "unspecified";
    }

    std::string_view toString(ShapeKind value) noexcept
    {
        switch (value)
        {
            case ShapeKind::Sphere:
                return "sphere";
            case ShapeKind::Capsule:
                return "capsule";
            case ShapeKind::Box:
                return "box";
        }
        return "capsule";
    }

    bool isSafePackagePath(std::string_view path) noexcept
    {
        if (path.empty() || path.size() > sMaxPathBytes || path.front() == '/')
        {
            return false;
        }
        std::size_t componentStart = 0;
        for (std::size_t i = 0; i <= path.size(); ++i)
        {
            if (i < path.size())
            {
                const unsigned char c = static_cast<unsigned char>(path[i]);
                if (c < 0x20 || c == 0x7F || c == ':' || c == '\\')
                {
                    return false;
                }
                if (c != '/')
                {
                    continue;
                }
            }
            const std::string_view component = path.substr(componentStart, i - componentStart);
            if (component.empty() || component == "." || component == "..")
            {
                return false;
            }
            componentStart = i + 1;
        }
        return true;
    }

    bool isValidPackageId(std::string_view id) noexcept
    {
        if (id.empty() || id.size() > sMaxIdBytes)
        {
            return false;
        }
        for (const char c : id)
        {
            const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            if (!valid)
            {
                return false;
            }
        }
        return true;
    }
}
