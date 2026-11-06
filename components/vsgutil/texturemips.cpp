#include "texturemips.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
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

        // An array of layers as convertToArrayTexture makes it (the terrain's layer arrays).
        bool isArrayData(const vsg::Data& data)
        {
            return data.dimensions() == 3 && data.properties.imageViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY
                && data.depth() > 1;
        }

        bool isCompressed(const vsg::Data& data)
        {
            return data.properties.blockWidth > 1 || data.properties.blockHeight > 1;
        }

        // The levels of the chain: the layout when the image has one (what the DDS reader gives), else the property.
        // An uncompressed array stores one level and the GPU makes the rest: its chain is counted down to one texel.
        std::uint32_t levelCount(const vsg::Data& data)
        {
            if (const vsg::MipmapLayout* layout = data.getMipmapLayout())
                return static_cast<std::uint32_t>(layout->size());
            if (isArrayData(data) && !isCompressed(data))
            {
                std::uint32_t levels = 1;
                for (std::uint32_t side = std::max(data.width(), data.height()); side > 1; side >>= 1)
                    ++levels;
                return levels;
            }
            return data.properties.mipLevels;
        }

        // The reduced copy of an image or an array: a 2D image loses its top levels, a compressed array too, an
        // uncompressed array is shrunk by the box filter. Reads `full` only, so it may run on a worker thread.
        vsg::ref_ptr<vsg::Data> makeReduced(const vsg::ref_ptr<vsg::Data>& full, int drop, TextureResize::Kind kind)
        {
            if (!isArrayData(*full))
                return TextureResize::dropMipLevels(*full, drop);
            if (isCompressed(*full))
                return TextureResize::dropArrayLevels(*full, drop);
            return TextureResize::shrinkArray(*full, drop, kind);
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
            t.layers = isArrayData(data) ? data.depth() : 1;
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
        if (isArrayData(data))
        {
            if (data.getMipmapLayout())
                return isCompressed(data) && levelCount(data) >= 2 && (data.valueSize() == 8 || data.valueSize() == 16);
            return !isCompressed(data) && data.valueSize() == 4;
        }
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
                    if (isArrayData(*data) && swapper.mHooks.arrayKind)
                        entry.kind = swapper.mHooks.arrayKind(image->dstBinding);
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
        std::vector<Probe> imagesAlive;
        report.imagesRetired = static_cast<int>(mRetiredImages.size());
        for (auto& probe : mRetiredImages)
            if (auto object = probe.object.ref_ptr())
            {
                ++report.imagesAlive;
                report.aliveImageTextures.push_back(probe.texture);
                report.imageReferenceCounts += object->referenceCount() - 1;
                imagesAlive.push_back(probe);
            }
        mRetiredImages = std::move(imagesAlive);
        return report;
    }

    // A swap in progress (see beginSwap and stepSwap).
    struct Swapper::Job
    {
        struct Slot
        {
            vsg::ref_ptr<vsg::StateGroup> group;
            vsg::ref_ptr<vsg::StateSwitch> choice; // set when the bind is a child of a switch
            std::size_t index = 0;
        };
        struct BindJob
        {
            vsg::ref_ptr<vsg::BindDescriptorSet> old;
            std::vector<Slot> slots;
        };

        std::uint64_t id = 0;
        int toDrop = 0;
        std::uint64_t frame = 0;
        TM::Texture size;
        vsg::ref_ptr<vsg::Data> full;
        vsg::ref_ptr<vsg::Data> target;
        std::future<vsg::ref_ptr<vsg::Data>> pending; // the reduced array being made on a worker
        std::map<const vsg::Data*, int> dataDrops; // the images of this texture a bind may hold now, and their drops
        std::vector<BindJob> binds;
        std::size_t next = 0;
        std::map<const vsg::DescriptorImage*, vsg::ref_ptr<vsg::DescriptorImage>> descriptors;
        std::set<const vsg::StateGroup*> groupsTouched;
        SwapResult result;
        bool failed = false;
        bool isArray = false;
    };

    Swapper::~Swapper() = default;

    bool Swapper::beginSwap(std::uint64_t id, int toDrop, std::uint64_t frame)
    {
        if (mJob || toDrop < 0)
            return false;
        const auto found = std::find_if(mEntries.begin(), mEntries.end(), [&](const Entry& e) { return e.id == id; });
        if (found == mEntries.end())
            return false;
        Entry& entry = *found;
        const TM::Texture size = describe(*entry.full, entry.id);
        if (toDrop > TM::maxDrop(size, mParams))
            return false;

        auto job = std::make_shared<Job>();
        job->id = id;
        job->toDrop = toDrop;
        job->frame = frame;
        job->size = size;
        job->full = entry.full;
        job->isArray = isArrayData(*entry.full);
        const auto buildBegin = Clock::now();
        if (toDrop == 0)
            job->target = entry.full;
        else if (const auto cached = entry.reduced.find(toDrop); cached != entry.reduced.end() && cached->second)
            job->target = cached->second;
        else if (isArrayData(*entry.full))
        {
            if (mHooks.beforeArraySwap)
                mHooks.beforeArraySwap();
            job->pending = std::async(std::launch::async,
                [full = entry.full, toDrop, kind = entry.kind] { return makeReduced(full, toDrop, kind); });
        }
        else
        {
            job->target = makeReduced(entry.full, toDrop, entry.kind);
            if (!job->target)
                return false;
            entry.reduced[toDrop] = job->target;
        }
        if (isArrayData(*entry.full) && toDrop == 0 && mHooks.beforeArraySwap)
            mHooks.beforeArraySwap();
        job->result.buildMs += millisecondsSince(buildBegin);

        job->dataDrops[entry.full.get()] = 0;
        for (const auto& [drop, reduced] : entry.reduced)
            job->dataDrops[reduced.get()] = drop;

        // The distinct binds that hold this texture at another drop, and where each is installed.
        std::map<const vsg::BindDescriptorSet*, std::size_t> bindIndex;
        const auto holdsTexture = [&](const vsg::BindDescriptorSet& bind) {
            if (!bind.descriptorSet)
                return false;
            for (const auto& descriptor : bind.descriptorSet->descriptors)
            {
                const vsg::DescriptorImage* image = nullptr;
                const vsg::Data* data = singleImageData(*descriptor, image);
                const auto drop = data ? job->dataDrops.find(data) : job->dataDrops.end();
                if (drop != job->dataDrops.end() && drop->second != toDrop)
                    return true;
            }
            return false;
        };
        const auto addSlot = [&](vsg::BindDescriptorSet& bind, Job::Slot slot) {
            auto [at, inserted] = bindIndex.emplace(&bind, job->binds.size());
            if (inserted)
            {
                job->binds.emplace_back();
                job->binds.back().old = vsg::ref_ptr<vsg::BindDescriptorSet>(&bind);
            }
            job->binds[at->second].slots.push_back(std::move(slot));
        };
        for (User& user : entry.users)
        {
            auto group = user.group.ref_ptr();
            if (!group)
                continue;
            for (std::size_t i = 0; i < group->stateCommands.size(); ++i)
            {
                if (auto* choice = group->stateCommands[i]->cast<vsg::StateSwitch>())
                {
                    for (std::size_t c = 0; c < choice->children.size(); ++c)
                    {
                        auto* bind = choice->children[c].stateCommand
                            ? choice->children[c].stateCommand->cast<vsg::BindDescriptorSet>()
                            : nullptr;
                        if (bind && holdsTexture(*bind))
                            addSlot(*bind, { group, vsg::ref_ptr<vsg::StateSwitch>(choice), c });
                    }
                }
                else if (auto* bind = group->stateCommands[i]->cast<vsg::BindDescriptorSet>())
                {
                    if (holdsTexture(*bind))
                        addSlot(*bind, { group, {}, i });
                }
            }
        }
        mJob = std::move(job);
        return true;
    }

    Swapper::StepResult Swapper::stepSwap(int maxBinds, double budgetMs)
    {
        StepResult step;
        if (!mJob)
        {
            step.done = true;
            return step;
        }
        Job& job = *mJob;
        SwapResult& result = job.result;
        const auto stepBegin = Clock::now();

        if (!job.target)
        {
            if (job.pending.valid())
            {
                if (job.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    step.waiting = true;
                    step.total = result;
                    return step;
                }
                job.target = job.pending.get();
            }
            const auto found
                = std::find_if(mEntries.begin(), mEntries.end(), [&](const Entry& e) { return e.id == job.id; });
            if (!job.target || found == mEntries.end())
            {
                job.failed = true; // the array could not be reduced, or the texture is gone
                job.next = job.binds.size();
            }
            else
                found->reduced[job.toDrop] = job.target;
        }

        std::vector<std::pair<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>> replaced;
        int processed = 0;
        while (job.next < job.binds.size() && processed < maxBinds && !job.failed
            && (processed == 0 || millisecondsSince(stepBegin) < budgetMs))
        {
            Job::BindJob& work = job.binds[job.next++];
            ++processed;
            vsg::BindDescriptorSet& old = *work.old;
            const auto buildBegin = Clock::now();
            vsg::Descriptors descriptors = old.descriptorSet->descriptors;
            for (auto& descriptor : descriptors)
            {
                const vsg::DescriptorImage* oldImage = nullptr;
                vsg::Data* data = singleImageData(*descriptor, oldImage);
                const auto drop = data ? job.dataDrops.find(data) : job.dataDrops.end();
                if (drop == job.dataDrops.end() || drop->second == job.toDrop)
                    continue;
                auto cached = job.descriptors.find(oldImage);
                if (cached == job.descriptors.end())
                {
                    // One new image for every bind that held this descriptor.
                    const auto& info = oldImage->imageInfoList[0];
                    mRetiredImages.push_back({ vsg::observer_ptr<vsg::Object>(info->imageView->image.get()), job.id });
                    auto image = vsg::Image::create(job.target);
                    image->usage = info->imageView->image->usage;
                    auto view = vsg::ImageView::create(image);
                    auto newInfo = vsg::ImageInfo::create(info->sampler, view, info->imageLayout);
                    auto fresh = vsg::DescriptorImage::create(
                        newInfo, oldImage->dstBinding, oldImage->dstArrayElement, oldImage->descriptorType);
                    cached = job.descriptors.emplace(oldImage, fresh).first;
                    result.bytesBefore += TM::chainBytes(job.size, drop->second);
                    result.bytesAfter += TM::chainBytes(job.size, job.toDrop);
                    if (mHooks.descriptorReplaced)
                        mHooks.descriptorReplaced(oldImage, fresh);
                }
                descriptor = cached->second;
            }
            auto set = vsg::DescriptorSet::create(old.descriptorSet->setLayout, descriptors);
            auto bind = vsg::BindDescriptorSet::create(old.pipelineBindPoint, old.layout, old.firstSet, set);
            bind->slot = old.slot;
            result.buildMs += millisecondsSince(buildBegin);

            const auto compileBegin = Clock::now();
            if (mHooks.compile && !mHooks.compile(bind))
            {
                job.failed = true;
                break;
            }
            result.compileMs += millisecondsSince(compileBegin);
            ++result.binds;

            const auto replaceBegin = Clock::now();
            for (Job::Slot& slot : work.slots)
            {
                if (slot.choice)
                {
                    if (slot.index < slot.choice->children.size()
                        && slot.choice->children[slot.index].stateCommand == work.old)
                        slot.choice->children[slot.index].stateCommand = bind;
                    else
                        continue;
                }
                else
                {
                    if (slot.index < slot.group->stateCommands.size()
                        && slot.group->stateCommands[slot.index] == work.old)
                        slot.group->stateCommands[slot.index] = bind;
                    else
                        continue;
                }
                job.groupsTouched.insert(slot.group.get());
            }
            result.replaceMs += millisecondsSince(replaceBegin);
            replaced.emplace_back(work.old.get(), bind);
            mRetiredProbes.push_back({ vsg::observer_ptr<vsg::Object>(work.old.get()), job.id });
            if (mHooks.retire)
                mHooks.retire(work.old);
            work.old = nullptr;
            work.slots.clear();
        }
        if (mHooks.bindsReplaced && !replaced.empty())
            mHooks.bindsReplaced(replaced);

        if (job.failed || job.next >= job.binds.size())
        {
            result.groups = static_cast<int>(job.groupsTouched.size());
            result.ok = !job.failed;
            const auto found
                = std::find_if(mEntries.begin(), mEntries.end(), [&](const Entry& e) { return e.id == job.id; });
            if (found != mEntries.end())
            {
                found->changedBefore = true;
                found->lastChangeFrame = job.frame;
                for (User& user : found->users)
                    if (job.groupsTouched.contains(user.group.ref_ptr().get()))
                        user.drop = job.toDrop;
            }
            if (result.ok)
                setReduction(job.full.get(), job.toDrop > 0 ? job.target : vsg::ref_ptr<vsg::Data>());
            step.done = true;
            step.total = result;
            const bool wasArray = job.isArray;
            mJob.reset();
            if (wasArray && mHooks.afterArraySwap)
                mHooks.afterArraySwap();
            return step;
        }
        step.total = result;
        return step;
    }

    SwapResult Swapper::swap(std::uint64_t id, int toDrop, std::uint64_t frame)
    {
        if (!beginSwap(id, toDrop, frame))
            return {};
        if (mJob->pending.valid())
            mJob->pending.wait();
        StepResult step;
        do
            step = stepSwap(std::numeric_limits<int>::max(), 1.0e9);
        while (!step.done);
        return step.total;
    }
}
