#import <ObjCBridge.h>

void *ObjCBridge_bridge(void *handle) {
    return (__bridge void*)handle;
}