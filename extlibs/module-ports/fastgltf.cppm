module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

export module fastgltf;

namespace fastgltf {
    export using fastgltf::Accessor;
    export using fastgltf::AccessorType;
    export using fastgltf::AlphaMode;
    export using fastgltf::Asset;
    export using fastgltf::AssetInfo;
    export using fastgltf::Buffer;
    export using fastgltf::BufferTarget;
    export using fastgltf::BufferView;
    export using fastgltf::Camera;
    export using fastgltf::ComponentType;
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

namespace sources {
    export using sources::Array;
    export using sources::BufferView;
    export using sources::ByteView;
    export using sources::URI;
}
}