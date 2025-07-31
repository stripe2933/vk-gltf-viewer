module;

#include <type_traits>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

export module MetalCpp;

namespace MTL {
    // MTL::BarrierScope
    export using MTL::BarrierScopeBuffers;
    export using MTL::BarrierScopeTextures;
    export using MTL::BarrierScopeRenderTargets;

    // MTL::BlitOption
    export using MTL::BlitOptionNone;
    export using MTL::BlitOptionDepthFromDepthStencil;
    export using MTL::BlitOptionStencilFromDepthStencil;
    export using MTL::BlitOptionRowLinearPVRTC;

    // MTL::CPUCacheMode
    export using MTL::CPUCacheModeDefaultCache;
    export using MTL::CPUCacheModeWriteCombined;

    // MTL::HazardTrackingMode
    export using MTL::HazardTrackingModeDefault;
    export using MTL::HazardTrackingModeUntracked;
    export using MTL::HazardTrackingModeTracked;

    // MTL::PixelFormat
    export using MTL::PixelFormatInvalid;
    export using MTL::PixelFormatA8Unorm;
    export using MTL::PixelFormatR8Unorm;
    export using MTL::PixelFormatR8Unorm_sRGB;
    export using MTL::PixelFormatR8Snorm;
    export using MTL::PixelFormatR8Uint;
    export using MTL::PixelFormatR8Sint;
    export using MTL::PixelFormatR16Unorm;
    export using MTL::PixelFormatR16Snorm;
    export using MTL::PixelFormatR16Uint;
    export using MTL::PixelFormatR16Sint;
    export using MTL::PixelFormatR16Float;
    export using MTL::PixelFormatRG8Unorm;
    export using MTL::PixelFormatRG8Unorm_sRGB;
    export using MTL::PixelFormatRG8Snorm;
    export using MTL::PixelFormatRG8Uint;
    export using MTL::PixelFormatRG8Sint;
    export using MTL::PixelFormatB5G6R5Unorm;
    export using MTL::PixelFormatA1BGR5Unorm;
    export using MTL::PixelFormatABGR4Unorm;
    export using MTL::PixelFormatBGR5A1Unorm;
    export using MTL::PixelFormatR32Uint;
    export using MTL::PixelFormatR32Sint;
    export using MTL::PixelFormatR32Float;
    export using MTL::PixelFormatRG16Unorm;
    export using MTL::PixelFormatRG16Snorm;
    export using MTL::PixelFormatRG16Uint;
    export using MTL::PixelFormatRG16Sint;
    export using MTL::PixelFormatRG16Float;
    export using MTL::PixelFormatRGBA8Unorm;
    export using MTL::PixelFormatRGBA8Unorm_sRGB;
    export using MTL::PixelFormatRGBA8Snorm;
    export using MTL::PixelFormatRGBA8Uint;
    export using MTL::PixelFormatRGBA8Sint;
    export using MTL::PixelFormatBGRA8Unorm;
    export using MTL::PixelFormatBGRA8Unorm_sRGB;
    export using MTL::PixelFormatRGB10A2Unorm;
    export using MTL::PixelFormatRGB10A2Uint;
    export using MTL::PixelFormatRG11B10Float;
    export using MTL::PixelFormatRGB9E5Float;
    export using MTL::PixelFormatBGR10A2Unorm;
    export using MTL::PixelFormatBGR10_XR;
    export using MTL::PixelFormatBGR10_XR_sRGB;
    export using MTL::PixelFormatRG32Uint;
    export using MTL::PixelFormatRG32Sint;
    export using MTL::PixelFormatRG32Float;
    export using MTL::PixelFormatRGBA16Unorm;
    export using MTL::PixelFormatRGBA16Snorm;
    export using MTL::PixelFormatRGBA16Uint;
    export using MTL::PixelFormatRGBA16Sint;
    export using MTL::PixelFormatRGBA16Float;
    export using MTL::PixelFormatBGRA10_XR;
    export using MTL::PixelFormatBGRA10_XR_sRGB;
    export using MTL::PixelFormatRGBA32Uint;
    export using MTL::PixelFormatRGBA32Sint;
    export using MTL::PixelFormatRGBA32Float;
    export using MTL::PixelFormatBC1_RGBA;
    export using MTL::PixelFormatBC1_RGBA_sRGB;
    export using MTL::PixelFormatBC2_RGBA;
    export using MTL::PixelFormatBC2_RGBA_sRGB;
    export using MTL::PixelFormatBC3_RGBA;
    export using MTL::PixelFormatBC3_RGBA_sRGB;
    export using MTL::PixelFormatBC4_RUnorm;
    export using MTL::PixelFormatBC4_RSnorm;
    export using MTL::PixelFormatBC5_RGUnorm;
    export using MTL::PixelFormatBC5_RGSnorm;
    export using MTL::PixelFormatBC6H_RGBFloat;
    export using MTL::PixelFormatBC6H_RGBUfloat;
    export using MTL::PixelFormatBC7_RGBAUnorm;
    export using MTL::PixelFormatBC7_RGBAUnorm_sRGB;
    export using MTL::PixelFormatPVRTC_RGB_2BPP;
    export using MTL::PixelFormatPVRTC_RGB_2BPP_sRGB;
    export using MTL::PixelFormatPVRTC_RGB_4BPP;
    export using MTL::PixelFormatPVRTC_RGB_4BPP_sRGB;
    export using MTL::PixelFormatPVRTC_RGBA_2BPP;
    export using MTL::PixelFormatPVRTC_RGBA_2BPP_sRGB;
    export using MTL::PixelFormatPVRTC_RGBA_4BPP;
    export using MTL::PixelFormatPVRTC_RGBA_4BPP_sRGB;
    export using MTL::PixelFormatEAC_R11Unorm;
    export using MTL::PixelFormatEAC_R11Snorm;
    export using MTL::PixelFormatEAC_RG11Unorm;
    export using MTL::PixelFormatEAC_RG11Snorm;
    export using MTL::PixelFormatEAC_RGBA8;
    export using MTL::PixelFormatEAC_RGBA8_sRGB;
    export using MTL::PixelFormatETC2_RGB8;
    export using MTL::PixelFormatETC2_RGB8_sRGB;
    export using MTL::PixelFormatETC2_RGB8A1;
    export using MTL::PixelFormatETC2_RGB8A1_sRGB;
    export using MTL::PixelFormatASTC_4x4_sRGB;
    export using MTL::PixelFormatASTC_5x4_sRGB;
    export using MTL::PixelFormatASTC_5x5_sRGB;
    export using MTL::PixelFormatASTC_6x5_sRGB;
    export using MTL::PixelFormatASTC_6x6_sRGB;
    export using MTL::PixelFormatASTC_8x5_sRGB;
    export using MTL::PixelFormatASTC_8x6_sRGB;
    export using MTL::PixelFormatASTC_8x8_sRGB;
    export using MTL::PixelFormatASTC_10x5_sRGB;
    export using MTL::PixelFormatASTC_10x6_sRGB;
    export using MTL::PixelFormatASTC_10x8_sRGB;
    export using MTL::PixelFormatASTC_10x10_sRGB;
    export using MTL::PixelFormatASTC_12x10_sRGB;
    export using MTL::PixelFormatASTC_12x12_sRGB;
    export using MTL::PixelFormatASTC_4x4_LDR;
    export using MTL::PixelFormatASTC_5x4_LDR;
    export using MTL::PixelFormatASTC_5x5_LDR;
    export using MTL::PixelFormatASTC_6x5_LDR;
    export using MTL::PixelFormatASTC_6x6_LDR;
    export using MTL::PixelFormatASTC_8x5_LDR;
    export using MTL::PixelFormatASTC_8x6_LDR;
    export using MTL::PixelFormatASTC_8x8_LDR;
    export using MTL::PixelFormatASTC_10x5_LDR;
    export using MTL::PixelFormatASTC_10x6_LDR;
    export using MTL::PixelFormatASTC_10x8_LDR;
    export using MTL::PixelFormatASTC_10x10_LDR;
    export using MTL::PixelFormatASTC_12x10_LDR;
    export using MTL::PixelFormatASTC_12x12_LDR;
    export using MTL::PixelFormatASTC_4x4_HDR;
    export using MTL::PixelFormatASTC_5x4_HDR;
    export using MTL::PixelFormatASTC_5x5_HDR;
    export using MTL::PixelFormatASTC_6x5_HDR;
    export using MTL::PixelFormatASTC_6x6_HDR;
    export using MTL::PixelFormatASTC_8x5_HDR;
    export using MTL::PixelFormatASTC_8x6_HDR;
    export using MTL::PixelFormatASTC_8x8_HDR;
    export using MTL::PixelFormatASTC_10x5_HDR;
    export using MTL::PixelFormatASTC_10x6_HDR;
    export using MTL::PixelFormatASTC_10x8_HDR;
    export using MTL::PixelFormatASTC_10x10_HDR;
    export using MTL::PixelFormatASTC_12x10_HDR;
    export using MTL::PixelFormatASTC_12x12_HDR;
    export using MTL::PixelFormatGBGR422;
    export using MTL::PixelFormatBGRG422;
    export using MTL::PixelFormatDepth16Unorm;
    export using MTL::PixelFormatDepth32Float;
    export using MTL::PixelFormatStencil8;
    export using MTL::PixelFormatDepth24Unorm_Stencil8;
    export using MTL::PixelFormatDepth32Float_Stencil8;
    export using MTL::PixelFormatX32_Stencil8;
    export using MTL::PixelFormatX24_Stencil8;

