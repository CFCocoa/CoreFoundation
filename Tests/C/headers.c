// Compiles the umbrella header plus the public headers it does not pull in,
// the way an ordinary C consumer would: no prefix header, no CF_BUILDING_CF.
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFRuntime.h>
#include <CoreFoundation/CFPriv.h>
#include <CoreFoundation/CFLogUtilities.h>
#include <CoreFoundation/CFByteOrder.h>
#include <CoreFoundation/CFBinaryHeap.h>
#include <CoreFoundation/CFBigNumber.h>
#include <CoreFoundation/CFBitVector.h>
#include <CoreFoundation/CFBundlePriv.h>
#include <CoreFoundation/CFCalendar.h>
#include <CoreFoundation/CFDateInterval.h>
#include <CoreFoundation/CFDateIntervalFormatter.h>
#include <CoreFoundation/CFListFormatter.h>
#include <CoreFoundation/CFRelativeDateTimeFormatter.h>
#include <CoreFoundation/CFRegularExpression.h>
#include <CoreFoundation/CFStorage.h>
#include <CoreFoundation/CFStreamPriv.h>
#include <CoreFoundation/CFURLComponents.h>
#include <CoreFoundation/CFURLPriv.h>
#include <CoreFoundation/CFUniChar.h>
#include <CoreFoundation/CFStringEncodingConverter.h>
#include <CoreFoundation/CFStringEncodingConverterExt.h>
#include <CoreFoundation/CFStringDefaultEncoding.h>
#include <CoreFoundation/CFCharacterSetPriv.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <CoreFoundation/ForFoundationOnly.h>

#include <stdio.h>

int main(void) {
    printf("sizeof(CFRuntimeBase) = %zu, DEPLOYMENT_RUNTIME_SWIFT = %d\n",
           sizeof(CFRuntimeBase), DEPLOYMENT_RUNTIME_SWIFT);
    // The library and the headers must agree on the object layout.
    return DEPLOYMENT_RUNTIME_SWIFT == 0 && sizeof(CFRuntimeBase) == 2 * sizeof(void *) ? 0 : 1;
}
