#include "worker.hpp"

#include "luamanagerimp.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

#include <cassert>

namespace MWLua
{
    Worker::Worker(LuaManager& manager)
        : mManager(manager)
    {
        if (Settings::lua().mLuaNumThreads > 0)
            mThread = std::thread([this] { run(); });
    }

    Worker::~Worker()
    {
        if (mThread && mThread->joinable())
        {
            Log(Debug::Error)
                << "Unexpected destruction of LuaWorker; likely there is an unhandled exception in the main thread.";
            join();
        }
    }

    void Worker::allowUpdate(float dt)
    {
        if (!mThread)
            return;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mUpdateRequest = dt;
        }
        mCV.notify_one();
    }

    void Worker::finishUpdate(float dt)
    {
        if (mThread)
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mCV.wait(lk, [&] { return !mUpdateRequest.has_value(); });
        }
        else
            update(dt);
    }

    void Worker::join()
    {
        if (mThread)
        {
            {
                std::lock_guard<std::mutex> lk(mMutex);
                mJoinRequest = true;
            }
            mCV.notify_one();
            mThread->join();
        }
    }

    void Worker::update(float dt)
    {
        mManager.update(dt);
    }

    void Worker::run() noexcept
    {
        while (true)
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mCV.wait(lk, [&] { return mUpdateRequest.has_value() || mJoinRequest; });
            if (mJoinRequest)
                break;

            assert(mUpdateRequest.has_value());

            try
            {
                update(*mUpdateRequest);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to update LuaManager: " << e.what();
            }

            mUpdateRequest.reset();
            lk.unlock();
            mCV.notify_one();
        }
    }
}
