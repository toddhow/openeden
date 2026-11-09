#include "enchantingdialog.hpp"

#include <iomanip>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_UString.h>

#include <components/misc/strings/format.hpp>
#include <components/settings/values.hpp>
#include <components/widgets/list.hpp>

#include <components/esm3/loadgmst.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/craftingrules.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/itemhandles.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "itemselection.hpp"
#include "itemwidget.hpp"

#include "sortfilteritemmodel.hpp"

namespace MWGui
{
    namespace
    {
        namespace sdk = openeden::sdk::game;

        // The plain form of the effects the player chose, for the crafting contract (ids as text).
        std::vector<sdk::EffectLine> effectLines(const ESM::EffectList& list)
        {
            std::vector<sdk::EffectLine> lines;
            lines.reserve(list.mList.size());
            for (const ESM::IndexedENAMstruct& effect : list.mList)
            {
                sdk::EffectLine line;
                line.effectId = effect.mData.mEffectID.serializeText();
                line.skill = effect.mData.mSkill.serializeText();
                line.attribute = effect.mData.mAttribute.serializeText();
                line.range = effect.mData.mRange;
                line.area = effect.mData.mArea;
                line.duration = effect.mData.mDuration;
                line.magnitudeMin = effect.mData.mMagnMin;
                line.magnitudeMax = effect.mData.mMagnMax;
                lines.push_back(std::move(line));
            }
            return lines;
        }
    }

    EnchantingDialog::EnchantingDialog()
        : WindowBase("openmw_enchanting_dialog.layout")
        , EffectEditorBase(EffectEditorBase::Enchanting)
        , mEnchanting(MWWorld::requireCraftingRules().beginEnchanting())
    {
        getWidget(mName, "NameEdit");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mAvailableEffectsList, "AvailableEffects");
        getWidget(mUsedEffectsView, "UsedEffects");
        getWidget(mItemBox, "ItemBox");
        getWidget(mSoulBox, "SoulBox");
        getWidget(mEnchantmentPoints, "Enchantment");
        getWidget(mCastCost, "CastCost");
        getWidget(mCharge, "Charge");
        getWidget(mSuccessChance, "SuccessChance");
        getWidget(mChanceLayout, "ChanceLayout");
        getWidget(mTypeButton, "TypeButton");
        getWidget(mBuyButton, "BuyButton");
        getWidget(mPrice, "PriceLabel");
        getWidget(mPriceText, "PriceTextLabel");

