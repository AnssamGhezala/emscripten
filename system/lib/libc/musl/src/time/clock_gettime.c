#include <time.h>
#include <errno.h>
#include <stdint.h>
#include "syscall.h"
#include "libc.h"
#include "atomic.h"

#ifdef VDSO_CGT_SYM

void *__vdsosym(const char *, const char *);

static void *volatile vdso_func;

static int cgt_init(clockid_t clk, struct timespec *ts)
{
	void *p = __vdsosym(VDSO_CGT_VER, VDSO_CGT_SYM);
	int (*f)(clockid_t, struct timespec *) =
		(int (*)(clockid_t, struct timespec *))p;
	a_cas_p(&vdso_func, (void *)cgt_init, p);
	return f ? f(clk, ts) : -ENOSYS;
}

static void *volatile vdso_func = (void *)cgt_init;

#endif

#if __EMSCRIPTEN__
_Static_assert(CLOCK_REALTIME == __WASI_CLOCKID_REALTIME, "monotonic clock must match");
_Static_assert(CLOCK_MONOTONIC == __WASI_CLOCKID_MONOTONIC, "monotonic clock must match");

int __clock_gettime(clockid_t clk, struct timespec *ts) {
	__wasi_timestamp_t timestamp;
	if (__wasi_syscall_ret(__wasi_clock_time_get(clk, 1, &timestamp))) {
		return -1;
	}
  *ts = __wasi_timestamp_to_timespec(timestamp);
	return 0;
}
#else // __EMSCRIPTEN__
int __clock_gettime(clockid_t clk, struct timespec *ts)
{
	int r;

#ifdef VDSO_CGT_SYM
	int (*f)(clockid_t, struct timespec *) =
		(int (*)(clockid_t, struct timespec *))vdso_func;
	if (f) {
		r = f(clk, ts);
		if (!r) return r;
		if (r == -EINVAL) return __syscall_ret(r);
		/* Fall through on errors other than EINVAL. Some buggy
		 * vdso implementations return ENOSYS for clocks they
		 * can't handle, rather than making the syscall. This
		 * also handles the case where cgt_init fails to find
		 * a vdso function to use. */
	}
#endif

	r = __syscall(SYS_clock_gettime, clk, ts);
	if (r == -ENOSYS) {
		if (clk == CLOCK_REALTIME) {
			__syscall(SYS_gettimeofday, ts, 0);
			ts->tv_nsec = (int)ts->tv_nsec * 1000;
			return 0;
		}
		r = -EINVAL;
	}
	return __syscall_ret(r);
}
#endif //__EMSCRIPTEN__

weak_alias(__clock_gettime, clock_gettime);
