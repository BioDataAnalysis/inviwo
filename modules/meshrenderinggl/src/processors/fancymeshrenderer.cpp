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

#include <modules/meshrenderinggl/processors/fancymeshrenderer.h>

#include <modules/opengl/geometry/meshgl.h>
#include <inviwo/core/common/inviwoapplication.h>
#include <inviwo/core/rendering/meshdrawerfactory.h>
#include <inviwo/core/util/stdextensions.h>
#include <modules/opengl/openglutils.h>
#include <modules/opengl/texture/textureutils.h>
#include <modules/opengl/shader/shaderutils.h>
#include <modules/base/algorithm/dataminmax.h>
#include <modules/opengl/image/layergl.h>
#include <modules/opengl/openglcapabilities.h>
#include <modules/opengl/rendering/meshdrawergl.h>

#include <sstream>
#include <chrono>
#include <variant>

#include <fmt/format.h>

namespace inviwo {

void configComposite(BoolCompositeProperty& comp) {

    auto callback = [&comp]() mutable {
        comp.setCollapsed(!comp.isChecked());
        for (auto p : comp) {
            if (p == comp.getBoolProperty()) continue;
            p->setReadOnly(!comp.isChecked());
        }
    };

    comp.getBoolProperty()->onChange(callback);
    callback();
}

// The Class Identifier has to be globally unique. Use a reverse DNS naming scheme
const ProcessorInfo FancyMeshRenderer::processorInfo_{
    "org.inviwo.FancyMeshRenderer",  // Class identifier
    "Fancy Mesh Renderer",           // Display name
    "Mesh Rendering",                // Category
    CodeState::Experimental,         // Code state
    Tags::GL,                        // Tags
};
const ProcessorInfo FancyMeshRenderer::getProcessorInfo() const { return processorInfo_; }

FancyMeshRenderer::FancyMeshRenderer()
    : Processor()
    , inport_("geometry")
    , imageInport_("imageInport")
    , outport_("image")
    , camera_("camera", "Camera", vec3(0.0f, 0.0f, 2.0f), vec3(0.0f, 0.0f, 0.0f),
              vec3(0.0f, 1.0f, 0.0f), &inport_)
    , trackball_(&camera_)
    , lightingProperty_("lighting", "Lighting", &camera_)
    , forceOpaque_("forceOpaque", "Shade Opaque", false, InvalidationLevel::InvalidResources)
    , drawSilhouette_("drawSilhouette", "Draw Silhouette", false,
                      InvalidationLevel::InvalidResources)
    , silhouetteColor_("silhouetteColor", "Silhouette Color", {0.f, 0.f, 0.f, 1.f})
    , normalSource_(
          "normalSource", "Normals Source",
          {
              {"inputVertex", "Input: Vertex Normal", NormalSource::InputVertex},
              {"generateVertex", "Generate: Vertex Normal", NormalSource::GenerateVertex},
              {"generateTriangle", "Generate: Triangle Normal", NormalSource::GenerateTriangle},
          },
          0, InvalidationLevel::InvalidResources)
    , normalComputationMode_(
          "normalComputationMode", "Normals Computation",
          {{"noWeighting", "No Weighting", meshutil::CalculateMeshNormalsMode::NoWeighting},
           {"area", "Area-weighting", meshutil::CalculateMeshNormalsMode::WeightArea},
           {"angle", "Angle-weighting", meshutil::CalculateMeshNormalsMode::WeightAngle},
           {"nmax", "Based on N.Max", meshutil::CalculateMeshNormalsMode::WeightNMax}},
          3, InvalidationLevel::InvalidResources)
    , faceSettings_{true, false}
    , meshHasAdjacency_(false)
    , shader_("fancymeshrenderer.vert", "fancymeshrenderer.geom", "fancymeshrenderer.frag", false) {

    // query OpenGL Capability
    supportsFragmentLists_ = FragmentListRenderer::supportsFragmentLists();
    supportesIllustration_ = FragmentListRenderer::supportsIllustration();
    if (!supportsFragmentLists_) {
        LogProcessorWarn(
            "Fragment lists are not supported by the hardware -> use blending without sorting, may "
            "lead to errors");
    }
    if (!supportesIllustration_) {
        LogProcessorWarn(
            "Illustration Buffer not supported by the hardware, screen-space silhouettes not "
            "available");
    }

    // input and output ports
    addPort(inport_).onChange([this]() { updateMeshes(); });
    addPort(imageInport_).setOptional(true);
    addPort(outport_);

    drawSilhouette_.onChange([this]() { updateMeshes(); });

    addProperties(camera_, lightingProperty_, trackball_, forceOpaque_, drawSilhouette_,
                  silhouetteColor_, illustrationSettings_.enabled_, normalSource_,
                  normalComputationMode_, alphaSettings_.container_, edgeSettings_.container_,
                  faceSettings_[0].show_, faceSettings_[1].show_);

    illustrationSettings_.enabled_.readonlyDependsOn(
        forceOpaque_, [this](const auto& prop) { return !supportesIllustration_ && prop.get(); });

    silhouetteColor_.visibilityDependsOn(drawSilhouette_, [](const auto& p) { return p.get(); });
    normalComputationMode_.visibilityDependsOn(
        normalSource_, [](const auto& p) { return p.get() == NormalSource::GenerateVertex; });

    alphaSettings_.container_.visibilityDependsOn(forceOpaque_,
                                                  [](const auto& p) { return !p.get(); });

    auto edgevis = [this](auto) {
        return drawSilhouette_.get() || faceSettings_[0].showEdges_.get() ||
               faceSettings_[1].showEdges_.get();
    };
    edgeSettings_.container_.visibilityDependsOn(drawSilhouette_, edgevis);
    edgeSettings_.container_.visibilityDependsOn(faceSettings_[0].showEdges_, edgevis);
    edgeSettings_.container_.visibilityDependsOn(faceSettings_[1].showEdges_, edgevis);

    camera_.setCollapsed(true);
    lightingProperty_.setCollapsed(true);
    trackball_.setCollapsed(true);

    silhouetteColor_.setSemantics(PropertySemantics::Color);

    faceSettings_[1].frontPart_ = &faceSettings_[0];

    shader_.onReload([this]() { invalidate(InvalidationLevel::InvalidOutput); });
    flrReload_ = flr_.onReload([this]() { invalidate(InvalidationLevel::InvalidResources); });
}

FancyMeshRenderer::AlphaSettings::AlphaSettings()
    : container_("alphaContainer", "Alpha")
    , enableUniform_("alphaUniform", "Uniform", true, InvalidationLevel::InvalidResources)
    , uniformScaling_("alphaUniformScaling", "Scaling", 0.5f, 0.f, 1.f, 0.01f)
    , enableAngleBased_("alphaAngleBased", "Angle-based", false,
                        InvalidationLevel::InvalidResources)
    , angleBasedExponent_("alphaAngleBasedExponent", "Exponent", 1.f, 0.f, 5.f, 0.01f)
    , enableNormalVariation_("alphaNormalVariation", "Normal variation", false,
                             InvalidationLevel::InvalidResources)
    , normalVariationExponent_("alphaNormalVariationExponent", "Exponent", 1.f, 0.f, 5.f, 0.01f)
    , enableDensity_("alphaDensity", "Density-based", false, InvalidationLevel::InvalidResources)
    , baseDensity_("alphaBaseDensity", "Base density", 1.f, 0.f, 2.f, 0.01f)
    , densityExponent_("alphaDensityExponent", "Exponent", 1.f, 0.f, 5.f, 0.01f)
    , enableShape_("alphaShape", "Shape-based", false, InvalidationLevel::InvalidResources)
    , shapeExponent_("alphaShapeExponent", "Exponent", 1.f, 0.f, 5.f, 0.01f) {
    container_.addProperties(enableUniform_, uniformScaling_, enableAngleBased_,
                             angleBasedExponent_, enableNormalVariation_, normalVariationExponent_,
                             enableDensity_, baseDensity_, densityExponent_, enableShape_,
                             shapeExponent_);

    const auto get = [](const auto& p) { return p.get(); };

    uniformScaling_.visibilityDependsOn(enableUniform_, get);
    angleBasedExponent_.visibilityDependsOn(enableAngleBased_, get);
    normalVariationExponent_.visibilityDependsOn(enableNormalVariation_, get);
    baseDensity_.visibilityDependsOn(enableDensity_, get);
    densityExponent_.visibilityDependsOn(enableDensity_, get);
    shapeExponent_.visibilityDependsOn(enableShape_, get);
}

void FancyMeshRenderer::AlphaSettings::setUniforms(Shader& shader, std::string_view prefix) const {
    std::array<std::pair<std::string_view, std::variant<float>>, 6> uniforms{
        {{"uniformScale", uniformScaling_},
         {"angleExp", angleBasedExponent_},
         {"normalExp", normalVariationExponent_},
         {"baseDensity", baseDensity_},
         {"densityExp", densityExponent_},
         {"shapeExp", shapeExponent_}}};

    for (const auto& [key, val] : uniforms) {
        std::visit([&, akey = key](
                       auto& aval) { shader.setUniform(fmt::format("{}{}", prefix, akey), aval); },
                   val);
    }
}

FancyMeshRenderer::EdgeSettings::EdgeSettings()
    : container_("edges", "Edges")
    , edgeThickness_("thickness", "Thickness", 2.f, 0.1f, 10.f, 0.1f)
    , depthDependent_("depth", "Depth dependent", false, InvalidationLevel::InvalidResources)
    , smoothEdges_("smooth", "Smooth edges", true, InvalidationLevel::InvalidResources) {
    container_.addProperties(edgeThickness_, depthDependent_, smoothEdges_);
}

FancyMeshRenderer::HatchingSettings::HatchingSettings()
    : hatching_("hatching", "Hatching Settings", false)
    , mode_("hatchingMode", "Hatching",
            {{"u", "U", HatchingMode::U},
             {"v", "V", HatchingMode::V},
             {"uv", "UV", HatchingMode::UV}},
            0, InvalidationLevel::InvalidResources)
    , steepness_("steepness", "Steepness", 5, 1, 10)
    , baseFrequencyU_("frequencyU", "U-Frequency", 3, 1, 10)
    , baseFrequencyV_("frequencyV", "V-Frequency", 3, 1, 10)
    , modulation_("modulation", "Modulation", false)
    , modulationMode_("modulationMode", "Modulation",
                      {{"u", "U", HatchingMode::U},
                       {"v", "V", HatchingMode::V},
                       {"uv", "UV", HatchingMode::UV}})
    , modulationAnisotropy_("modulationAnisotropy", "Anisotropy", 0.5f, -1.f, 1.f, 0.01f)
    , modulationOffset_("modulationOffset", "Offset", 0.f, 0.f, 1.f, 0.01f)
    , color_("color", "Color", {0.f, 0.f, 0.f})
    , strength_("strength", "Strength", 0.5f, 0.f, 1.f, 0.01f)
    , blendingMode_("blending", "Blending",
                    {{"mult", "Multiplicative", HatchingBlendingMode::Multiplicative},
                     {"add", "Additive", HatchingBlendingMode::Additive}}) {

    hatching_.getBoolProperty()->setInvalidationLevel(InvalidationLevel::InvalidResources);
    configComposite(hatching_);

    // init properties
    color_.setSemantics(PropertySemantics::Color);

    // add to container
    modulation_.addProperties(modulationMode_, modulationAnisotropy_, modulationOffset_);
    configComposite(modulation_);

    hatching_.addProperties(mode_, steepness_, baseFrequencyU_, baseFrequencyV_, modulation_,
                            color_, strength_, blendingMode_);

    baseFrequencyU_.visibilityDependsOn(
        mode_, [](const auto& prop) { return prop.get() != HatchingMode::V; });
    baseFrequencyV_.visibilityDependsOn(
        mode_, [](const auto& prop) { return prop.get() != HatchingMode::U; });

    modulation_.visibilityDependsOn(
        mode_, [](const auto& prop) { return prop.get() == HatchingMode::UV; });
}

FancyMeshRenderer::FaceSettings::FaceSettings(bool frontFace)
    : frontFace_(frontFace)
    , show_(frontFace ? "frontcontainer" : "backcontainer", frontFace ? "Front Face" : "Back Face",
            true)
    , sameAsFrontFace_("same", "Same as Front Face")
    , copyFrontToBack_("copy", "Copy Front to Back")
    , transferFunction_("tf", "Transfer Function")
    , externalColor_("extraColor", "Color", {1.f, 0.3f, 0.01f})
    , colorSource_("colorSource", "Color Source",
                   {{"vertexColor", "VertexColor", ColorSource::VertexColor},
                    {"tf", "Transfer Function", ColorSource::TransferFunction},
                    {"external", "Constant Color", ColorSource::ExternalColor}},
                   2, InvalidationLevel::InvalidResources)
    , separateUniformAlpha_("separateUniformAlpha", "Separate Uniform Alpha")
    , uniformAlpha_("uniformAlpha", "Uniform Alpha", 0.5f, 0.f, 1.f, 0.01f)
    , shadingMode_("shadingMode", "Shading Mode",
                   {
                       {"off", "Off", ShadingMode::Off},
                       {"phong", "Phong", ShadingMode::Phong},
                       {"pbr", "PBR", ShadingMode::Pbr},
                   })
    , showEdges_("showEdges", "Show Edges", false, InvalidationLevel::InvalidResources)
    , edgeColor_("edgeColor", "Edge color", {0.f, 0.f, 0.f})
    , edgeOpacity_("edgeOpacity", "Edge Opacity", 0.5f, 0.f, 2.f, 0.01f)
    , hatching_() {

    externalColor_.setSemantics(PropertySemantics::Color);
    edgeColor_.setSemantics(PropertySemantics::Color);

    if (!frontFace) {
        show_.addProperties(sameAsFrontFace_, copyFrontToBack_);
        copyFrontToBack_.onChange([this]() { copyFrontToBack(); });
    }
    show_.addProperties(colorSource_, transferFunction_, externalColor_, separateUniformAlpha_,
                        uniformAlpha_, shadingMode_, showEdges_, edgeColor_, edgeOpacity_,
                        hatching_.hatching_);

    configComposite(show_);

    const auto get = [](const auto& p) { return p.get(); };
    edgeColor_.visibilityDependsOn(showEdges_, get);
    edgeOpacity_.visibilityDependsOn(showEdges_, get);

    uniformAlpha_.visibilityDependsOn(separateUniformAlpha_,
                                      [](const auto& prop) { return prop.get(); });

    transferFunction_.visibilityDependsOn(
        colorSource_, [](const auto& prop) { return prop.get() == ColorSource::TransferFunction; });

    externalColor_.visibilityDependsOn(
        colorSource_, [](const auto& prop) { return prop.get() == ColorSource::ExternalColor; });
}

void FancyMeshRenderer::FaceSettings::copyFrontToBack() {
    for (auto src : frontPart_->show_) {
        if (auto dst = show_.getPropertyByIdentifier(src->getIdentifier())) {
            dst->set(src);
        }
    }
}

void FancyMeshRenderer::FaceSettings::setUniforms(Shader& shader, std::string_view prefix) const {

    std::array<std::pair<std::string_view, std::variant<bool, int, float, vec4>>, 15> uniforms{
        {{"externalColor", vec4{*externalColor_, 1.0f}},
         {"colorSource", static_cast<int>(*colorSource_)},
         {"separateUniformAlpha", separateUniformAlpha_},
         {"uniformAlpha", uniformAlpha_},
         {"shadingMode", static_cast<int>(*shadingMode_)},
         {"showEdges", showEdges_},
         {"edgeColor", vec4{*edgeColor_, *edgeOpacity_}},
         {"hatchingMode", (hatching_.mode_.get() == HatchingMode::UV)
                              ? 3 + static_cast<int>(hatching_.modulationMode_.get())
                              : static_cast<int>(hatching_.mode_.get())},
         {"hatchingSteepness", hatching_.steepness_.get()},
         {"hatchingFreqU", hatching_.baseFrequencyU_.getMaxValue() - hatching_.baseFrequencyU_},
         {"hatchingFreqV", hatching_.baseFrequencyV_.getMaxValue() - hatching_.baseFrequencyV_},
         {"hatchingModulationAnisotropy", hatching_.modulationAnisotropy_},
         {"hatchingModulationOffset", hatching_.modulationOffset_},
         {"hatchingColor", vec4(hatching_.color_.get(), hatching_.strength_.get())},
         {"hatchingBlending", static_cast<int>(hatching_.blendingMode_.get())}}};

    for (const auto& [key, val] : uniforms) {
        std::visit([&, akey = key](
                       auto aval) { shader.setUniform(fmt::format("{}{}", prefix, akey), aval); },
                   val);
    }
}

FancyMeshRenderer::IllustrationSettings::IllustrationSettings()
    : enabled_("illustration", "Use Illustration Effects", false)
    , edgeColor_("edgeColor", "Edge Color", vec3(0.f, 0.f, 0.f))
    , edgeStrength_("edgeStrength", "Edge Strength", 0.5f, 0.f, 1.f, 0.01f)
    , haloStrength_("haloStrength", "Halo Strength", 0.5f, 0.f, 1.f, 0.01f)
    , smoothingSteps_("smoothingSteps", "Smoothing Steps", 3, 0, 50, 1)
    , edgeSmoothing_("edgeSmoothing", "Edge Smoothing", 0.8f, 0.f, 1.f, 0.01f)
    , haloSmoothing_("haloSmoothing", "Halo Smoothing", 0.8f, 0.f, 1.f, 0.01f) {

    edgeColor_.setSemantics(PropertySemantics::Color);
    enabled_.addProperties(edgeColor_, edgeStrength_, haloStrength_, smoothingSteps_,
                           edgeSmoothing_, haloSmoothing_);

    configComposite(enabled_);
}

FragmentListRenderer::IllustrationSettings FancyMeshRenderer::IllustrationSettings::getSettings()
    const {
    return FragmentListRenderer::IllustrationSettings{
        edgeColor_.get(),      edgeStrength_.get(),  haloStrength_.get(),
        smoothingSteps_.get(), edgeSmoothing_.get(), haloSmoothing_.get(),
    };
}

void FancyMeshRenderer::initializeResources() {
    auto fso = shader_.getFragmentShaderObject();

    fso->addShaderExtension("GL_NV_gpu_shader5", true);
    fso->addShaderExtension("GL_EXT_shader_image_load_store", true);
    fso->addShaderExtension("GL_NV_shader_buffer_load", true);
    fso->addShaderExtension("GL_NV_shader_buffer_store", true);
    fso->addShaderExtension("GL_EXT_bindable_uniform", true);

    // shading defines
    utilgl::addShaderDefines(shader_, lightingProperty_);

    const std::array<std::pair<std::string, bool>, 15> defines = {
        {{"USE_FRAGMENT_LIST", !forceOpaque_},
         {"ALPHA_UNIFORM", alphaSettings_.enableUniform_},
         {"ALPHA_ANGLE_BASED", alphaSettings_.enableAngleBased_},
         {"ALPHA_NORMAL_VARIATION", alphaSettings_.enableNormalVariation_},
         {"ALPHA_DENSITY", alphaSettings_.enableDensity_},
         {"ALPHA_SHAPE", alphaSettings_.enableShape_},
         {"DRAW_EDGES", faceSettings_[0].showEdges_ || faceSettings_[1].showEdges_},
         {"DRAW_EDGES_DEPTH_DEPENDENT", edgeSettings_.depthDependent_},
         {"DRAW_EDGES_SMOOTHING", edgeSettings_.smoothEdges_},
         {"MESH_HAS_ADJACENCY", meshHasAdjacency_},
         {"DRAW_SILHOUETTE", drawSilhouette_},
         {"SEND_TEX_COORD", faceSettings_[0].hatching_.hatching_.isChecked() ||
                                faceSettings_[1].hatching_.hatching_.isChecked()},
         {"SEND_SCALAR", faceSettings_[0].colorSource_ == ColorSource::TransferFunction ||
                             faceSettings_[1].colorSource_ == ColorSource::TransferFunction},
         {"SEND_COLOR", faceSettings_[0].colorSource_ == ColorSource::VertexColor ||
                            faceSettings_[1].colorSource_ == ColorSource::VertexColor}}};

    for (auto&& [key, val] : defines) {
        for (auto&& so : shader_.getShaderObjects()) {
            so.setShaderDefine(key, val);
        }
    }

    shader_.build();
}

void FancyMeshRenderer::process() {
    utilgl::activateTargetAndClearOrCopySource(outport_, imageInport_);

    if (!faceSettings_[0].show_ && !faceSettings_[1].show_) {
        utilgl::deactivateCurrentTarget();
        return;  // everything is culled
    }
    const bool opaque = forceOpaque_.get();
    const bool fragmentLists = !opaque && supportsFragmentLists_;

    // Loop: fragment list may need another try if not enough space for the pixels was available
    bool retry = false;
    do {
        retry = false;

        if (fragmentLists) {
            // prepare fragment list rendering
            flr_.prePass(outport_.getDimensions());
        }

        shader_.activate();

        // general settings for camera, lighting, picking, mesh data
        utilgl::setUniforms(shader_, camera_, lightingProperty_);
        shader_.setUniform("halfScreenSize", ivec2(outport_.getDimensions()) / ivec2(2));

        // update face render settings
        std::array<TextureUnit, 2> transFuncUnit;
        for (size_t j = 0; j < faceSettings_.size(); ++j) {
            const std::string prefix = fmt::format("renderSettings[{}].", j);
            auto& face = faceSettings_[faceSettings_[1].sameAsFrontFace_.get() ? 0 : j];
            face.setUniforms(shader_, prefix);

            const Layer* tfLayer = face.transferFunction_->getData();
            const LayerGL* transferFunctionGL = tfLayer->getRepresentation<LayerGL>();
            transferFunctionGL->bindTexture(transFuncUnit[j].getEnum());
            shader_.setUniform(fmt::format("transferFunction{}", j),
                               transFuncUnit[j].getUnitNumber());
        }

        // update alpha settings
        alphaSettings_.setUniforms(shader_, "alphaSettings.");

        // update other global fragment shader settings
        shader_.setUniform("silhouetteColor", silhouetteColor_);

        // update geometry shader settings
        shader_.setUniform("geomSettings.edgeWidth", edgeSettings_.edgeThickness_);
        shader_.setUniform("geomSettings.triangleNormal",
                           normalSource_.get() == NormalSource::GenerateTriangle);

        if (fragmentLists) {
            flr_.setShaderUniforms(shader_);  // set uniforms fragment list rendering
        }

        {
            // various OpenGL states: depth, blending, culling
            utilgl::GlBoolState depthTest(GL_DEPTH_TEST, opaque);
            utilgl::DepthMaskState depthMask(opaque ? GL_TRUE : GL_FALSE);

            utilgl::CullFaceState culling(
                !faceSettings_[0].show_ && faceSettings_[1].show_
                    ? GL_FRONT
                    : faceSettings_[0].show_ && !faceSettings_[1].show_ ? GL_BACK : GL_NONE);
            utilgl::BlendModeState blendModeState(opaque ? GL_ONE : GL_SRC_ALPHA,
                                                  opaque ? GL_ZERO : GL_ONE_MINUS_SRC_ALPHA);

            // Finally, draw it
            for (auto mesh : enhancedMeshes_) {
                MeshDrawerGL::DrawObject drawer{mesh->getRepresentation<MeshGL>(),
                                                mesh->getDefaultMeshInfo()};
                utilgl::setShaderUniforms(shader_, *mesh, "geometry");
                shader_.setUniform("pickingEnabled", meshutil::hasPickIDBuffer(mesh.get()));

                drawer.draw();
            }
        }

        shader_.deactivate();

        if (fragmentLists) {
            // final processing of fragment list rendering
            const bool useIllustration =
                illustrationSettings_.enabled_.isChecked() && supportesIllustration_;
            if (useIllustration) {
                flr_.setIllustrationSettings(illustrationSettings_.getSettings());
            }
            retry = !flr_.postPass(useIllustration);
        }
    } while (retry);

    utilgl::deactivateCurrentTarget();
}

void FancyMeshRenderer::updateMeshes() {
    enhancedMeshes_.clear();
    for (auto mesh : inport_) {
        std::shared_ptr<Mesh> copy = nullptr;

        if (drawSilhouette_) {
            copy = std::make_shared<Mesh>(Mesh::DontCopyBuffers{}, *mesh);
            for (auto&& [info, buffer] : mesh->getBuffers()) {
                copy->addBuffer(info, std::shared_ptr<BufferBase>(buffer->clone()));
            }

            // create adjacency information
            const auto halfEdges = HalfEdges{*mesh};

            // add new index buffer with adjacency information
            copy->addIndices(
                {DrawType::Triangles, ConnectivityType::Adjacency},
                std::make_shared<IndexBuffer>(halfEdges.createIndexBufferWithAdjacency()));

            if (!meshHasAdjacency_) {
                meshHasAdjacency_ = true;
                initializeResources();
            }
        } else {
            if (meshHasAdjacency_) {
                meshHasAdjacency_ = false;
                initializeResources();
            }
        }

        if (normalSource_.get() == NormalSource::GenerateVertex) {
            if (!copy) copy = std::shared_ptr<Mesh>(mesh->clone());
            meshutil::calculateMeshNormals(*copy, normalComputationMode_);
        }

        enhancedMeshes_.push_back(copy ? copy : mesh);
    }
}

}  // namespace inviwo
