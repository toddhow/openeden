#include "texturemips.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <typeindex>
#include <unordered_set>

#include <vsg/core/MipmapLayout.h>
#include <vsg/core/Visitor.h>
#include <vsg/nodes/CullGroup.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Transform.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/Image.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/StateSwitch.h>

#include "textureresize.hpp"

namespace vsgUtil::TextureMips
{
    namespace TM = Render::VideoMemory::TextureMips;

    namespace
    {
        using Clock = std::chrono::steady_clock;

        double millisecondsSince(Clock::time_point begin)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        }

        // The data of the one image a descriptor holds, or null (several images, or an image without data).
        vsg::Data* singleImageData(const vsg::Descriptor& descriptor, const vsg::DescriptorImage*& image)
        {
            image = descriptor.cast<vsg::DescriptorImage>();
            if (!image || image->imageInfoList.size() != 1)
                return nullptr;
            const auto& info = image->imageInfoList[0];
            if (!info || !info->imageView || !info->imageView->image)
                return nullptr;
            return info->imageView->image->data.get();
        }

        // The levels of the chain: the layout when the image has one (what the DDS reader gives), else the property.
        std::uint32_t levelCount(const vsg::Data& data)
        {
            if (const vsg::MipmapLayout* layout = data.getMipmapLayout())
                return static_cast<std::uint32_t>(layout->size());
            return data.properties.mipLevels;
        }

