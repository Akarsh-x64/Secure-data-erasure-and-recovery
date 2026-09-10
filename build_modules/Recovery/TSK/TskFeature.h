#pragma once

// Compile-time feature detection for libtsk.
// CMake builds define either RECOVERY_ENABLE_LIBTSK or
// RECOVERY_DISABLE_LIBTSK so linkage is explicit and reproducible. Standalone
// builds retain header-based detection for existing direct test commands.
#if defined(RECOVERY_DISABLE_LIBTSK)
#  define RECOVERY_HAS_LIBTSK 0
#elif defined(RECOVERY_ENABLE_LIBTSK)
#  define RECOVERY_HAS_LIBTSK 1
#elif defined(__has_include)
#  if __has_include(<tsk/libtsk.h>)
#    define RECOVERY_HAS_LIBTSK 1
#  else
#    define RECOVERY_HAS_LIBTSK 0
#  endif
#else
#  define RECOVERY_HAS_LIBTSK 0
#endif

#if RECOVERY_HAS_LIBTSK
#  include <tsk/libtsk.h>
#endif
