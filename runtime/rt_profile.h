/*
 * xbintsc runtime — opt-in call counters.
 *
 * Included by `rt_internal.h` so every runtime unit can bump a counter with
 * `XT_PROFILE_TICK`; `xt_profile.c` owns the storage and the exit dump. Kept in
 * its own header so the shared runtime header stays within the size budget.
 */

#ifndef XT_RT_PROFILE_H
#define XT_RT_PROFILE_H

enum {
  XT_PROFILE_ALLOC,
  XT_PROFILE_GC,
  XT_PROFILE_MATH,
  XT_PROFILE_SET,
  XT_PROFILE_GET_IC,
  XT_PROFILE_METHOD_IC,
  XT_PROFILE_OBJECT_SET,
  XT_PROFILE_OBJECT_INSERT,
  XT_PROFILE_OBJECT_FIND,
  XT_PROFILE_OBJECT_RESERVE,
  XT_PROFILE_CLOSURE_CALL,
  XT_PROFILE_ARRAY_GET,
  XT_PROFILE_STRING_CSTR,
  XT_PROFILE_TYPED_READ,
  XT_PROFILE_TYPED_WRITE,
  XT_PROFILE_COUNT
};

extern int xt_profile_on;
extern unsigned long long xt_profile_calls[XT_PROFILE_COUNT];
void xt_profile_init(void);

#define XT_PROFILE_TICK(slot) \
  do {                         \
    if (xt_profile_on) xt_profile_calls[slot]++; \
  } while (0)

#endif /* XT_RT_PROFILE_H */
