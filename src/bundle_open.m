#import <string.h>

#import <Foundation/Foundation.h>
#include <lua.h>

#ifdef MACOS_USE_BUNDLE
void set_macos_bundle_resources(lua_State *L)
{ @autoreleasepool
{
    NSString* resource_path = [[NSBundle mainBundle] resourcePath];
    lua_pushstring(L, [resource_path UTF8String]);
    lua_setglobal(L, "MACOS_RESOURCES");
}}

char *get_macos_resources()
{ @autoreleasepool
{
    NSString* resource_path = [[NSBundle mainBundle] resourcePath];
    return strdup([resource_path UTF8String]);
}}
#endif

/* Thanks to mathewmariani, taken from his lite-macos github repository. */
void enable_momentum_scroll() {
  [[NSUserDefaults standardUserDefaults]
    setBool: YES
    forKey: @"AppleMomentumScrollSupported"];
}

