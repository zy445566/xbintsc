/*
 * xbintsc runtime — program termination.
 *
 * The generated `main` runs the module body, drains microtasks, pumps the
 * reactor and then calls `xt_finish()`, which gives the host one last chance to
 * run JavaScript before the process ends: the Node extension registers a hook
 * that fires the `process` "exit" listeners there. `xt_finish` also returns the
 * status the program asked for, because the generated `main` no longer returns a
 * constant — `process.exitCode = 1` (and `process.exit(1)`, which ends up in the
 * same place) reaches the parent process.
 *
 * The hook runs at most once: `process.exit()` calls `xt_finish()` itself before
 * `exit()`, so the terminating call cannot fire the listeners a second time.
 */

#include "rt.h"

static int xt_exit_status_value = 0;
static xt_exit_hook xt_exit_hook_value = NULL;
static int xt_exit_hook_done = 0;

void xt_set_exit_status(int status) { xt_exit_status_value = status; }

int xt_get_exit_status(void) { return xt_exit_status_value; }

void xt_at_exit(xt_exit_hook hook) { xt_exit_hook_value = hook; }

int xt_finish(void) {
  if (!xt_exit_hook_done) {
    xt_exit_hook_done = 1;
    if (xt_exit_hook_value) xt_exit_hook_value();
  }
  return xt_exit_status_value;
}
