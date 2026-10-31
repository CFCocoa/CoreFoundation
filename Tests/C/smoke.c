// Smoke test for the standalone C build of CoreFoundation.
//
// Exercises the runtime (custom CFRuntimeClass, retain/release, allocator
// path), the collections, strings, property lists, ICU-backed formatters,
// time zones, URLs, UUIDs, the run loop (which needs libdispatch) and the
// helpers restored for the C runtime mode (CFCopyUserName, current directory).
//
// SPDX-License-Identifier: Apache-2.0

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFRuntime.h>
#include <CoreFoundation/CFPriv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static Boolean cstringEquals(CFStringRef s, const char *expected) {
    char buf[512];
    if (!s) return false;
    if (!CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return false;
    return strcmp(buf, expected) == 0;
}

// --- a custom CF type -------------------------------------------------------

typedef struct {
    CFRuntimeBase base;
    int payload;
} Widget;

static int widgetsFinalized = 0;

static void widgetFinalize(CFTypeRef cf) { (void)cf; widgetsFinalized++; }
static Boolean widgetEqual(CFTypeRef a, CFTypeRef b) { return ((const Widget *)a)->payload == ((const Widget *)b)->payload; }
static CFHashCode widgetHash(CFTypeRef cf) { return (CFHashCode)((const Widget *)cf)->payload; }
static CFStringRef widgetCopyDescription(CFTypeRef cf) {
    return CFStringCreateWithFormat(NULL, NULL, CFSTR("<Widget %d>"), ((const Widget *)cf)->payload);
}

static const CFRuntimeClass widgetClass = {
    0, "Widget", NULL, NULL, widgetFinalize, widgetEqual, widgetHash, NULL, widgetCopyDescription
};

static void testRuntime(void) {
    CFTypeID widgetTypeID = _CFRuntimeRegisterClass(&widgetClass);
    CHECK(widgetTypeID != _kCFRuntimeNotATypeID);

    Widget *w = (Widget *)_CFRuntimeCreateInstance(kCFAllocatorDefault, widgetTypeID, sizeof(Widget) - sizeof(CFRuntimeBase), NULL);
    CHECK(w != NULL);
    w->payload = 42;
    CHECK(CFGetTypeID(w) == widgetTypeID);
    CHECK(CFGetRetainCount(w) == 1);
    CFRetain(w);
    CHECK(CFGetRetainCount(w) == 2);
    CFRelease(w);
    CHECK(CFGetRetainCount(w) == 1);

    CFStringRef desc = CFCopyDescription(w);
    CHECK(cstringEquals(desc, "<Widget 42>"));
    CFRelease(desc);

    CFRelease(w);
    CHECK(widgetsFinalized == 1);

    // A non-default allocator exercises the allocator bookkeeping in _CFRelease.
    CFStringRef s = CFStringCreateWithCString(kCFAllocatorMalloc, "malloc allocator", kCFStringEncodingUTF8);
    CHECK(s != NULL);
    CHECK(CFGetAllocator(s) == kCFAllocatorMalloc || CFGetAllocator(s) == kCFAllocatorSystemDefault);
    CFRelease(s);
}

// --- strings and collections -------------------------------------------------

