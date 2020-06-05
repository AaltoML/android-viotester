#ifndef ALWAYS_ASSERT_H
#define ALWAYS_ASSERT_H

// CMake makes it difficult to control variables such as NDEBUG ... take this, CMake!
#ifdef NDEBUG
#undef NDEBUG
#endif

#endif