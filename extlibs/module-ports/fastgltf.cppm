module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

export module fastgltf;

export import glm;

namespace fastgltf {
    export using fastgltf::Accessor;
    export using fastgltf::AccessorType;
    export using fastgltf::AlphaMode;
    export using fastgltf::Animation;
    export using fastgltf::AnimationChannel;
    export using fastgltf::AnimationInterpolation;
    export using fastgltf::AnimationPath;
    export using fastgltf::AnimationSampler;
    export using fastgltf::Asset;
    export using fastgltf::AssetInfo;
    export using fastgltf::Buffer;
    export using fastgltf::BufferTarget;
    export using fastgltf::BufferView;
    export using fastgltf::Camera;
    export using fastgltf::ComponentType;
    export using fastgltf::copyFromAccessor;
    export using fastgltf::decomposeTransformMatrix;
    export using fastgltf::DefaultBufferDataAdapter;
    export using fastgltf::ElementTraits;
    export using fastgltf::ElementTraitsBase;
    export using fastgltf::Expected;
    export using fastgltf::Extensions;
    export using fastgltf::Filter;
    export using fastgltf::getAccessorElement;
    export using fastgltf::getElementByteSize;
    export using fastgltf::GltfDataBuffer;
    export using fastgltf::Image;
    export using fastgltf::IterableAccessor;
    export using fastgltf::iterateAccessor;
    export using fastgltf::iterateAccessorWithIndex;
    export using fastgltf::Light;
    export using fastgltf::Material;
    export using fastgltf::Mesh;
    export using fastgltf::MimeType;
    export using fastgltf::Node;
    export using fastgltf::OptionalWithFlagValue;
    export using fastgltf::Parser;
    export using fastgltf::Primitive;
    export using fastgltf::PrimitiveType;
    export using fastgltf::Sampler;
    export using fastgltf::Scene;
    export using fastgltf::Texture;
    export using fastgltf::TRS;
    export using fastgltf::visitor;
    export using fastgltf::Wrap;

    export using fastgltf::operator|;
    export using fastgltf::operator&;
    export using fastgltf::operator-;
    export using fastgltf::operator~;

    export template <>
    struct ElementTraits<glm::vec2> : ElementTraitsBase<glm::vec2, AccessorType::Vec2, float> {};
    export template <>
    struct ElementTraits<glm::vec3> : ElementTraitsBase<glm::vec3, AccessorType::Vec3, float> {};
    export template <>
    struct ElementTraits<glm::vec4> : ElementTraitsBase<glm::vec4, AccessorType::Vec4, float> {};
#ifndef GLM_FORCE_QUAT_DATA_WXYZ
    // Unless GLM_FORCE_QUAT_DATA_WXYZ is defined, glm::quat components are stored as xyzw, which has the same manner in the glTF's quaternion.
    export template <>
    struct ElementTraits<glm::quat> : ElementTraitsBase<glm::quat, AccessorType::Vec4, float> {};
#endif

namespace sources {
    export using sources::Array;
    export using sources::BufferView;
    export using sources::ByteView;
    export using sources::URI;
}
}