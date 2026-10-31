#include "cellmarker.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>

#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>

#include <components/misc/constants.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/sets.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>

#include <apps/opencs/view/render/mask.hpp>

#include "picking.hpp"
#include "scenehost.hpp"

namespace
{
    // The OSG editor's osgText character size, in pixels: its marker scaled to the screen.
    constexpr int sCharacterSize = 20;
    constexpr int sMargin = 2;
    // The default shader's unlit colour mode, as Render::DebugGeometry.
    constexpr uint32_t sUnlitColorMode = 3;

    QImage drawText(const std::string& text, bool cellExists)
    {
        QFont font;
        font.setPixelSize(sCharacterSize);
        const QString string = QString::fromStdString(text);
        const QFontMetrics metrics(font);
        QImage image(metrics.horizontalAdvance(string) + 2 * sMargin, metrics.height() + 2 * sMargin,
            QImage::Format_RGBA8888);
        // If cell exists then show black bounding box otherwise show red.
        image.fill(cellExists ? QColor(0, 0, 0) : QColor(255, 0, 0));
        QPainter painter(&image);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(image.rect(), Qt::AlignCenter, string);
        return image;
    }
}

CSVRender::CellMarkerTag::CellMarkerTag(CellMarker* marker)
    : TagBase(Mask_CellMarker)
    , mMarker(marker)
{
}

CSVRender::CellMarker* CSVRender::CellMarkerTag::getCellMarker() const
{
    return mMarker;
}

vsg::ref_ptr<vsg::Node> CSVRender::CellMarker::buildMarker() const
{
    const QImage text = drawText(std::to_string(mCoordinates.getX()) + "," + std::to_string(mCoordinates.getY()), mExists);

    auto image = vsg::ubvec4Array2D::create(static_cast<uint32_t>(text.width()), static_cast<uint32_t>(text.height()),
        vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
    for (int y = 0; y < text.height(); ++y)
        std::memcpy(static_cast<void*>(&image->at(0, static_cast<uint32_t>(y))), text.constScanLine(y),
            static_cast<size_t>(text.width()) * 4);

    Pipeline::Options options;
    options.shader = { {},
        Pipeline::modes({ Pipeline::Mode::DIFFUSE_MAP, Pipeline::Mode::VERTEX, Pipeline::Mode::NORMAL,
            Pipeline::Mode::TEXCOORD, Pipeline::Mode::COLOR }) };
    options.numUvSets = 1;
    options.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    options.colorMode = sUnlitColorMode;
    options.depthTest = false;
    options.depthWrite = false;
    options.cullMode = VK_CULL_MODE_NONE;

    const Pipeline::Builder& builder = *mHost.resources().builder;
    auto pipeline = builder.graphics->getOrCreate(options);
    auto texture = vsg::DescriptorImage::create(builder.createSampler(), image);
    auto bindTexture = vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout, Pipeline::TEXTURE_SET, vsg::Descriptors{ texture });

    const float halfWidth = text.width() / 2.f;
    const float halfHeight = text.height() / 2.f;
    auto vertices = vsg::vec3Array::create({ { -halfWidth, -halfHeight, 0 }, { halfWidth, -halfHeight, 0 },
        { -halfWidth, halfHeight, 0 }, { halfWidth, halfHeight, 0 } });
    auto normals = vsg::vec3Array::create(4, vsg::vec3(0, 0, 1));
    auto colours = vsg::vec4Array::create(4, vsg::vec4(1, 1, 1, 1));
    auto uvs = vsg::vec2Array::create({ { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } });

    auto draw = vsg::VertexDraw::create();
    draw->assignArrays(vsg::DataList{ vertices, normals, colours, uvs });
    draw->vertexCount = 4;
    draw->instanceCount = 1;

    auto state = vsg::StateGroup::create();
    state->stateCommands = { pipeline, bindTexture };
    state->addChild(draw);
    return state;
}

CSVRender::CellMarker::CellMarker(
    SceneHost& host, vsg::Group& cellNode, const CSMWorld::CellCoordinates& coordinates, bool cellExists)
    : mHost(host)
    , mCellNode(cellNode)
    , mTag(new CellMarkerTag(this))
    , mCoordinates(coordinates)
    , mExists(cellExists)
{
    const int cellSize = Constants::CellSizeInUnits;

    // Move marker to center of cell.
    mPosition = vsg::dvec3((mCoordinates.getX() * cellSize) + (cellSize / 2.0),
        (mCoordinates.getY() * cellSize) + (cellSize / 2.0), 0.0);

    mMarkerNode = vsg::MatrixTransform::create();
    mMarkerNode->addChild(buildMarker());
    // Any distance works for a bin with no sorting, and the overlay bin keeps it out of the depth sorted one.
    mMaskNode = SceneHost::maskNode(Mask_CellMarker, vsgUtil::createBinSetter(SceneHost::sBinOverlay, mMarkerNode));
    Picking::setTag(*mMaskNode, mTag);
    update();
    mHost.compileContext()->compile(mMaskNode);
    mCellNode.addChild(mMaskNode);
}

CSVRender::CellMarker::~CellMarker()
{
    auto& children = mCellNode.children;
    children.erase(std::remove(children.begin(), children.end(), mMaskNode), children.end());
    mHost.compileContext()->detach(mMaskNode);
}

void CSVRender::CellMarker::update()
{
    const double scale = mHost.worldPerPixel(mPosition);
    const vsg::dvec3 right = mHost.cameraRight() * scale;
    const vsg::dvec3 up = mHost.cameraUp() * scale;
    const vsg::dvec3 back = vsg::normalize(vsg::cross(right, up));
    mMarkerNode->matrix = vsg::dmat4(right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, back.x, back.y, back.z, 0,
        mPosition.x, mPosition.y, mPosition.z, 1);
}
