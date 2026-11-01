#ifndef VSGOPENMW_VSGUTIL_OPERATION_H
#define VSGOPENMW_VSGUTIL_OPERATION_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <vsg/core/Exception.h>
#include <vsg/threading/OperationQueue.h>

namespace vsgUtil
{
    /*
     * Tracks completion.
     */
    struct Operation : public vsg::Operation
    {
        std::atomic_bool done = false;
        virtual void operate() = 0;
        void run() override
        {
            try
            {
                operate();
            }
            catch (std::exception& e)
            {
                mError = e.what();
                std::cerr << "!Operation::run( " << e.what() << ")" << std::endl;
            }
            catch (vsg::Exception& e)
            {
                mError = e.message;
                std::cerr << "!Operation::run(vsg::Exception(" << e.message << "))" << std::endl;
            }
            {
                // OpenEden: set under the mutex, so a waiter cannot miss the notification between its check of done
                // and its wait (task 0012, V4g).
                std::lock_guard<std::mutex> lock(mMutex);
                done = true;
            }
            mCv.notify_all();
        }
        void wait()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [this] { return done.load(); });
        }
        void ensure()
        {
            wait();
            if (mError)
                throw std::runtime_error(*mError);
        }

    private:
        std::condition_variable mCv;
        std::mutex mMutex;
        std::optional<std::string> mError;
    };

    struct OperationFunc : public Operation
    {
        std::function<void()> func;
        OperationFunc(std::function<void()> f)
            : func(f)
        {
        }
        void operate() override { func(); }
    };
}

#endif
