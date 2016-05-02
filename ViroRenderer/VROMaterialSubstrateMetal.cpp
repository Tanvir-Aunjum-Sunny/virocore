//
//  VROMaterialSubstrateMetal.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/30/15.
//  Copyright © 2015 Viro Media. All rights reserved.
//

#include "VROMaterialSubstrateMetal.h"
#include "VROSharedStructures.h"
#include "VROMetalUtils.h"
#include "VRODriverMetal.h"
#include "VROMatrix4f.h"
#include "VROLight.h"
#include "VROMath.h"
#include "VROAllocationTracker.h"
#include "VROConcurrentBuffer.h"
#include "VRORenderParameters.h"

VROMaterialSubstrateMetal::VROMaterialSubstrateMetal(VROMaterial &material,
                                                     const VRODriverMetal &context) :
    _material(material),
    _lightingModel(material.getLightingModel()) {

    id <MTLDevice> device = context.getDevice();
    id <MTLLibrary> library = context.getLibrary();
    
    _lightingUniformsBuffer = new VROConcurrentBuffer(sizeof(VROSceneLightingUniforms), @"VROSceneLightingUniformBuffer", device);
    _materialUniformsBuffer = new VROConcurrentBuffer(sizeof(VROMaterialUniforms), @"VROMaterialUniformBuffer", device);
    
    switch (material.getLightingModel()) {
        case VROLightingModel::Constant:
            loadConstantLighting(material, library, device, context);
            break;
            
        case VROLightingModel::Blinn:
            loadBlinnLighting(material, library, device, context);
            break;
            
        case VROLightingModel::Lambert:
            loadLambertLighting(material, library, device, context);
            break;
            
        case VROLightingModel::Phong:
            loadPhongLighting(material, library, device, context);
            break;
            
        default:
            break;
    }
        
    ALLOCATION_TRACKER_ADD(MaterialSubstrates, 1);
}

VROMaterialSubstrateMetal::~VROMaterialSubstrateMetal() {
    delete (_materialUniformsBuffer);
    delete (_lightingUniformsBuffer);
    
    ALLOCATION_TRACKER_SUB(MaterialSubstrates, 1);
}

void VROMaterialSubstrateMetal::loadConstantLighting(VROMaterial &material,
                                                     id <MTLLibrary> library, id <MTLDevice> device,
                                                     const VRODriverMetal &context) {
    

    _vertexProgram   = [library newFunctionWithName:@"constant_lighting_vertex"];
    VROMaterialVisual &diffuse = material.getDiffuse();

    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        _fragmentProgram = [library newFunctionWithName:@"constant_lighting_fragment_c"];
    }
    else if (diffuse.getContentsType() == VROContentsType::Texture2D) {
        _textures.push_back(diffuse.getContentsTexture());
        _fragmentProgram = [library newFunctionWithName:@"constant_lighting_fragment_t"];
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        _fragmentProgram = [library newFunctionWithName:@"constant_lighting_fragment_q"];
    }
}

void VROMaterialSubstrateMetal::loadLambertLighting(VROMaterial &material,
                                                    id <MTLLibrary> library, id <MTLDevice> device,
                                                    const VRODriverMetal &context) {
    
    _vertexProgram   = [library newFunctionWithName:@"lambert_lighting_vertex"];
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            _fragmentProgram = [library newFunctionWithName:@"lambert_lighting_fragment_c_reflect"];
        }
        else {
            _fragmentProgram = [library newFunctionWithName:@"lambert_lighting_fragment_c"];
        }
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            _fragmentProgram = [library newFunctionWithName:@"lambert_lighting_fragment_t_reflect"];
        }
        else {
            _fragmentProgram = [library newFunctionWithName:@"lambert_lighting_fragment_t"];
        }
    }
}