static void testStringsAndCollections(void) {
    CFStringRef constant = CFSTR("hello");
    CHECK(CFStringGetLength(constant) == 5);
    CHECK(CFGetTypeID(constant) == CFStringGetTypeID());
    CFRetain(constant); CFRelease(constant); // constant strings must survive retain/release

    CFMutableStringRef m = CFStringCreateMutableCopy(NULL, 0, constant);
    CFStringUppercase(m, NULL);
    CHECK(cstringEquals(m, "HELLO"));
    CFStringAppend(m, CFSTR(" wörld"));
    CHECK(CFStringGetLength(m) == 11);
    CFRange r = CFStringFind(m, CFSTR("wörld"), 0);
    CHECK(r.location == 6 && r.length == 5);
    CFRelease(m);

    CFMutableArrayRef array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    for (int i = 0; i < 100; i++) {
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &i);
        CFArrayAppendValue(array, n);
        CFRelease(n);
    }
    CHECK(CFArrayGetCount(array) == 100);
    int v = -1;
    CHECK(CFNumberGetValue(CFArrayGetValueAtIndex(array, 57), kCFNumberIntType, &v) && v == 57);

    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(dict, CFSTR("numbers"), array);
    CFDictionarySetValue(dict, CFSTR("name"), CFSTR("CoreFoundation"));
    CFDictionarySetValue(dict, CFSTR("flag"), kCFBooleanTrue);
    CHECK(CFDictionaryGetCount(dict) == 3);
    CHECK(CFDictionaryGetValue(dict, CFSTR("flag")) == kCFBooleanTrue);

    CFMutableSetRef set = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
    CFSetAddValue(set, CFSTR("a"));
    CFSetAddValue(set, CFSTR("a"));
    CFSetAddValue(set, CFSTR("b"));
    CHECK(CFSetGetCount(set) == 2);
    CFRelease(set);

    // Property list round trip, XML and binary.
    CFErrorRef error = NULL;
    for (int fmt = 0; fmt < 2; fmt++) {
        CFPropertyListFormat format = fmt == 0 ? kCFPropertyListXMLFormat_v1_0 : kCFPropertyListBinaryFormat_v1_0;
        CFDataRef data = CFPropertyListCreateData(NULL, dict, format, 0, &error);
        CHECK(data != NULL && error == NULL);
        if (data) {
            CFPropertyListRef back = CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, NULL, &error);
            CHECK(back != NULL && error == NULL);
            CHECK(back && CFEqual(back, dict));
            if (back) CFRelease(back);
            CFRelease(data);
        }
    }

    CFRelease(dict);
    CFRelease(array);

    CFDataRef d = CFDataCreate(NULL, (const UInt8 *)"bytes", 5);
    CHECK(CFDataGetLength(d) == 5);
    CFRelease(d);
}

// --- ICU-backed functionality ----------------------------------------------

static void testLocaleAndFormatters(void) {
    CFLocaleRef posix = CFLocaleCreate(NULL, CFSTR("en_US_POSIX"));
    CHECK(posix != NULL);

    CFTimeZoneRef gmt = CFTimeZoneCreateWithTimeIntervalFromGMT(NULL, 0);
    CHECK(gmt != NULL);

    CFDateFormatterRef df = CFDateFormatterCreate(NULL, posix, kCFDateFormatterNoStyle, kCFDateFormatterNoStyle);
    CFDateFormatterSetFormat(df, CFSTR("yyyy-MM-dd HH:mm:ss"));
    CFDateFormatterSetProperty(df, kCFDateFormatterTimeZone, gmt);
    CFStringRef epoch = CFDateFormatterCreateStringWithAbsoluteTime(NULL, df, 0.0);
    CHECK(cstringEquals(epoch, "2001-01-01 00:00:00"));
    CFRelease(epoch);

    CFAbsoluteTime parsed = -1;
    CHECK(CFDateFormatterGetAbsoluteTimeFromString(df, CFSTR("2001-01-02 00:00:00"), NULL, &parsed));
    CHECK(parsed == 86400.0);
    CFRelease(df);

    CFLocaleRef enUS = CFLocaleCreate(NULL, CFSTR("en_US"));
    CFNumberFormatterRef nf = CFNumberFormatterCreate(NULL, enUS, kCFNumberFormatterDecimalStyle);
    double value = 1234.5;
    CFStringRef formatted = CFNumberFormatterCreateStringWithValue(NULL, nf, kCFNumberDoubleType, &value);
    CHECK(cstringEquals(formatted, "1,234.5"));
    CFRelease(formatted);
    CFRelease(nf);

    CFStringRef lang = CFLocaleGetValue(enUS, kCFLocaleLanguageCode);
    CHECK(cstringEquals(lang, "en"));
    CFRelease(enUS);

    CFCalendarRef cal = CFCalendarCreateWithIdentifier(NULL, kCFGregorianCalendar);
    CFCalendarSetTimeZone(cal, gmt);
    int year = 0, month = 0, day = 0;
    CHECK(CFCalendarDecomposeAbsoluteTime(cal, 86400.0 * 31, "yMd", &year, &month, &day));
    CHECK(year == 2001 && month == 2 && day == 1);
    CFRelease(cal);

    CFTimeZoneRef la = CFTimeZoneCreateWithName(NULL, CFSTR("America/Los_Angeles"), true);
    CHECK(la != NULL); // needs the system zoneinfo database
    if (la) {
        CHECK(CFTimeZoneGetSecondsFromGMT(la, 0.0) == -8 * 3600);
        CFRelease(la);
    }
    CFRelease(gmt);
    CFRelease(posix);

    // Unicode tables compiled into the library (CFUniChar*.inc.h).
    CFMutableStringRef s = CFStringCreateMutableCopy(NULL, 0, CFSTR("Ångström"));
    CFStringNormalize(s, kCFStringNormalizationFormD);
    CHECK(CFStringGetLength(s) == 10);
    CFStringNormalize(s, kCFStringNormalizationFormC);
    CHECK(CFStringGetLength(s) == 8);
    CFRelease(s);

    CFCharacterSetRef letters = CFCharacterSetGetPredefined(kCFCharacterSetLetter);
    CHECK(CFCharacterSetIsCharacterMember(letters, 'a'));
    CHECK(!CFCharacterSetIsCharacterMember(letters, '1'));
}

