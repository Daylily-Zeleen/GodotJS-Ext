#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

// Include all test headers to register TEST_CASE macros
#include "tests/jsb_test_helpers.h"
#include "tests/test_jsb_any_runtime.h"
#include "tests/test_jsb_sarray.h"
#if JSB_WITH_QUICKJS
#	include "tests/test_jsb_quickjs_runtime.h"
#endif
#if JSB_WITH_V8
#	include "tests/test_jsb_v8_runtime.h"
#endif

// doctest will automatically collect all TEST_CASE and run them in main()
