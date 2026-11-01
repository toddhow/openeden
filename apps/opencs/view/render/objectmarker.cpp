#include "objectmarker.hpp"

#include <algorithm>
#include <stdexcept>

#include <vsg/maths/transform.h>

#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>

#include "../../model/prefs/state.hpp"
#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>

#include "colouredgeometry.hpp"
#include "mask.hpp"
#include "picking.hpp"
#include "selectionmarkergeometry.hpp"
#include "worldspacewidget.hpp"

namespace Geometry = CSVRender::SelectionMarkerGeometry;

namespace CSVRender
{
    ObjectMarkerTag::ObjectMarkerTag(CSVRender::Object* object, int axis, std::string node)
        : ObjectTag(object)
        , mAxis(axis)
        , mNode(std::move(node))
    {
    }

    ObjectMarker::ObjectMarker(WorldspaceWidget* worldspaceWidget)
        : mWorldspaceWidget(worldspaceWidget)
        , mMarkerScale(static_cast<float>(CSMPrefs::get()["Rendering"]["object-marker-scale"].toDouble()))
        , mSubMode(Object::Mode_None)
    {
    }

    ObjectMarker::~ObjectMarker()
    {
        if (!mHost)
            return;
        mHost->removeListener(*this);
        auto& children = mHost->contentRoot().children;
        children.erase(
            std::remove(children.begin(), children.end(), vsg::ref_ptr<vsg::Node>(mRootNode)), children.end());
        mHost->compileContext()->detach(mRootNode);
    }

    void ObjectMarker::addParts(vsg::Switch& modes, int mode)
    {
        const auto parts = mode == Object::Mode_Rotate
            ? Geometry::rotateParts()
            : (mode == Object::Mode_Scale ? Geometry::scaleParts() : Geometry::moveParts());
        auto group = vsg::Group::create();
        const Pipeline::Builder& builder = *mHost->resources().builder;
        for (const auto& source : parts)
        {
            Part part;
            part.mName = source.mName;
            part.mAxis = source.mAxis;
            part.mAlpha = source.mAlpha;
            part.mVertices = vsg::vec3Array::create(static_cast<uint32_t>(source.mVertices.size()));
            std::copy(source.mVertices.begin(), source.mVertices.end(), part.mVertices->begin());
            part.mColours = vsg::vec4Array::create(static_cast<uint32_t>(source.mVertices.size()),
                Geometry::axisColour(source.mAxis, false, source.mAlpha));
            part.mColours->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
            if (mode == Object::Mode_Rotate)
            {
                part.mFullVertices = source.mVertices;
                part.mVertices->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
                mClipped.resize(part.mFullVertices.size());
            }
            part.mTag = vsg::ref_ptr{ new ObjectMarkerTag(nullptr, source.mAxis, source.mName) };

            ColouredOptions options;
            options.mNoDepth = true;
            options.mBlend = source.mAlpha;
            options.mTwoSided = source.mAlpha;
            auto node = createColouredGeometry(builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, part.mVertices,
                part.mColours, vsg::ref_ptr<vsg::Data>(), options);
            Picking::setTag(*node, part.mTag);
            group->addChild(node);
            mParts.push_back(std::move(part));
        }
        modes.addChild(mode == Object::Mode_Move, group);
    }

    void ObjectMarker::setHost(SceneHost& host)
    {
        mHost = &host;
        mModes = vsg::Switch::create();
        // In the order of Object::SubMode.
        addParts(*mModes, Object::Mode_Move);
        addParts(*mModes, Object::Mode_Rotate);
        addParts(*mModes, Object::Mode_Scale);

        mTransform = vsg::MatrixTransform::create();
        mTransform->addChild(mModes);
        mRootNode = vsg::Switch::create();
        mRootNode->addChild(vsg::MASK_OFF, vsgUtil::createBinSetter(SceneHost::sBinOverlay, mTransform));
        mHost->compileContext()->compile(mRootNode);
        mHost->contentRoot().addChild(mRootNode);
        mHost->addListener(*this);
        if (mSubMode != Object::Mode_None)
        {
            const int subMode = mSubMode;
            mSubMode = Object::Mode_None;
            setSubMode(subMode);
        }
    }

    void ObjectMarker::updateMask()
    {
        if (!mRootNode)
            return;
        const bool shown = isAttached() && mUserVisible && !mSuppressed;
        mRootNode->children[0].mask = shown ? vsg::Mask(Mask_Reference) : vsg::MASK_OFF;
        mHost->requestRedraw();
    }

    void ObjectMarker::toggleVisibility()
    {
        mUserVisible = !mUserVisible;
        updateMask();
    }

    void ObjectMarker::setSuppressed(bool suppressed)
    {
        mSuppressed = suppressed;
        updateMask();
    }

    void ObjectMarker::updateScale(const float scale)
    {
        mMarkerScale = scale;
        if (mHost)
            mHost->requestRedraw();
    }

    void ObjectMarker::updateRings(const vsg::vec3& eye)
    {
        if (eye == mLastRingEye)
            return;
        mLastRingEye = eye;
        for (auto& part : mParts)
        {
            if (part.mFullVertices.empty())
                continue;
            Geometry::clipRing(part.mFullVertices, eye, mClipped);
            std::copy(mClipped.begin(), mClipped.end(), part.mVertices->begin());
            part.mVertices->dirty();
        }
        mHost->requestRedraw();
    }

