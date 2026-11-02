// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include <edenarchive/error.hpp>

#include <cstdio>

namespace EdenArchive
{
    const char* errorClassName(ErrorClass errorClass) noexcept
    {
        switch (errorClass)
        {
            case ErrorClass::None:
                return "OK";
            case ErrorClass::Magic:
                return "E_MAGIC";
            case ErrorClass::Version:
                return "E_VERSION";
            case ErrorClass::Truncated:
                return "E_TRUNCATED";
            case ErrorClass::Checksum:
                return "E_CHECKSUM";
            case ErrorClass::Range:
                return "E_RANGE";
            case ErrorClass::Overlap:
                return "E_OVERLAP";
            case ErrorClass::Order:
                return "E_ORDER";
            case ErrorClass::Duplicate:
                return "E_DUPLICATE";
            case ErrorClass::Name:
                return "E_NAME";
            case ErrorClass::Limit:
                return "E_LIMIT";
            case ErrorClass::Index:
                return "E_INDEX";
            case ErrorClass::Unsupported:
                return "E_UNSUPPORTED";
            case ErrorClass::Codec:
                return "E_CODEC";
            case ErrorClass::Hash:
                return "E_HASH";
            case ErrorClass::Io:
                return "E_IO";
        }
        return "E_UNKNOWN";
    }

    std::string Error::describe(const std::string& file) const
    {
        char offset[40];
        std::snprintf(offset, sizeof(offset), "0x%llX", static_cast<unsigned long long>(mOffset));
        std::string text;
        if (!file.empty())
            text = file + ": ";
        text += errorClassName(mClass);
        text += " at offset ";
        text += offset;
        if (!mField.empty())
            text += ", field " + mField;
        if (!mMessage.empty())
            text += ": " + mMessage;
        return text;
    }
}
