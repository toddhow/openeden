#include "widgets.hpp"

#include <MyGUI_ComboBox.h>

#include <components/fx/uniformsettings.hpp>

#include <components/widgets/box.hpp>

namespace
{
    template <class T, class WidgetT>
    void createVectorWidget(
        const std::shared_ptr<Fx::Types::UniformBase>& uniform, MyGUI::Widget* client, Fx::Widgets::UniformBase* base)
    {
        constexpr int count = static_cast<int>(T::size());
        int height = client->getHeight();
        base->setSize(base->getSize().width, (base->getSize().height - height) + (height * count));
        client->setSize(client->getSize().width, height * count);

        for (int i = 0; i < count; ++i)
        {
            auto* widget = client->createWidget<WidgetT>(
                "MW_ValueEditNumber", { 0, height * i, client->getWidth(), height }, MyGUI::Align::Default);
            widget->setData(uniform, static_cast<Fx::Widgets::Index>(i));
            base->addItem(widget);
        }
    }
}

namespace Fx
{
    namespace Widgets
    {
        void EditBase::commit()
        {
            if (mShowing)
                return;
            storeValue(*mUniform);
            if (mOnChanged)
                mOnChanged();
        }

        void EditBool::setValue(bool value)
        {
            mCheckbutton->setCaptionWithReplacing(value ? "#{Interface:On}" : "#{Interface:Off}");
            mFill->setVisible(value);

            const bool changed = mUniform->getValue<bool>() != value;
            mUniform->setValue<bool>(value);
            if (changed)
                commit();
        }

        void EditBool::setValueFromUniform()
        {
            setValue(mUniform->template getValue<bool>());
        }

        void EditBool::toDefault()
        {
            setValue(mUniform->getDefault<bool>());
        }

        void EditBool::initialiseOverride()
        {
            Base::initialiseOverride();

            assignWidget(mCheckbutton, "Checkbutton");
            assignWidget(mFill, "Fill");

            mCheckbutton->eventMouseButtonClick += MyGUI::newDelegate(this, &EditBool::notifyMouseButtonClick);
        }

        void EditBool::notifyMouseButtonClick(MyGUI::Widget* /*sender*/)
        {
            setValue(!mUniform->getValue<bool>());
        }

        template <class T>
        void EditChoice::setValue(const T& value)
        {
            // Update the combo view
            for (size_t i = 0; i < this->mChoices->getItemCount(); i++)
            {
                if (*this->mChoices->getItemDataAt<T>(i) == value)
                {
                    this->mChoices->setIndexSelected(i);
                    break;
                }
            }

            const bool changed = !(mUniform->template getValue<T>() == value);
            mUniform->template setValue<T>(value);
            if (changed)
                commit();
        }

        void EditChoice::notifyComboBoxChanged(MyGUI::ComboBox* sender, size_t pos)
        {
            std::visit(
                [this, sender, pos](auto&& data) {
                    using T = typename std::decay_t<decltype(data)>::value_type;
                    setValue<T>(*sender->getItemDataAt<T>(pos));
                },
                mUniform->mData);
        }

        void EditChoice::setValueFromUniform()
        {
            std::visit(
                [this](auto&& data) {
                    using T = typename std::decay_t<decltype(data)>::value_type;
                    size_t index = 0;
                    for (const auto& choice : data.mChoices)
                    {
                        this->mChoices->addItem(choice.mLabel, choice.mValue);

                        if (choice.mValue == mUniform->template getValue<T>())
                        {
                            this->mChoices->setIndexSelected(index);
                        }

                        index++;
                    }

                    setValue(mUniform->template getValue<T>());
                },
                mUniform->mData);
        }

        void EditChoice::toDefault()
        {
            std::visit(
                [this](auto&& data) {
                    using T = typename std::decay_t<decltype(data)>::value_type;
                    setValue(mUniform->template getDefault<T>());
                },
                mUniform->mData);
        }

