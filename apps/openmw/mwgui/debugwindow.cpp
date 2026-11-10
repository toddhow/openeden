#include "debugwindow.hpp"

#include <MyGUI_EditBox.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>

#include <components/debug/debugging.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include <Jolt/Jolt.h>

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace MWGui
{

    DebugWindow::DebugWindow()
        : WindowBase("openmw_debug_window.layout")
    {
        getWidget(mTabControl, "TabControl");

        // Ideas for other tabs:
        // - Texture / compositor texture viewer
        // - Material editor
        // - Shader editor

        MyGUI::TabItem* itemLV = mTabControl->addItem("Log Viewer");
        itemLV->setCaptionWithReplacing(" #{OMWEngine:LogViewer} ");
        mLogView
            = itemLV->createWidgetReal<MyGUI::EditBox>("LogEdit", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Stretch);
        mLogView->setEditReadOnly(true);

        MyGUI::TabItem* itemLuaProfiler = mTabControl->addItem("Lua Profiler");
        itemLuaProfiler->setCaptionWithReplacing(" #{OMWEngine:LuaProfiler} ");
        mLuaProfiler = itemLuaProfiler->createWidgetReal<MyGUI::EditBox>(
            "LogEdit", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Stretch);
        mLuaProfiler->setEditReadOnly(true);

#ifdef JPH_PROFILE_ENABLED
        MyGUI::TabItem* item = mTabControl->addItem("Physics Profiler");
        item->setCaptionWithReplacing(" #{OMWEngine:PhysicsProfiler} ");
        mPhysicsProfilerEdit
            = item->createWidgetReal<MyGUI::EditBox>("LogEdit", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Stretch);
        mPhysicsProfilerEdit->setEditReadOnly(true);
#else
        mPhysicsProfilerEdit = nullptr;
#endif

        // The extensions' per-phase timings and scopes (task 0017 stage 5), last in the tab list so the indices of the
        // others stay. It is always there; its text says how to switch the profiler on.
        MyGUI::TabItem* itemExtensions = mTabControl->addItem("Extensions");
        itemExtensions->setCaptionWithReplacing(" #{OMWEngine:ExtensionsProfiler} ");
        mExtensionProfiler = itemExtensions->createWidgetReal<MyGUI::EditBox>(
            "LogEdit", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Stretch);
        mExtensionProfiler->setEditReadOnly(true);
        mExtensionsTabIndex = mTabControl->getItemCount() - 1;
        // Test aid (task 0017): start on the Extensions tab, for a screenshot
        if (const char* tab = std::getenv("OPENEDEN_DEBUG_WINDOW_TAB");
            tab != nullptr && std::string_view(tab) == "extensions")
            mTabControl->setIndexSelected(mExtensionsTabIndex);
    }

    static std::vector<char> sLogCircularBuffer;
    static std::mutex sBufferMutex;
    static std::size_t sLogStartIndex;
    static std::size_t sLogEndIndex;
    static bool hasPrefix = false;

    void DebugWindow::startLogRecording()
    {
        sLogCircularBuffer.resize(Settings::general().mLogBufferSize);
        Debug::setLogListener([](Debug::Level level, std::string_view prefix, std::string_view msg) {
            if (sLogCircularBuffer.empty())
                return; // Log viewer is disabled.
            std::string_view color;
            switch (level)
            {
                case Debug::Error:
                    color = "#FF0000";
                    break;
                case Debug::Warning:
                    color = "#FFFF00";
                    break;
                case Debug::Info:
                    color = "#FFFFFF";
                    break;
                case Debug::Verbose:
                case Debug::Debug:
                    color = "#666666";
                    break;
                default:
                    color = "#FFFFFF";
            }
            bool bufferOverflow = false;
            std::lock_guard lock(sBufferMutex);
            const std::size_t bufSize = sLogCircularBuffer.size();
            auto addChar = [&](char c) {
                sLogCircularBuffer[sLogEndIndex++] = c;
                if (sLogEndIndex == bufSize)
                    sLogEndIndex = 0;
                bufferOverflow = bufferOverflow || sLogEndIndex == sLogStartIndex;
            };
            auto addShieldedStr = [&](std::string_view s) {
                for (char c : s)
                {
                    addChar(c);
                    if (c == '#')
                        addChar(c);
                    if (c == '\n')
                        hasPrefix = false;
                }
            };
            for (char c : color)
                addChar(c);
            if (!hasPrefix)
            {
                addShieldedStr(prefix);
                hasPrefix = true;
            }
            addShieldedStr(msg);
            if (bufferOverflow)
                sLogStartIndex = (sLogEndIndex + 1) % bufSize;
        });
    }

    void DebugWindow::updateLogView()
    {
        std::lock_guard lock(sBufferMutex);

        if (!mLogView || sLogCircularBuffer.empty() || sLogStartIndex == sLogEndIndex)
            return;
        if (mLogView->isTextSelection())
            return; // Don't change text while player is trying to copy something

        std::string addition;
        const std::size_t bufSize = sLogCircularBuffer.size();
        {
            if (sLogStartIndex < sLogEndIndex)
                addition = std::string(sLogCircularBuffer.data() + sLogStartIndex, sLogEndIndex - sLogStartIndex);
            else
            {
                addition = std::string(sLogCircularBuffer.data() + sLogStartIndex, bufSize - sLogStartIndex);
                addition.append(sLogCircularBuffer.data(), sLogEndIndex);
            }
            sLogStartIndex = sLogEndIndex;
        }

        size_t scrollPos = mLogView->getVScrollPosition();
        bool scrolledToTheEnd = scrollPos + 1 >= mLogView->getVScrollRange();
        const std::size_t newSizeEstimation = mLogView->getTextLength() + addition.size();
        if (newSizeEstimation > bufSize)
            mLogView->eraseText(0, newSizeEstimation - bufSize);
        mLogView->addText(addition);
        if (scrolledToTheEnd && mLogView->getVScrollRange() > 0)
            mLogView->setVScrollPosition(mLogView->getVScrollRange() - 1);
        else
            mLogView->setVScrollPosition(scrollPos);
    }

    void DebugWindow::updateLuaProfile()
    {
        if (mLuaProfiler->isTextSelection())
            return;

        size_t previousPos = mLuaProfiler->getVScrollPosition();
        mLuaProfiler->setCaption(MWBase::Environment::get().getLuaManager()->formatResourceUsageStats());
        mLuaProfiler->setVScrollPosition(std::min(previousPos, mLuaProfiler->getVScrollRange() - 1));
    }

    void DebugWindow::updatePhysicsProfile()
    {
#ifdef JPH_PROFILE_ENABLED
        // Jolt writes its profile as an HTML chart on the next physics frame, request one each time the tab is opened
        if (mPhysicsProfileDumped)
            return;
        mPhysicsProfileDumped = true;

        JPH_PROFILE_DUMP("openmw");
        const std::filesystem::path chart = std::filesystem::current_path() / "profile_chart_openmw.html";
        mPhysicsProfilerEdit->setCaption("Jolt physics profile written to:\n" + chart.string());
#endif
    }

    void DebugWindow::updateExtensionProfile()
    {
        if (mExtensionProfiler->isTextSelection())
            return;

        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        std::string text = extensions->profilerOverlayEnabled() ? extensions->profilerReport() : std::string();
        if (!extensions->profilerOverlayEnabled())
            text
                = "The extension profiler is off. Set \"extension profiler = true\" under [Game] in settings.cfg and "
                  "restart the game.";
        size_t previousPos = mExtensionProfiler->getVScrollPosition();
        mExtensionProfiler->setCaption(text);
        if (mExtensionProfiler->getVScrollRange() > 0)
            mExtensionProfiler->setVScrollPosition(std::min(previousPos, mExtensionProfiler->getVScrollRange() - 1));
    }

    void DebugWindow::onFrame(float dt)
    {
        static float timer = 0;
        timer -= dt;
        if (timer > 0 || !isVisible())
            return;
        timer = 0.25;

        const std::size_t selected = mTabControl->getIndexSelected();
        if (selected != 2)
            mPhysicsProfileDumped = false;

        if (selected == mExtensionsTabIndex)
        {
            updateExtensionProfile();
            return;
        }

        switch (selected)
        {
            case 0:
                updateLogView();
                break;
            case 1:
                updateLuaProfile();
                break;
            case 2:
                updatePhysicsProfile();
                break;
            default:;
        }
    }
}