// --- URLs, UUIDs, platform helpers -------------------------------------------

static void testURLsAndPlatform(void) {
    CFURLRef rel = CFURLCreateWithFileSystemPath(NULL, CFSTR("relative/file.txt"), kCFURLPOSIXPathStyle, false);
    CHECK(rel != NULL);
    // Resolving against the current directory goes through _CFGetCurrentDirectory.
    CFURLRef abs = CFURLCopyAbsoluteURL(rel);
    CHECK(abs != NULL);
    if (abs) {
        CFStringRef path = CFURLCopyFileSystemPath(abs, kCFURLPOSIXPathStyle);
        CHECK(path && CFStringHasPrefix(path, CFSTR("/")));
        CHECK(path && CFStringHasSuffix(path, CFSTR("/relative/file.txt")));
        if (path) CFRelease(path);
        CFRelease(abs);
    }
    CFRelease(rel);

    CFURLRef url = CFURLCreateWithString(NULL, CFSTR("https://user@example.com:8080/a/b?q=1#frag"), NULL);
    CHECK(url != NULL);
    CFStringRef host = CFURLCopyHostName(url);
    CHECK(cstringEquals(host, "example.com"));
    CHECK(CFURLGetPortNumber(url) == 8080);
    CFRelease(host);
    CFRelease(url);

    CFUUIDRef uuid = CFUUIDCreate(NULL);
    CFStringRef uuidString = CFUUIDCreateString(NULL, uuid);
    CHECK(CFStringGetLength(uuidString) == 36);
    CFUUIDRef parsedUUID = CFUUIDCreateFromString(NULL, uuidString);
    CHECK(CFEqual(uuid, parsedUUID));
    CFRelease(parsedUUID);
    CFRelease(uuidString);
    CFRelease(uuid);

    CFStringRef user = CFCopyUserName();
    CHECK(user != NULL); // restored C implementation
    if (user) CFRelease(user);

    CFBundleRef mainBundle = CFBundleGetMainBundle();
    (void)mainBundle; // may be NULL for a bare executable; must not crash

    CFStringRef key = CFSTR("CFSmokeTestKey");
    CFPreferencesSetAppValue(key, CFSTR("value"), kCFPreferencesCurrentApplication);
    CFStringRef back = CFPreferencesCopyAppValue(key, kCFPreferencesCurrentApplication);
    CHECK(cstringEquals(back, "value"));
    if (back) CFRelease(back);
    CFPreferencesSetAppValue(key, NULL, kCFPreferencesCurrentApplication);
}

// --- run loop (libdispatch) --------------------------------------------------

static int timerFired = 0;

static void timerCallback(CFRunLoopTimerRef timer, void *info) {
    (void)timer;
    timerFired++;
    CFRunLoopStop((CFRunLoopRef)info);
}

static void testRunLoop(void) {
    CFRunLoopRef rl = CFRunLoopGetCurrent();
    CHECK(rl != NULL);
    CHECK(rl == CFRunLoopGetMain());

    CFRunLoopTimerContext ctx = {0, rl, NULL, NULL, NULL};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.05, 0, 0, 0, timerCallback, &ctx);
    CFRunLoopAddTimer(rl, timer, kCFRunLoopDefaultMode);

    __block int blockRan = 0;
    CFRunLoopPerformBlock(rl, kCFRunLoopDefaultMode, ^{ blockRan++; });
    CFRunLoopWakeUp(rl);

    SInt32 result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5.0, false);
    CHECK(result == kCFRunLoopRunStopped);
    CHECK(timerFired == 1);
    CHECK(blockRan == 1);

    CFRunLoopRemoveTimer(rl, timer, kCFRunLoopDefaultMode);
    CFRelease(timer);
}

int main(void) {
    testRuntime();
    testStringsAndCollections();
    testLocaleAndFormatters();
    testURLsAndPlatform();
    testRunLoop();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
