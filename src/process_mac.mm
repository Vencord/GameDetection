#import <AppKit/AppKit.h>
#include <unistd.h>

#include <vector>
#include <string>
#include "matcher.h"

std::vector<Process> listProcesses(const Database& db) {
    std::vector<Process> processes;

    @autoreleasepool {
        NSArray<NSRunningApplication *> *runningApps = [[NSWorkspace sharedWorkspace] runningApplications];
        
        for (NSRunningApplication *app in runningApps) {
            NSURL *bundleURL = [app bundleURL];
            if (!bundleURL) continue;

            pid_t pid = [app processIdentifier];
            std::string path = normalizePath([[bundleURL path] UTF8String]);

            if (db.isCandidate(path, false, true)) {
                processes.push_back({
                    static_cast<uint32_t>(pid), 
                    std::move(path), 
                    "", 
                    false, 
                    true
                });
            }
        }
    }

    return processes;
}