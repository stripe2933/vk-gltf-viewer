#include <apple/filesystem.hpp>

#import <Foundation/Foundation.h>

std::filesystem::path getApplicationSupportFolderPath() {
    @autoreleasepool {
        NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);

        NSString* basePath = [paths firstObject];

        // Use bundle identifier (recommended)
        NSString* bundleID = [[NSBundle mainBundle] bundleIdentifier];

        NSString* fullPath = [basePath stringByAppendingPathComponent:bundleID];

        // Create directory if needed
        [[NSFileManager defaultManager] createDirectoryAtPath:fullPath
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];

        return reinterpret_cast<const char8_t*>([fullPath UTF8String]);
    }
}