    void ObjectMarker::frameUpdate(float /*dt*/)
    {
        if (!isAttached())
            return;

        Object* object = mWorldspaceWidget->getObjectByReferenceId(mAttached);
        if (!object)
        {
            removeFromSelectionHistory(mAttached);
            detachMarker();
            return;
        }

        const ESM::Position position = object->getPosition();
        const vsg::dvec3 markerPosition(position.pos[0], position.pos[1], position.pos[2]);
        const vsg::dvec3 relativeEye = mHost->getCameraEye() - markerPosition;
        const double scale = Geometry::distanceScale(static_cast<float>(vsg::length(relativeEye))) * mMarkerScale;

        // The eye in the marker's own axes, as SceneWidget::update read it.
        const double sign = mFlipped ? -1.0 : 1.0;
        vsg::vec3 local(vsg::dvec3(relativeEye.x * sign, relativeEye.y * sign, relativeEye.z) / scale);
        if (Geometry::cameraWantsFlip(local))
        {
            mFlipped = !mFlipped;
            local.x = -local.x;
            local.y = -local.y;
        }
        const double flip = mFlipped ? -1.0 : 1.0;

        const vsg::dmat4 matrix(scale * flip, 0, 0, 0, 0, scale * flip, 0, 0, 0, 0, scale, 0, markerPosition.x,
            markerPosition.y, markerPosition.z, 1);
        if (matrix != mTransform->matrix)
        {
            mTransform->matrix = matrix;
            mHost->requestRedraw();
        }

        if (mSubMode == Object::Mode_Rotate)
            updateRings(local);
    }

    bool ObjectMarker::hitBehindMarker(const vsg::dvec3& hitPos) const
    {
        if (mSubMode != Object::Mode_Rotate || !mHost)
            return false;
        const vsg::dvec3 markerPosition(mTransform->matrix[3][0], mTransform->matrix[3][1], mTransform->matrix[3][2]);
        const vsg::dvec3 forward = vsg::normalize(mHost->getCameraCenter() - mHost->getCameraEye());
        return Geometry::hitBehind(hitPos, markerPosition, forward);
    }

    std::optional<Picking::Result> ObjectMarker::pick(
        const double x, const double y, const double widthPixels, const double heightPixels) const
    {
        if (!mHost || !mRootNode || !isAttached() || mRootNode->children[0].mask == vsg::MASK_OFF)
            return std::nullopt;
        return Picking::pickNear(*mRootNode, mHost->getProjectionMatrix(), mHost->getViewMatrix(), x, y, widthPixels,
            heightPixels, sPickRadiusPixels, vsg::Mask(Mask_Reference), [this](const Picking::Result& result) {
                return dynamic_cast<ObjectMarkerTag*>(result.mTag.get()) != nullptr
                    && !hitBehindMarker(result.mWorldPos);
            });
    }

    void ObjectMarker::setSubMode(const int subMode)
    {
        if (subMode == mSubMode)
            return;
        mSubMode = subMode;
        if (mModes)
        {
            for (std::size_t i = 0; i < mModes->children.size(); ++i)
                mModes->children[i].mask = static_cast<int>(i) == subMode ? vsg::MASK_ALL : vsg::MASK_OFF;
            mLastRingEye = vsg::vec3();
        }
        resetMarkerHighlight();
        updateSelectionMarker();
    }

    bool ObjectMarker::attachMarker(const std::string& refId)
    {
        CSVRender::Object* object = mWorldspaceWidget->getObjectByReferenceId(refId);

        if (!object)
            removeFromSelectionHistory(refId);

        if (!object || !object->getSelected())
            return false;

        mAttached = refId;
        for (auto& part : mParts)
            part.mTag->mObject = object;
        updateMask();
        return true;
    }

    void ObjectMarker::detachMarker()
    {
        mAttached.clear();
        for (auto& part : mParts)
            part.mTag->mObject = nullptr;
        updateMask();
    }

    void ObjectMarker::addToSelectionHistory(const std::string& refId, bool update)
    {
        auto foundObject = std::find(mSelectionHistory.begin(), mSelectionHistory.end(), refId);

        if (foundObject == mSelectionHistory.end())
            mSelectionHistory.push_back(refId);
        else
            std::rotate(foundObject, foundObject + 1, mSelectionHistory.end());

        if (update)
            updateSelectionMarker(refId);
    }

    void ObjectMarker::removeFromSelectionHistory(const std::string& refId)
    {
        mSelectionHistory.erase(
            std::remove(mSelectionHistory.begin(), mSelectionHistory.end(), refId), mSelectionHistory.end());
    }

    void ObjectMarker::updateSelectionMarker(const std::string& refId)
    {
        if (mSelectionHistory.empty())
            return;

        detachMarker();

        if (refId.empty())
        {
            for (std::size_t index = mSelectionHistory.size(); index > 0;)
                if (attachMarker(mSelectionHistory[--index]))
                    break;
        }
        else
            attachMarker(refId);
    }

    void ObjectMarker::applyColours()
    {
        for (auto& part : mParts)
        {
            const vsg::vec4 colour = Geometry::axisColour(part.mAxis, part.mAxis == mHighlightAxis, part.mAlpha);
            std::fill(part.mColours->begin(), part.mColours->end(), colour);
            part.mColours->dirty();
        }
        if (mHost)
            mHost->requestRedraw();
    }

    void ObjectMarker::resetMarkerHighlight()
    {
        if (mHighlightAxis == -1)
            return;
        mHighlightAxis = -1;
        mLastHitNode.clear();
        applyColours();
    }

    void ObjectMarker::updateMarkerHighlight(const std::string_view hitNode, const int axis)
    {
        if (hitNode == mLastHitNode)
            return;

        if (axis < Object::Axis_X || axis > Object::Axis_Z)
            throw std::runtime_error("Invalid axis for highlighting: " + std::to_string(axis));

        mHighlightAxis = axis;
        mLastHitNode = hitNode;
        applyColours();
    }
}
