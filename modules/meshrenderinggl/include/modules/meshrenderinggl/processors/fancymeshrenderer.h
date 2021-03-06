/*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 *
 * Copyright (c) 2019-2020 Inviwo Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************************/

#pragma once

#include <modules/meshrenderinggl/meshrenderingglmoduledefine.h>
#include <modules/meshrenderinggl/datastructures/halfedges.h>
#include <inviwo/core/common/inviwo.h>
#include <inviwo/core/processors/processor.h>
#include <inviwo/core/properties/ordinalproperty.h>
#include <inviwo/core/ports/imageport.h>
#include <inviwo/core/interaction/cameratrackball.h>
#include <inviwo/core/ports/meshport.h>
#include <inviwo/core/properties/boolproperty.h>
#include <inviwo/core/properties/buttonproperty.h>
#include <inviwo/core/properties/cameraproperty.h>
#include <inviwo/core/properties/compositeproperty.h>
#include <inviwo/core/properties/boolcompositeproperty.h>
#include <inviwo/core/properties/optionproperty.h>
#include <inviwo/core/properties/transferfunctionproperty.h>
#include <inviwo/core/properties/simplelightingproperty.h>
#include <inviwo/core/rendering/meshdrawer.h>
#include <modules/opengl/shader/shader.h>

#include <modules/meshrenderinggl/rendering/fragmentlistrenderer.h>
#include <modules/meshrenderinggl/algorithm/calcnormals.h>

#include <string_view>

namespace inviwo {

/** \docpage{org.inviwo.FancyMeshRenderer, Fancy Mesh Renderer}
 * ![](org.inviwo.FancyMeshRenderer.png?classIdentifier=org.inviwo.FancyMeshRenderer)
 * Mesh Renderer specialized for rendering highly layered and transparent surfaces.
 * Example usages: stream surfaces, isosurfaces, separatrices.
 *
 * Fragment lists are used to render the transparent pixels with correct alpha blending.
 * Many different alpha modes, shading modes, coloring modes are available.
 *
 * ### Inports
 *   * __geometry__ Input meshes
 *   * __imageInport__ Optional background image
 *
 * ### Outports
 *   * __image__ output image containing the rendered mesh and the optional input image
 *
 * ### Properties
 *   * __Camera__ Camera used for rendering the mesh
 *   * __Lighting__ Standard lighting settings
 *   * __Trackball__ Standard trackball settings
 *   * __Shade Opaque__ Draw the mesh opaquly instead of transparent. Disables all transparency
 * settings
 *   * __Alpha__ Assemble construction of the alpha value out of many factors (which are summed up)
 *       + __Uniform__ uniform alpha value
 *       + __Angle-based__ based on the angle between the pixel normal and the direction to the
 * camera
 *       + __Normal variation__ based on the variation (norm of the derivative) of the pixel normal
 *       + __Density-based__ based on the size of the triangle / density of the smoke volume inside
 * the triangle
 *       + __Shape-based__ based on the shape of the triangle. The more stretched, the more
 * transparent
 *   * __Edges__ Settings for the display of triangle edges
 *       + __Thickness__ The thickness of the edges
 *       + __Depth dependent__ If checked, the thickness also depends on the depth.
 *           If unchecked, every edge has the same size in screen space regardless of the distance
 * to the camera
 *       + __Smooth edges__ If checked, a simple anti-alising is used
 *   * __Front Face__ Settings for the front face
 *       + __Show__ Shows or hides that face (culling)
 *       + __Color Source__ The source of the color: vertex color, transfer function, or external
 * constant color
 *       + __Separate Uniform Alpha__ Overwrite alpha settings from above with a constant alpha
 * value
 *       + __Normal Source__ Source of the pixel normal: interpolated or not
 *       + __Shading Mode__ The shading that is applied to the pixel color
 *       + __Show Edges__ Show triangle edges
 *       + __Edge Color__ The color of the edges
 *       + __Edge Opacity__ Blending of the edge color:
 *           0-1: blending factor of the edge color into the triangle color, alpha unmodified;
 *           1-2: full edge color and alpha is increased to fully opaque
 *   * __Back Face__ Settings for the back face
 *       + __Show__ Shows or hides that face (culling)
 *       + __Same as front face__ use the settings from the front face, disables all other settings
 * for the back face
 *       + __Copy Front to Back__ Copies all settings from the front face to the back face
 *       + __Color Source__ The source of the color: vertex color, transfer function, or external
 * constant color
 *       + __Separate Uniform Alpha__ Overwrite alpha settings from above with a constant alpha
 * value
 *       + __Normal Source__ Source of the pixel normal: interpolated or not
 *       + __Shading Mode__ The shading that is applied to the pixel color
 *       + __Show Edges__ Show triangle edges
 *       + __Edge Color__ The color of the edges
 *       + __Edge Opacity__ Blending of the edge color:
 *           0-1: blending factor of the edge color into the triangle color, alpha unmodified;
 *           1-2: full edge color and alpha is increased to fully opaque
 */

/**
 * \class FancyMeshRenderer
 * \brief Mesh Renderer specialized for rendering highly layered and transparent surfaces.
 *
 * It uses the FragmentListRenderer for the rendering of the transparent mesh.
 * Many alpha computation modes, shading modes, color modes can be combined
 * and even selected individually for the front- and back face.
 */
class IVW_MODULE_MESHRENDERINGGL_API FancyMeshRenderer : public Processor {
public:
    FancyMeshRenderer();
    virtual ~FancyMeshRenderer() = default;

