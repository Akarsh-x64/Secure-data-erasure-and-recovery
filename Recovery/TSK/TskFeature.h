#pragma once

// Compile-time feature detection for libtsk.
// Keep all libtsk-specific includes isolated under Recovery/TSK.
#if defined(__has_include)
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