        setWidgets(mAvailableEffectsList, mUsedEffectsView);

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onCancelButtonClicked);
        mItemBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onSelectItem);
        mSoulBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onSelectSoul);
        mBuyButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onBuyButtonClicked);
        mTypeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onTypeButtonClicked);
        mName->eventEditSelectAccept += MyGUI::newDelegate(this, &EnchantingDialog::onAccept);

        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:Cancel}";
        mControllerButtons.mY = "#{OMWEngine:EnchantType}";
        mControllerButtons.mL1 = "#{Interface:Item}";
        mControllerButtons.mR1 = "#{Interface:Soul}";
    }

    EnchantingDialog::~EnchantingDialog() {}

    void EnchantingDialog::onOpen()
    {
        center();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mName);
    }

    void EnchantingDialog::setSoulGem(const MWWorld::Ptr& gem)
    {
        MWWorld::ItemHandleScope handles;
        if (gem.isEmpty())
        {
            mSoulBox->setItem(MWWorld::Ptr());
            mSoulBox->clearUserStrings();
            mEnchanting->setSoulGem(sdk::kNoItem);
        }
        else
        {
            mSoulBox->setItem(gem);
            mSoulBox->setUserString("ToolTipType", "ItemPtr");
            mSoulBox->setUserData(MWWorld::Ptr(gem));
            mEnchanting->setSoulGem(handles.make(gem));
        }
    }

    void EnchantingDialog::setItem(const MWWorld::Ptr& item)
    {
        MWWorld::ItemHandleScope handles;
        if (item.isEmpty())
        {
            mItemBox->setItem(MWWorld::Ptr());
            mItemBox->clearUserStrings();
            mEnchanting->setOldItem(sdk::kNoItem);
        }
        else
        {
            std::string_view name = item.getClass().getName(item);
            mName->setCaption(MyGUI::UString(name));
            mItemBox->setItem(item);
            mItemBox->setUserString("ToolTipType", "ItemPtr");
            mItemBox->setUserData(MWWorld::Ptr(item));
            mEnchanting->setOldItem(handles.make(item));
        }
    }

    void EnchantingDialog::updateLabels()
    {
        mEnchantmentPoints->setCaption(std::to_string(static_cast<int>(mEnchanting->enchantPoints(false))) + " / "
            + std::to_string(mEnchanting->maxEnchantValue()));
        mCharge->setCaption(std::to_string(mEnchanting->gemCharge()));
        mSuccessChance->setCaption(std::to_string(std::clamp(mEnchanting->enchantChance(), 0, 100)));
        mCastCost->setCaption(std::to_string(mEnchanting->effectiveCastCost()));
        mPrice->setCaption(std::to_string(mEnchanting->enchantPrice()));

        switch (mEnchanting->castStyle())
        {
            case sdk::CastStyle::CastOnce:
                mTypeButton->setCaption(MyGUI::UString(
                    MWBase::Environment::get().getWindowManager()->getGameSettingString("sItemCastOnce", "Cast Once")));
                setConstantEffect(false);
                break;
            case sdk::CastStyle::WhenStrikes:
                mTypeButton->setCaption(
                    MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                        "sItemCastWhenStrikes", "When Strikes")));
                setConstantEffect(false);
                break;
            case sdk::CastStyle::WhenUsed:
                mTypeButton->setCaption(
                    MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                        "sItemCastWhenUsed", "When Used")));
                setConstantEffect(false);
                break;
            case sdk::CastStyle::ConstantEffect:
                mTypeButton->setCaption(
                    MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                        "sItemCastConstant", "Cast Constant")));
                setConstantEffect(true);
                break;
        }
    }

    void EnchantingDialog::setPtr(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || (ptr.getType() != ESM::REC_MISC && !ptr.getClass().isActor()))
            throw std::runtime_error("Invalid argument in EnchantingDialog::setPtr");

        mName->setCaption({});

        MWWorld::ItemHandleScope handles;
        if (ptr.getClass().isActor())
        {
            mEnchanting->setSelfEnchanting(false);
            mEnchanting->setEnchanter(handles.make(ptr));
            mBuyButton->setCaptionWithReplacing("#{sBuy}");
            mControllerButtons.mX = "#{Interface:Buy}";
            mChanceLayout->setVisible(false);
            mPtr = ptr;
            setSoulGem(MWWorld::Ptr());
            mPrice->setVisible(true);
            mPriceText->setVisible(true);
        }
        else
        {
            mEnchanting->setSelfEnchanting(true);
            mEnchanting->setEnchanter(handles.make(MWMechanics::getPlayer()));
            mBuyButton->setCaptionWithReplacing("#{sCreate}");
            mControllerButtons.mX = "#{Interface:Create}";
            mChanceLayout->setVisible(Settings::game().mShowEnchantChance);
            mPtr = MWMechanics::getPlayer();
            setSoulGem(ptr);
            mPrice->setVisible(false);
            mPriceText->setVisible(false);
        }

        setItem(MWWorld::Ptr());
        startEditing();
        updateLabels();
    }

    void EnchantingDialog::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
        resetReference();
    }

    void EnchantingDialog::resetReference()
    {
        ReferenceInterface::resetReference();
        setItem(MWWorld::Ptr());
        setSoulGem(MWWorld::Ptr());
        mPtr = MWWorld::Ptr();
        mEnchanting->setEnchanter(sdk::kNoItem);
    }

    void EnchantingDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
    }

    void EnchantingDialog::onSelectItem(MyGUI::Widget* /*sender*/)
    {
        if (mEnchanting->itemEmpty())
        {
            mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sEnchantItems}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &EnchantingDialog::onItemSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &EnchantingDialog::onItemCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyEnchantable);
        }
        else
        {
            setItem(MWWorld::Ptr());
            updateLabels();
        }
    }

    void EnchantingDialog::onItemSelected(MWWorld::Ptr item)
    {
        mItemSelectionDialog->setVisible(false);

        setItem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        mEnchanting->nextCastStyle();
        updateLabels();
    }

    void EnchantingDialog::onItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSoulSelected(MWWorld::Ptr item)
    {
        mItemSelectionDialog->setVisible(false);

        {
            MWWorld::ItemHandleScope handles;
            mEnchanting->setSoulGem(handles.make(item));
        }
        if (mEnchanting->gemCharge() == 0)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage32}");
            return;
        }

        setSoulGem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        updateLabels();
    }

    void EnchantingDialog::onSoulCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSelectSoul(MyGUI::Widget* /*sender*/)
    {
        if (mEnchanting->soulEmpty())
        {
            mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sSoulGemsWithSouls}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &EnchantingDialog::onSoulSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &EnchantingDialog::onSoulCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyChargedSoulstones);

            // MWBase::Environment::get().getWindowManager()->messageBox("#{sInventorySelectNoSoul}");
        }
        else
        {
            setSoulGem(MWWorld::Ptr());
            mEnchanting->nextCastStyle();
            updateLabels();
            updateEffectsView();
        }
    }

    void EnchantingDialog::notifyEffectsChanged()
    {
        mEffectList.populate(mEffects);
        mEnchanting->setEffects(effectLines(mEffectList));
        updateLabels();
    }

    void EnchantingDialog::onTypeButtonClicked(MyGUI::Widget* /*sender*/)
    {
        mEnchanting->nextCastStyle();
        updateLabels();
        updateEffectsView();
    }

    void EnchantingDialog::onAccept(MyGUI::EditBox* sender)
    {
        onBuyButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void EnchantingDialog::onBuyButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (mEffects.size() <= 0)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sEnchantmentMenu11}");
            return;
        }

        if (mName->getCaption().empty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage10}");
            return;
        }

        if (mEnchanting->soulEmpty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage52}");
            return;
        }

        if (mEnchanting->itemEmpty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage11}");
            return;
        }

        if (static_cast<int>(mEnchanting->enchantPoints(false)) > mEnchanting->maxEnchantValue())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage29}");
            return;
        }

        mEnchanting->setNewItemName(mName->getCaption().asUTF8());
        mEnchanting->setEffects(effectLines(mEffectList));

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);
        if (mPtr != player && mEnchanting->enchantPrice() > playerGold)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage18}");
            return;
        }

        // check if the player is attempting to use a soulstone or item that was stolen from this actor
        if (mPtr != player)
        {
            for (int i = 0; i < 2; ++i)
            {
                MWWorld::Ptr item = MWWorld::resolveItem(i == 0 ? mEnchanting->oldItem() : mEnchanting->soulGem());
                if (MWBase::Environment::get().getMechanicsManager()->isItemStolenFrom(
                        item.getCellRef().getRefId(), mPtr))
                {
                    std::string msg = MWBase::Environment::get()
                                          .getESMStore()
                                          ->get<ESM::GameSetting>()
                                          .find("sNotifyMessage49")
                                          ->mValue.getString();
                    msg = Misc::StringUtils::format(msg, item.getClass().getName(item));
                    MWBase::Environment::get().getWindowManager()->messageBox(msg);

                    MWBase::Environment::get().getMechanicsManager()->confiscateStolenItemToOwner(
                        player, item, mPtr, 1);

                    MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
                    MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
                    return;
                }
            }
        }

        if (mEnchanting->create())
        {
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("enchant success"));
            MWBase::Environment::get().getWindowManager()->messageBox("#{sEnchantmentMenu12}");
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
        }
        else
        {
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("enchant fail"));
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage34}");
            const MWWorld::Ptr gem = MWWorld::resolveItem(mEnchanting->soulGem());
            if (!gem.isEmpty() && !gem.getCellRef().getCount())
            {
                setSoulGem(MWWorld::Ptr());
                mEnchanting->nextCastStyle();
                updateLabels();
                updateEffectsView();
            }
        }
    }

    bool EnchantingDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancelButtonClicked(mCancelButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
            onBuyButtonClicked(mBuyButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_Y)
            onTypeButtonClicked(mTypeButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            onSelectItem(mItemBox);
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
            onSelectSoul(mSoulBox);
        else
            return EffectEditorBase::onControllerButtonEvent(arg);

        return true;
    }
}