    // MTL::PurgeableState
    export using MTL::PurgeableStateKeepCurrent;
    export using MTL::PurgeableStateNonVolatile;
    export using MTL::PurgeableStateVolatile;
    export using MTL::PurgeableStateEmpty;

    // MTL::ResourceOptions
    export using MTL::ResourceCPUCacheModeDefaultCache;
    export using MTL::ResourceCPUCacheModeWriteCombined;
    export using MTL::ResourceStorageModeShared;
    export using MTL::ResourceStorageModeManaged;
    export using MTL::ResourceStorageModePrivate;
    export using MTL::ResourceStorageModeMemoryless;
    export using MTL::ResourceHazardTrackingModeDefault;
    export using MTL::ResourceHazardTrackingModeUntracked;
    export using MTL::ResourceHazardTrackingModeTracked;
    export using MTL::ResourceOptionCPUCacheModeDefault;
    export using MTL::ResourceOptionCPUCacheModeWriteCombined;

    // MTL::ResourceUsage
    export using MTL::ResourceUsageRead;
    export using MTL::ResourceUsageWrite;
    export using MTL::ResourceUsageSample;

    // MTL::StorageMode
    export using MTL::StorageModeShared;
    export using MTL::StorageModeManaged;
    export using MTL::StorageModePrivate;
    export using MTL::StorageModeMemoryless;

