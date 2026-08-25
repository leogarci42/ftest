#pragma once

// Feature detection for GNU extensions used by ftl. Where an extension is
// missing, ftl falls back to strict-POSIX implementations with slightly
// reduced semantics (documented per symbol in docs/reference.md).
//
//   FTL_HAVE_EXECVPE          execvpe()            (glibc, musl w/ _GNU_SOURCE)
//   FTL_HAVE_TIMEDJOIN_NP     pthread_timedjoin_np (glibc, musl w/ _GNU_SOURCE)

#if defined(__GLIBC__) || defined(__MUSL__)
#define FTL_HAVE_EXECVPE 1
#define FTL_HAVE_TIMEDJOIN_NP 1
#endif
