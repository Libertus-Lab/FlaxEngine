// Copyright (c) 2012-2022 Wojciech Figat. All rights reserved.

#include "RenderView.h"
#include "Engine/Level/Actors/Camera.h"
#include "Engine/Renderer/RenderList.h"
#include "Engine/Renderer/RendererPass.h"
#include "RenderBuffers.h"
#include "RenderTask.h"

void RenderView::Prepare(RenderContext& renderContext)
{
    ASSERT(renderContext.List && renderContext.Buffers);

    const float width = static_cast<float>(renderContext.Buffers->GetWidth());
    const float height = static_cast<float>(renderContext.Buffers->GetHeight());

    // Check if use TAA (need to modify the projection matrix)
    Float2 taaJitter;
    NonJitteredProjection = Projection;
    if (renderContext.List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing)
    {
        // Move to the next frame
        const int MaxSampleCount = 8;
        if (++TaaFrameIndex >= MaxSampleCount)
            TaaFrameIndex = 0;

        // Calculate jitter
        const float jitterSpread = renderContext.List->Settings.AntiAliasing.TAA_JitterSpread;
        const float jitterX = RendererUtils::TemporalHalton(TaaFrameIndex + 1, 2) * jitterSpread;
        const float jitterY = RendererUtils::TemporalHalton(TaaFrameIndex + 1, 3) * jitterSpread;
        taaJitter = Float2(jitterX * 2.0f / width, jitterY * 2.0f / height);

        // Modify projection matrix
        if (IsOrthographicProjection())
        {
            // TODO: jitter otho matrix in a proper way
        }
        else
        {
            Projection.Values[2][0] += taaJitter.X;
            Projection.Values[2][1] += taaJitter.Y;
        }

        // Update matrices
        Matrix::Invert(Projection, IP);
        Frustum.SetMatrix(View, Projection);
        Frustum.GetInvMatrix(IVP);
        CullingFrustum = Frustum;
    }
    else
    {
        TaaFrameIndex = 0;
        taaJitter = Float2::Zero;
    }

    renderContext.List->Init(renderContext);
    renderContext.LodProxyView = nullptr;

    PrepareCache(renderContext, width, height, taaJitter);
}

void RenderView::PrepareCache(RenderContext& renderContext, float width, float height, const Float2& temporalAAJitter)
{
    // The same format used by the Flax common shaders and postFx materials
    ViewInfo = Float4(1.0f / Projection.M11, 1.0f / Projection.M22, Far / (Far - Near), (-Far * Near) / (Far - Near) / Far);
    ScreenSize = Float4(width, height, 1.0f / width, 1.0f / height);

    TemporalAAJitter.Z = TemporalAAJitter.X;
    TemporalAAJitter.W = TemporalAAJitter.Y;
    TemporalAAJitter.X = temporalAAJitter.X;
    TemporalAAJitter.Y = temporalAAJitter.Y;

    // Ortho views have issues with screen size LOD culling
    const float modelLODDistanceFactor = (renderContext.LodProxyView ? renderContext.LodProxyView->IsOrthographicProjection() : IsOrthographicProjection()) ? 100.0f : ModelLODDistanceFactor;
    ModelLODDistanceFactorSqrt = modelLODDistanceFactor * modelLODDistanceFactor;
}

void RenderView::SetUp(const Matrix& view, const Matrix& projection)
{
    // Copy data
    Projection = projection;
    NonJitteredProjection = projection;
    View = view;
    Matrix::Invert(View, IV);
    Matrix::Invert(Projection, IP);

    // Compute matrix
    Matrix viewProjection;
    Matrix::Multiply(View, Projection, viewProjection);
    Matrix::Invert(viewProjection, IVP);
    Frustum.SetMatrix(viewProjection);
    CullingFrustum = Frustum;
}