    // MTL::TextureCompressionType
    export using MTL::TextureCompressionTypeLossless;
    export using MTL::TextureCompressionTypeLossy;

    // MTL::TextureSwizzle
    export using MTL::TextureSwizzleZero;
    export using MTL::TextureSwizzleOne;
    export using MTL::TextureSwizzleRed;
    export using MTL::TextureSwizzleGreen;
    export using MTL::TextureSwizzleBlue;
    export using MTL::TextureSwizzleAlpha;

    // MTL::TextureType
    export using MTL::TextureType1D;
    export using MTL::TextureType1DArray;
    export using MTL::TextureType2D;
    export using MTL::TextureType2DArray;
    export using MTL::TextureType2DMultisample;
    export using MTL::TextureTypeCube;
    export using MTL::TextureTypeCubeArray;
    export using MTL::TextureType3D;
    export using MTL::TextureType2DMultisampleArray;
    export using MTL::TextureTypeTextureBuffer;

    // MTL::TextureUsage
    export using MTL::TextureUsageUnknown;
    export using MTL::TextureUsageShaderRead;
    export using MTL::TextureUsageShaderWrite;
    export using MTL::TextureUsageRenderTarget;
    export using MTL::TextureUsagePixelFormatView;
    export using MTL::TextureUsageShaderAtomic;

    export using MTL::BarrierScope;
    export using MTL::BlitCommandEncoder;
    export using MTL::BlitOption;
    export using MTL::CPUCacheMode;
    export using MTL::Buffer;
    export using MTL::Device;
    export using MTL::CommandBuffer;
    export using MTL::CommandEncoder;
    export using MTL::CommandQueue;
    export using MTL::Event;
    export using MTL::HazardTrackingMode;
    export using MTL::PixelFormat;
    export using MTL::PurgeableState;
    export using MTL::Resource;
    export using MTL::ResourceOptions;
    export using MTL::ResourceUsage;
    export using MTL::SharedEvent;
    export using MTL::SharedTextureHandle;
    export using MTL::StorageMode;
    export using MTL::Texture;
    export using MTL::TextureCompressionType;
    export using MTL::TextureDescriptor;
    export using MTL::TextureSwizzle;
    export using MTL::TextureSwizzleChannels;
    export using MTL::TextureType;
    export using MTL::TextureUsage;
}

namespace NS {
    // NS::StringEncoding
    export using NS::NEXTSTEPStringEncoding;
    export using NS::JapaneseEUCStringEncoding;
    export using NS::UTF8StringEncoding;
    export using NS::ISOLatin1StringEncoding;
    export using NS::SymbolStringEncoding;
    export using NS::NonLossyASCIIStringEncoding;
    export using NS::ShiftJISStringEncoding;
    export using NS::ISOLatin2StringEncoding;
    export using NS::UnicodeStringEncoding;
    export using NS::WindowsCP1251StringEncoding;
    export using NS::WindowsCP1252StringEncoding;
    export using NS::WindowsCP1253StringEncoding;
    export using NS::WindowsCP1254StringEncoding;
    export using NS::WindowsCP1250StringEncoding;
    export using NS::ISO2022JPStringEncoding;
    export using NS::MacOSRomanStringEncoding;
    export using NS::UTF16StringEncoding;
    export using NS::UTF16BigEndianStringEncoding;
    export using NS::UTF16LittleEndianStringEncoding;
    export using NS::UTF32StringEncoding;
    export using NS::UTF32BigEndianStringEncoding;
    export using NS::UTF32LittleEndianStringEncoding;

    // NS::StringCompareOptions
    export using NS::CaseInsensitiveSearch;
    export using NS::LiteralSearch;
    export using NS::BackwardsSearch;
    export using NS::AnchoredSearch;
    export using NS::NumericSearch;
    export using NS::DiacriticInsensitiveSearch;
    export using NS::WidthInsensitiveSearch;
    export using NS::ForcedOrderingSearch;
    export using NS::RegularExpressionSearch;

    export using NS::RetainPtr;
    export using NS::SharedPtr;
    export using NS::String;
    export using NS::StringEncoding;
    export using NS::TransferPtr;
}