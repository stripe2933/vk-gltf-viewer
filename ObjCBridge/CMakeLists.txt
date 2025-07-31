enable_language(OBJCXX)

add_library(ObjCBridge src/ObjCBridge.mm)
target_include_directories(ObjCBridge PUBLIC include)
target_sources(ObjCBridge PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES interface/mod.cppm
)

add_library(ObjCBridge::ObjCBridge ALIAS ObjCBridge)