        TM::Texture describe(const vsg::Data& data, std::uint64_t id)
        {
            TM::Texture t;
            t.id = id;
            const auto extents = data.pixelExtents();
            t.width = std::get<0>(extents);
            t.height = std::get<1>(extents);
            t.mipLevels = levelCount(data);
            t.blockWidth = std::max<std::uint32_t>(1, data.properties.blockWidth);
            t.blockHeight = std::max<std::uint32_t>(1, data.properties.blockHeight);
            t.blockBytes = static_cast<std::uint32_t>(data.valueSize());
            return t;
        }
    }

    namespace
    {
        std::mutex reductionMutex;
        // The full image to the reduced copy new readers get. The key stays valid while the swapper's entry holds the
        // full image; the swapper removes the pair when it forgets the entry.
        std::map<const vsg::Data*, vsg::ref_ptr<vsg::Data>> activeReductions;

        void setReduction(const vsg::Data* full, vsg::ref_ptr<vsg::Data> reduced)
        {
            std::scoped_lock lock(reductionMutex);
            if (reduced)
                activeReductions[full] = std::move(reduced);
            else
                activeReductions.erase(full);
        }

        // Reads a SharedObjects' internals: its LoadedObject entries are not reachable through its public interface.
        struct SharedObjectsPeek : vsg::SharedObjects
        {
            using vsg::SharedObjects::_sharedObjects;
        };
    }

    vsg::ref_ptr<vsg::Data> activeReduction(const vsg::Data* full)
    {
        std::scoped_lock lock(reductionMutex);
        const auto found = activeReductions.find(full);
        return found == activeReductions.end() ? vsg::ref_ptr<vsg::Data>() : found->second;
    }

    std::vector<vsg::ref_ptr<vsg::Node>> cachedModels(vsg::SharedObjects& sharedObjects)
    {
        std::vector<vsg::ref_ptr<vsg::Node>> models;
        std::scoped_lock lock(sharedObjects.mutex);
        auto& peek = static_cast<SharedObjectsPeek&>(sharedObjects);
        const auto found = peek._sharedObjects.find(std::type_index(typeid(vsg::LoadedObject)));
        if (found == peek._sharedObjects.end())
            return models;
        for (const auto& object : found->second)
            if (auto* loaded = dynamic_cast<vsg::LoadedObject*>(object.get()))
                if (auto node = loaded->object.cast<vsg::Node>())
                    models.push_back(node);
        return models;
    }

    bool reducible(const vsg::Data& data)
    {
        if (data.dimensions() != 2 || data.depth() > 1 || levelCount(data) < 2)
            return false;
        const int8_t type = data.properties.imageViewType;
        return type < 0 || type == VK_IMAGE_VIEW_TYPE_2D;
    }

    // Walks the graph once, noting for every bind that holds an eligible image which group uses it.
    struct ScanVisitor : vsg::Visitor
    {
        Swapper& swapper;
        std::unordered_set<const vsg::StateGroup*> seen;
        std::vector<std::size_t> touched; // entries seen
        const vsg::dvec3* camera = nullptr; // null: no use signal
        std::vector<vsg::dmat4> matrices{ vsg::dmat4() }; // the transforms above the node being visited
        double sphereDistance = std::numeric_limits<double>::infinity(); // of the innermost sphere above

        explicit ScanVisitor(Swapper& s)
            : swapper(s)
        {
            // A switched-off child (a hidden object, an unused alternative) still holds its binds: visit it too.
            overrideMask = vsg::MASK_ALL;
        }

        void note(vsg::StateGroup& group, vsg::StateCommand* command)
        {
            auto* bind = command ? command->cast<vsg::BindDescriptorSet>() : nullptr;
            if (!bind || !bind->descriptorSet)
                return;
            for (const auto& descriptor : bind->descriptorSet->descriptors)
            {
                const vsg::DescriptorImage* image = nullptr;
                vsg::Data* data = singleImageData(*descriptor, image);
                if (!data || !reducible(*data))
                    continue;
                std::size_t entryIndex = 0;
                int drop = 0;
                const auto found = swapper.mDataIndex.find(data);
                if (found != swapper.mDataIndex.end())
                {
                    entryIndex = found->second.first;
                    drop = found->second.second;
                }
                else
                {
                    const TM::Texture size = describe(*data, 0);
                    if (std::max(size.width, size.height) < swapper.mParams.minLongSide)
                        continue;
                    Swapper::Entry entry;
                    entry.id = swapper.mNextId++;
                    entry.full = vsg::ref_ptr<vsg::Data>(data);
                    entryIndex = swapper.mEntries.size();
                    swapper.mEntries.push_back(std::move(entry));
                    swapper.mDataIndex[data] = { entryIndex, 0 };
                    swapper.mEntries[entryIndex].users.clear();
                }
                if (std::find(touched.begin(), touched.end(), entryIndex) == touched.end())
                {
                    touched.push_back(entryIndex);
                    swapper.mEntries[entryIndex].users.clear();
                }
                swapper.mEntries[entryIndex].users.push_back(
                    { vsg::observer_ptr<vsg::StateGroup>(&group), drop, sphereDistance });
            }
        }

        // vsg::Visitor's default for a node does nothing; every node must pass the visit on to its children.
        void apply(vsg::Object& object) override { object.traverse(*this); }

        void apply(vsg::Transform& transform) override
        {
            matrices.push_back(transform.transform(matrices.back()));
            transform.traverse(*this);
            matrices.pop_back();
        }

        void visitSphere(const vsg::dsphere& bound, vsg::Group* group, vsg::Node* child)
        {
            const double saved = sphereDistance;
            if (camera)
            {
                const vsg::dvec3 center = matrices.back() * bound.center;
                sphereDistance = vsg::length(center - *camera) - bound.radius;
            }
            if (group)
                group->traverse(*this);
            else if (child)
                child->accept(*this);
            sphereDistance = saved;
        }

        void apply(vsg::CullNode& node) override { visitSphere(node.bound, nullptr, node.child.get()); }

        void apply(vsg::CullGroup& node) override { visitSphere(node.bound, &node, nullptr); }

        void apply(vsg::StateGroup& group) override
        {
            if (!seen.insert(&group).second)
                return;
            for (const auto& command : group.stateCommands)
            {
                if (auto* choice = command->cast<vsg::StateSwitch>())
                {
                    for (const auto& child : choice->children)
                        note(group, child.stateCommand.get());
                }
                else
                    note(group, command.get());
            }
            group.traverse(*this);
        }
    };

    Swapper::Swapper(Hooks hooks, TM::Params params)
        : mHooks(std::move(hooks))
        , mParams(params)
    {
    }

    std::vector<TM::Texture> Swapper::scan(vsg::Node& root)
    {
        return scanImpl(root, nullptr, 0);
    }

    std::vector<TM::Texture> Swapper::scan(vsg::Node& root, const vsg::dvec3& camera, std::uint64_t frame)
    {
        return scanImpl(root, &camera, frame);
    }

    std::vector<TM::Texture> Swapper::scanImpl(vsg::Node& root, const vsg::dvec3* camera, std::uint64_t frame)
    {
        // Entries are rebuilt from what is bound now: the index covers the full image and each reduced copy.
        mDataIndex.clear();
        for (std::size_t i = 0; i < mEntries.size(); ++i)
        {
            mDataIndex[mEntries[i].full.get()] = { i, 0 };
            for (const auto& [drop, data] : mEntries[i].reduced)
                mDataIndex[data.get()] = { i, drop };
        }

        ScanVisitor visitor(*this);
        visitor.camera = camera;
        root.accept(visitor);

        // Entries not seen any more have no users: those at drop 0 are forgotten, the others keep their reduced copy
        // only while some bind could still hold it, which a scan with no users says is not so.
        std::vector<Entry> kept;
        for (std::size_t i = 0; i < mEntries.size(); ++i)
        {
            if (std::find(visitor.touched.begin(), visitor.touched.end(), i) == visitor.touched.end())
            {
                setReduction(mEntries[i].full.get(), {}); // forgotten: readers get the full image again
                continue;
            }
            Entry& entry = mEntries[i];
            for (auto it = entry.reduced.begin(); it != entry.reduced.end();)
            {
                const bool used = std::any_of(
                    entry.users.begin(), entry.users.end(), [&](const User& u) { return u.drop == it->first; });
                it = used ? std::next(it) : entry.reduced.erase(it);
            }
            kept.push_back(std::move(entry));
        }
        mEntries = std::move(kept);
        mDataIndex.clear();

        std::vector<TM::Texture> textures;
        textures.reserve(mEntries.size());
        for (Entry& entry : mEntries)
        {
            TM::Texture t = describe(*entry.full, entry.id);
            t.drop = entry.users.empty() ? 0 : entry.users.front().drop;
            for (const User& user : entry.users)
                t.drop = std::min(t.drop, user.drop);
            t.changedBefore = entry.changedBefore;
            t.lastChangeFrame = entry.lastChangeFrame;
            t.visible = camera && std::any_of(entry.users.begin(), entry.users.end(), [&](const User& u) {
                return u.distance < mParams.visibleDistance;
            });
            if (t.visible)
                entry.lastNearFrame = frame;
            t.lastUsedFrame = entry.lastNearFrame;
            textures.push_back(t);
        }
        return textures;
    }

    std::int64_t Swapper::residentBytes() const
    {
        std::int64_t sum = 0;
        for (const Entry& entry : mEntries)
        {
            const TM::Texture t = describe(*entry.full, entry.id);
            // One image per distinct drop in use.
            std::vector<int> drops;
            for (const User& user : entry.users)
                if (std::find(drops.begin(), drops.end(), user.drop) == drops.end())
                    drops.push_back(user.drop);
            for (const int drop : drops)
                sum += TM::chainBytes(t, drop);
        }
        return sum;
    }

    Swapper::RetiredReport Swapper::retiredReport()
    {
        RetiredReport report;
        report.retired = static_cast<int>(mRetiredProbes.size());
        std::vector<Probe> alive;
        for (auto& probe : mRetiredProbes)
        {
            if (auto object = probe.object.ref_ptr())
            {
                ++report.alive;
                report.aliveTextures.push_back(probe.texture);
                report.referenceCounts += object->referenceCount() - 1; // minus this probe's own reference
                alive.push_back(probe);
            }
        }
        mRetiredProbes = std::move(alive);
        std::vector<vsg::observer_ptr<vsg::Object>> imagesAlive;
        report.imagesRetired = static_cast<int>(mRetiredImages.size());
        for (auto& probe : mRetiredImages)
            if (auto object = probe.ref_ptr())
            {
                ++report.imagesAlive;
                report.imageReferenceCounts += object->referenceCount() - 1;
                imagesAlive.push_back(probe);
            }
        mRetiredImages = std::move(imagesAlive);
        return report;
    }

    SwapResult Swapper::swap(std::uint64_t id, int toDrop, std::uint64_t frame)
    {
        SwapResult result;
        const auto found = std::find_if(mEntries.begin(), mEntries.end(), [&](const Entry& e) { return e.id == id; });
        if (found == mEntries.end() || toDrop < 0)
            return result;
        Entry& entry = *found;
        const TM::Texture size = describe(*entry.full, entry.id);
        if (toDrop > TM::maxDrop(size, mParams))
            return result;

        const auto buildBegin = Clock::now();
        vsg::ref_ptr<vsg::Data> target = entry.full;
        if (toDrop > 0)
        {
            auto& slot = entry.reduced[toDrop];
            if (!slot)
                slot = TextureResize::dropMipLevels(*entry.full, toDrop);
            if (!slot)
            {
                entry.reduced.erase(toDrop);
                return result;
            }
            target = slot;
        }
        result.buildMs += millisecondsSince(buildBegin);

        // The data pointers this entry's binds may hold now.
        const auto dropOf = [&](const vsg::Data* data) -> int {
            if (data == entry.full.get())
                return 0;
            for (const auto& [drop, reduced] : entry.reduced)
                if (reduced.get() == data)
                    return drop;
            return -1;
        };

        std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>> replacements;
        std::vector<vsg::ref_ptr<vsg::Object>> retired;
        bool failed = false;

        const auto replacementFor = [&](vsg::BindDescriptorSet& old) -> vsg::ref_ptr<vsg::BindDescriptorSet> {
            const auto cached = replacements.find(&old);
            if (cached != replacements.end())
                return cached->second;
            if (!old.descriptorSet)
                return {};
            const auto begin = Clock::now();
            vsg::Descriptors descriptors = old.descriptorSet->descriptors;
            bool changed = false;
            for (auto& descriptor : descriptors)
            {
                const vsg::DescriptorImage* oldImage = nullptr;
                vsg::Data* data = singleImageData(*descriptor, oldImage);
                if (!data)
                    continue;
                const int current = dropOf(data);
                if (current < 0 || current == toDrop)
                    continue;
                const auto& info = oldImage->imageInfoList[0];
                mRetiredImages.emplace_back(info->imageView->image.get());
                auto image = vsg::Image::create(target);
                image->usage = info->imageView->image->usage;
                auto view = vsg::ImageView::create(image);
                auto newInfo = vsg::ImageInfo::create(info->sampler, view, info->imageLayout);
                descriptor = vsg::DescriptorImage::create(
                    newInfo, oldImage->dstBinding, oldImage->dstArrayElement, oldImage->descriptorType);
                changed = true;
                result.bytesBefore += TM::chainBytes(size, current);
                result.bytesAfter += TM::chainBytes(size, toDrop);
            }
            if (!changed)
            {
                replacements[&old] = {};
                return {};
            }
            auto set = vsg::DescriptorSet::create(old.descriptorSet->setLayout, descriptors);
            auto bind = vsg::BindDescriptorSet::create(old.pipelineBindPoint, old.layout, old.firstSet, set);
            bind->slot = old.slot;
            result.buildMs += millisecondsSince(begin);
            const auto compileBegin = Clock::now();
            if (mHooks.compile && !mHooks.compile(bind))
            {
                failed = true;
                return {};
            }
            result.compileMs += millisecondsSince(compileBegin);
            ++result.binds;
            replacements[&old] = bind;
            retired.push_back(vsg::ref_ptr<vsg::Object>(&old));
            return bind;
        };

        const auto replaceBegin = Clock::now();
        const double buildBefore = result.buildMs;
        const double compileBefore = result.compileMs;
        for (User& user : entry.users)
        {
            auto group = user.group.ref_ptr();
            if (!group || failed)
                continue;
            bool touched = false;
            for (auto& command : group->stateCommands)
            {
                if (auto* choice = command->cast<vsg::StateSwitch>())
                {
                    for (auto& child : choice->children)
                    {
                        auto* bind = child.stateCommand ? child.stateCommand->cast<vsg::BindDescriptorSet>() : nullptr;
                        if (!bind)
                            continue;
                        if (auto replacement = replacementFor(*bind))
                        {
                            child.stateCommand = replacement;
                            touched = true;
                        }
                    }
                }
                else if (auto* bind = command->cast<vsg::BindDescriptorSet>())
                {
                    if (auto replacement = replacementFor(*bind))
                    {
                        command = replacement;
                        touched = true;
                    }
                }
            }
            if (touched)
            {
                ++result.groups;
                user.drop = toDrop;
            }
        }
        result.replaceMs
            = millisecondsSince(replaceBegin) - (result.buildMs - buildBefore) - (result.compileMs - compileBefore);

        for (auto& object : retired)
            mRetiredProbes.push_back({ vsg::observer_ptr<vsg::Object>(object), id });
        if (mHooks.retire)
            for (auto& object : retired)
                mHooks.retire(object);
        entry.changedBefore = true;
        entry.lastChangeFrame = frame;
        result.ok = !failed;
        if (result.ok)
            setReduction(entry.full.get(), toDrop > 0 ? target : vsg::ref_ptr<vsg::Data>());
        return result;
    }
}