    virtual const ProcessorInfo getProcessorInfo() const override;
    static const ProcessorInfo processorInfo_;

    virtual void initializeResources() override;
    /**
     * \brief Performs the rendering.
     */
    virtual void process() override;

protected:
    /**
     * \brief Update the mesh drawer.
     * This is called when the inport is changed or when a property requires preprocessing steps on
     * the mesh, e.g. for silhouettes or special alpha features.
     */
    void updateMeshes();

    MeshFlatMultiInport inport_;
    ImageInport imageInport_;
    ImageOutport outport_;

    CameraProperty camera_;
    CameraTrackball trackball_;
    SimpleLightingProperty lightingProperty_;

    BoolProperty forceOpaque_;

    BoolProperty drawSilhouette_;
    FloatVec4Property silhouetteColor_;

    enum class NormalSource : int { InputVertex, GenerateVertex, GenerateTriangle };
    TemplateOptionProperty<NormalSource> normalSource_;
    TemplateOptionProperty<meshutil::CalculateMeshNormalsMode> normalComputationMode_;

    /**
     * \brief Settings to assemble the equation for the alpha values.
     * All individual factors are clamped to [0,1].
     */
    struct AlphaSettings {
        AlphaSettings();

        CompositeProperty container_;
        BoolProperty enableUniform_;
        FloatProperty uniformScaling_;
        // IRIS
        BoolProperty enableAngleBased_;
        FloatProperty angleBasedExponent_;
        BoolProperty enableNormalVariation_;
        FloatProperty normalVariationExponent_;
        // Smoke surfaces
        BoolProperty enableDensity_;
        FloatProperty baseDensity_;  // k in the paper
        FloatProperty densityExponent_;
        BoolProperty enableShape_;
        FloatProperty shapeExponent_;  // s in the paper

        void setUniforms(Shader& shader, std::string_view prefix) const;
    };
    AlphaSettings alphaSettings_;

    /**
     * \brief Settings controlling how edges are highlighted.
     */
    struct EdgeSettings {
        EdgeSettings();
        CompositeProperty container_;
        FloatProperty edgeThickness_;
        BoolProperty depthDependent_;
        BoolProperty smoothEdges_;
    };
    EdgeSettings edgeSettings_;

    enum class ColorSource : int { VertexColor, TransferFunction, ExternalColor };
    enum class ShadingMode : int {
        Off,  // no light, no reflection, just diffuse
        Phong,
        Pbr
    };
    enum class HatchingMode : char { U, V, UV };
    enum class HatchingBlendingMode : char { Multiplicative, Additive };
    /**
     * \brief Hatching settings. These are exactly the parameters from the IRIS-paper
     */
    struct HatchingSettings {
        HatchingSettings();
        BoolCompositeProperty hatching_;

        TemplateOptionProperty<HatchingMode> mode_;
        IntProperty steepness_;
        IntProperty baseFrequencyU_;
        IntProperty baseFrequencyV_;

        BoolCompositeProperty modulation_;
        TemplateOptionProperty<HatchingMode> modulationMode_;
        FloatProperty modulationAnisotropy_;
        FloatProperty modulationOffset_;
        FloatVec3Property color_;
        FloatProperty strength_;
        TemplateOptionProperty<HatchingBlendingMode> blendingMode_;
    };
    /**
     * \brief The render settings per face.
     * faceSettings_[0]=front face, faceSettings_[1]=back face
     */
    struct FaceSettings {
        FaceSettings(bool frontFace);

        bool frontFace_;
        BoolCompositeProperty show_;
        BoolProperty sameAsFrontFace_;
        ButtonProperty copyFrontToBack_;

        TransferFunctionProperty transferFunction_;
        FloatVec3Property externalColor_;
        TemplateOptionProperty<ColorSource> colorSource_;

        BoolProperty separateUniformAlpha_;
        FloatProperty uniformAlpha_;
        TemplateOptionProperty<ShadingMode> shadingMode_;

        BoolProperty showEdges_;
        FloatVec3Property edgeColor_;
        FloatProperty edgeOpacity_;

        HatchingSettings hatching_;

        // to copy front to back:
        FaceSettings* frontPart_;

        void copyFrontToBack();

        void setUniforms(Shader& shader, std::string_view prefix) const;

        bool lastOpaque_;
    };

    std::array<FaceSettings, 2> faceSettings_;

    struct IllustrationSettings {
        IllustrationSettings();

        BoolCompositeProperty enabled_;
        FloatVec3Property edgeColor_;
        FloatProperty edgeStrength_;
        FloatProperty haloStrength_;
        IntProperty smoothingSteps_;
        FloatProperty edgeSmoothing_;
        FloatProperty haloSmoothing_;

        FragmentListRenderer::IllustrationSettings getSettings() const;
    };
    IllustrationSettings illustrationSettings_;

    std::vector<std::shared_ptr<const Mesh>> enhancedMeshes_;

    /**
     * \brief This flag is set to true if adjacency information is available in the shader.
     */
    bool meshHasAdjacency_;

    FragmentListRenderer flr_;
    typename Dispatcher<void()>::Handle flrReload_;
    bool supportsFragmentLists_;
    bool supportesIllustration_;

    Shader shader_;
};

}  // namespace inviwo