void RenderView::SetUpCube(float nearPlane, float farPlane, const Float3& position)
{
    // Copy data
    Near = nearPlane;
    Far = farPlane;
    Position = position;

    // Create projection matrix
    Matrix::PerspectiveFov(PI_OVER_2, 1.0f, nearPlane, farPlane, Projection);
    NonJitteredProjection = Projection;
    Matrix::Invert(Projection, IP);
}

void RenderView::SetFace(int32 faceIndex)
{
    static Float3 directions[6] =
    {
        { Float3::Right },
        { Float3::Left },
        { Float3::Up },
        { Float3::Down },
        { Float3::Forward },
        { Float3::Backward },
    };
    static Float3 ups[6] =
    {
        { Float3::Up },
        { Float3::Up },
        { Float3::Backward },
        { Float3::Forward },
        { Float3::Up },
        { Float3::Up },
    };
    ASSERT(faceIndex >= 0 && faceIndex < 6);

    // Create view matrix
    Direction = directions[faceIndex];
    Matrix::LookAt(Position, Position + Direction, ups[faceIndex], View);
    Matrix::Invert(View, IV);

    // Compute frustum matrix
    Frustum.SetMatrix(View, Projection);
    Matrix::Invert(ViewProjection(), IVP);
    CullingFrustum = Frustum;
}

void RenderView::SetProjector(float nearPlane, float farPlane, const Float3& position, const Float3& direction, const Float3& up, float angle)
{
    // Copy data
    Near = nearPlane;
    Far = farPlane;
    Position = position;

    // Create projection matrix
    Matrix::PerspectiveFov(angle * DegreesToRadians, 1.0f, nearPlane, farPlane, Projection);
    NonJitteredProjection = Projection;
    Matrix::Invert(Projection, IP);

    // Create view matrix
    Direction = direction;
    Matrix::LookAt(Position, Position + Direction, up, View);
    Matrix::Invert(View, IV);

    // Compute frustum matrix
    Frustum.SetMatrix(View, Projection);
    Matrix::Invert(ViewProjection(), IVP);
    CullingFrustum = Frustum;
}

void RenderView::CopyFrom(Camera* camera)
{
    Position = camera->GetPosition();
    Direction = camera->GetDirection();
    Near = camera->GetNearPlane();
    Far = camera->GetFarPlane();
    View = camera->GetView();
    Projection = camera->GetProjection();
    NonJitteredProjection = Projection;
    Frustum = camera->GetFrustum();
    Matrix::Invert(View, IV);
    Matrix::Invert(Projection, IP);
    Frustum.GetInvMatrix(IVP);
    CullingFrustum = Frustum;
    RenderLayersMask = camera->RenderLayersMask;
}

void RenderView::CopyFrom(Camera* camera, Viewport* viewport)
{
    Position = camera->GetPosition();
    Direction = camera->GetDirection();
    Near = camera->GetNearPlane();
    Far = camera->GetFarPlane();
    View = camera->GetView();
    camera->GetMatrices(View, Projection, *viewport);
    Frustum.SetMatrix(View, Projection);
    NonJitteredProjection = Projection;
    Matrix::Invert(View, IV);
    Matrix::Invert(Projection, IP);
    Frustum.GetInvMatrix(IVP);
    CullingFrustum = Frustum;
    RenderLayersMask = camera->RenderLayersMask;
}

DrawPass RenderView::GetShadowsDrawPassMask(ShadowsCastingMode shadowsMode) const
{
    switch (shadowsMode)
    {
    case ShadowsCastingMode::All:
        return DrawPass::All;
    case ShadowsCastingMode::DynamicOnly:
        return IsOfflinePass ? ~DrawPass::Depth : DrawPass::All;
    case ShadowsCastingMode::StaticOnly:
        return IsOfflinePass ? DrawPass::All : ~DrawPass::Depth;
    case ShadowsCastingMode::None:
        return ~DrawPass::Depth;
    default:
        return DrawPass::All;
    }
}
