#pragma once

#if defined _MSC_VER

#define COMPILER_PRAGMA(text) __pragma(text)
#define STORE_COMPILER_WARNINGS COMPILER_PRAGMA(warning(push))
#define RESTORE_COMPILER_WARNINGS COMPILER_PRAGMA(warning(pop))
#define DISABLE_SPECIFIC_COMPILER_WARNING(warningCode) COMPILER_PRAGMA(warning (diable: warningCode))

#elif defined __clang__

#define COMPILER_PRAGMA(text) _Pragma(#text) // Stringifying the text to wrap it into quotes
#define STORE_COMPILER_WARNINGS COMPILER_PRAGMA(clang diagnostic push)
#define RESTORE_COMPILER_WARNINGS COMPILER_PRAGMA(clang diagnostic pop)
#define DISABLE_SPECIFIC_COMPILER_WARNING(warning) COMPILER_PRAGMA(clang diagnostic ignored warning)

#elif defined __GNUC__ || defined __GNUG__

#define COMPILER_PRAGMA(text) _Pragma(#text) // Stringifying the text to wrap it into quotes
#define STORE_COMPILER_WARNINGS COMPILER_PRAGMA(gcc diagnostic push)
#define RESTORE_COMPILER_WARNINGS COMPILER_PRAGMA(gcc diagnostic pop)
#define DISABLE_SPECIFIC_COMPILER_WARNING(warning) COMPILER_PRAGMA(gcc diagnostic ignored warning)

#else

#define COMPILER_PRAGMA(text)
#define DISABLE_SPECIFIC_COMPILER_WARNING(warning)
#define STORE_COMPILER_WARNINGS
#define RESTORE_COMPILER_WARNINGS

#endif


#if defined _MSC_VER

#define DISABLE_COMPILER_WARNINGS COMPILER_PRAGMA(warning(push, 0)) // Set /W0
#define RESTORE_COMPILER_WARNINGS COMPILER_PRAGMA(warning(pop))

#elif defined __clang__ || defined __GNUC__ || defined __GNUG__

#define DISABLE_COMPILER_WARNINGS \
    STORE_COMPILER_WARNINGS \
    DISABLE_SPECIFIC_COMPILER_WARNING("-Wshorten-64-to-32") \
    DISABLE_SPECIFIC_COMPILER_WARNING("-Wall") \
    DISABLE_SPECIFIC_COMPILER_WARNING("-Wunknown-pragmas") \
    DISABLE_SPECIFIC_COMPILER_WARNING("-Weverything")


#define RESTORE_COMPILER_WARNINGS COMPILER_PRAGMA(clang diagnostic pop)

#else

#pragma message ("Unknown compiler")

#define DISABLE_COMPILER_WARNINGS
#define RESTORE_COMPILER_WARNINGS

#endif