void VROMaterialSubstrateMetal::loadPhongLighting(VROMaterial &material,
                                                  id <MTLLibrary> library, id <MTLDevice> device,
                                                  const VRODriverMetal &context) {
    
    /*
     If there's no specular map, then we fall back to Lambert lighting.
     */
    VROMaterialVisual &specular = material.getSpecular();
    if (specular.getContentsType() != VROContentsType::Texture2D) {
        loadLambertLighting(material, library, device, context);
        return;
    }
    
    _vertexProgram   = [library newFunctionWithName:@"phong_lighting_vertex"];
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        _textures.push_back(specular.getContentsTexture());
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            _fragmentProgram = [library newFunctionWithName:@"phong_lighting_fragment_c_reflect"];
        }
        else {
            _fragmentProgram = [library newFunctionWithName:@"phong_lighting_fragment_c"];
        }
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        _textures.push_back(specular.getContentsTexture());
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            _fragmentProgram = [library newFunctionWithName:@"phong_lighting_fragment_t_reflect"];
        }
        else {
            _fragmentProgram = [library newFunctionWithName:@"phong_lighting_fragment_t"];
        }
    }
}

void VROMaterialSubstrateMetal::loadBlinnLighting(VROMaterial &material,
                                                  id <MTLLibrary> library, id <MTLDevice> device,
                                                  const VRODriverMetal &context) {
    
    /*
     If there's no specular map, then we fall back to Lambert lighting.
     */
    VROMaterialVisual &specular = material.getSpecular();
    if (specular.getContentsType() != VROContentsType::Texture2D) {
        loadLambertLighting(material, library, device, context);
        return;
    }
    
    _vertexProgram   = [library newFunctionWithName:@"blinn_lighting_vertex"];
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        _textures.push_back(specular.getContentsTexture());

        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            _fragmentProgram = [library newFunctionWithName:@"blinn_lighting_fragment_c_reflect"];
        }
        else {
            _fragmentProgram = [library newFunctionWithName:@"blinn_lighting_fragment_c"];
        }
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        _textures.push_back(specular.getContentsTexture());

        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            _fragmentProgram = [library newFunctionWithName:@"blinn_lighting_fragment_t_reflect"];
        }
        else {
            _fragmentProgram = [library newFunctionWithName:@"blinn_lighting_fragment_t"];
        }
    }
}

VROConcurrentBuffer &VROMaterialSubstrateMetal::bindMaterialUniforms(VRORenderParameters &params,
                                                                     VROEyeType eye,
                                                                     int frame) {
    VROMaterialUniforms *uniforms = (VROMaterialUniforms *)_materialUniformsBuffer->getWritableContents(eye, frame);
    uniforms->diffuse_surface_color = toVectorFloat4(_material.getDiffuse().getContentsColor());
    uniforms->diffuse_intensity = _material.getDiffuse().getIntensity();
    uniforms->shininess = _material.getShininess();
    uniforms->alpha = _material.getTransparency() * params.opacities.top();
    
    return *_materialUniformsBuffer;
}

VROConcurrentBuffer &VROMaterialSubstrateMetal::bindLightingUniforms(const std::vector<std::shared_ptr<VROLight>> &lights,
                                                                     VROEyeType eye, int frame) {
    VROSceneLightingUniforms *uniforms = (VROSceneLightingUniforms *)_lightingUniformsBuffer->getWritableContents(eye, frame);
    uniforms->num_lights = (int) lights.size();
    
    VROVector3f ambientLight;

    for (int i = 0; i < lights.size(); i++) {
        const std::shared_ptr<VROLight> &light = lights[i];
        
        VROLightUniforms &light_uniforms = uniforms->lights[i];
        light_uniforms.type = (int) light->getType();
        light_uniforms.position = toVectorFloat3(light->getTransformedPosition());
        light_uniforms.direction = toVectorFloat3(light->getDirection());
        light_uniforms.color = toVectorFloat3(light->getColor());
        light_uniforms.attenuation_start_distance = light->getAttenuationStartDistance();
        light_uniforms.attenuation_end_distance = light->getAttenuationEndDistance();
        light_uniforms.attenuation_falloff_exp = light->getAttenuationFalloffExponent();
        light_uniforms.spot_inner_angle = degrees_to_radians(light->getSpotInnerAngle());
        light_uniforms.spot_outer_angle = degrees_to_radians(light->getSpotOuterAngle());
        
        if (light->getType() == VROLightType::Ambient) {
            ambientLight += light->getColor();
        }
    }
    
    uniforms->ambient_light_color = toVectorFloat3(ambientLight);
    return *_lightingUniformsBuffer;
}