        void EditChoice::initialiseOverride()
        {
            Base::initialiseOverride();

            assignWidget(mChoices, "Choices");

            mChoices->eventComboChangePosition += MyGUI::newDelegate(this, &EditChoice::notifyComboBoxChanged);
        }

        void UniformBase::init(
            const std::shared_ptr<Fx::Types::UniformBase>& uniform, std::function<void()> onChanged)
        {
            if (uniform->mDisplayName.empty())
                mLabel->setCaption(uniform->mName);
            else
                mLabel->setCaptionWithReplacing(uniform->mDisplayName);

            if (uniform->mDescription.empty())
            {
                mLabel->setUserString("ToolTipType", "");
            }
            else
            {
                mLabel->setUserString("ToolTipType", "Layout");
                mLabel->setUserString("ToolTipLayout", "TextToolTip");
                mLabel->setUserString("Caption_Text", uniform->mDescription);
            }

            std::visit(
                [this, &uniform, &onChanged](auto&& arg) {
                    using T = typename std::decay_t<decltype(arg)>::value_type;

                    if (arg.mChoices.size() > 0)
                    {
                        auto* widget = mClient->createWidget<EditChoice>("MW_ValueEditChoice",
                            { 0, 0, mClient->getWidth(), mClient->getHeight() }, MyGUI::Align::Stretch);
                        widget->setData(uniform);
                        mBases.emplace_back(widget);
                    }
                    else
                    {
                        if constexpr (std::is_same_v<vsg::vec4, T>)
                        {
                            createVectorWidget<T, EditNumberFloat4>(uniform, mClient, this);
                        }
                        else if constexpr (std::is_same_v<vsg::vec3, T>)
                        {
                            createVectorWidget<T, EditNumberFloat3>(uniform, mClient, this);
                        }
                        else if constexpr (std::is_same_v<vsg::vec2, T>)
                        {
                            createVectorWidget<T, EditNumberFloat2>(uniform, mClient, this);
                        }
                        else if constexpr (std::is_same_v<T, float>)
                        {
                            auto* widget = mClient->createWidget<EditNumberFloat>("MW_ValueEditNumber",
                                { 0, 0, mClient->getWidth(), mClient->getHeight() }, MyGUI::Align::Stretch);
                            widget->setData(uniform);
                            mBases.emplace_back(widget);
                        }
                        else if constexpr (std::is_same_v<T, int>)
                        {
                            auto* widget = mClient->createWidget<EditNumberInt>("MW_ValueEditNumber",
                                { 0, 0, mClient->getWidth(), mClient->getHeight() }, MyGUI::Align::Stretch);
                            widget->setData(uniform);
                            mBases.emplace_back(widget);
                        }
                        else if constexpr (std::is_same_v<T, bool>)
                        {
                            auto* widget = mClient->createWidget<EditBool>("MW_ValueEditBool",
                                { 0, 0, mClient->getWidth(), mClient->getHeight() }, MyGUI::Align::Stretch);
                            widget->setData(uniform);
                            mBases.emplace_back(widget);
                        }
                    }

                    mReset->eventMouseButtonClick += MyGUI::newDelegate(this, &UniformBase::notifyResetClicked);

                    for (EditBase* base : mBases)
                    {
                        base->setOnChanged(onChanged);
                        base->showUniform();
                    }
                },
                uniform->mData);
        }

        void UniformBase::addItem(EditBase* item)
        {
            mBases.emplace_back(item);
        }

        void UniformBase::toDefault()
        {
            for (EditBase* base : mBases)
            {
                if (base)
                    base->toDefault();
            }
        }

        void UniformBase::notifyResetClicked(MyGUI::Widget* /*sender*/)
        {
            toDefault();
        }

        void UniformBase::initialiseOverride()
        {
            Base::initialiseOverride();

            assignWidget(mReset, "Reset");
            assignWidget(mLabel, "Label");
            assignWidget(mClient, "Client");
        }
    }
}
