#include "partreaderrorlog.hpp"

#include <components/debug/debuglog.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender
{
    bool ReadErrorLog::firstFailure(std::string_view model)
    {
        // The key is the VFS path: lower case, forward slashes (a record may spell the same file differently).
        std::string key(model);
        VFS::Path::normalizeFilenameInPlace(key);
        const std::lock_guard<std::mutex> lock(mMutex);
        if (mReported.find(key) != mReported.end())
            return false;
        // Full: count it as reported rather than grow without bound.
        if (mReported.size() >= sCapacity)
            return false;
        mReported.insert(std::move(key));
        return true;
    }

    void ReadErrorLog::clear()
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        mReported.clear();
    }

    std::size_t ReadErrorLog::size() const
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        return mReported.size();
    }

    ReadErrorLog& ReadErrorLog::instance()
    {
        static ReadErrorLog log;
        return log;
    }

    void logPartReadError(std::string_view prefix, const std::string& model, const std::exception& error)
    {
        if (ReadErrorLog::instance().firstFailure(model))
            Log(Debug::Error) << prefix << error.what();
        else
            Log(Debug::Verbose) << "Still unable to read '" << model << "'; the part is left out again";
    }
}
