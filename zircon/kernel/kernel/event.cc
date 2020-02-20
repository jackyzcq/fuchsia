// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Event wait and signal functions for threads.
 * @defgroup event Events
 *
 * An event is a subclass of a wait queue.
 *
 * Threads wait for events, with optional timeouts.
 *
 * Events are "signaled", releasing waiting threads to continue.
 * Signals may be one-shot signals (EVENT_FLAG_AUTOUNSIGNAL), in which
 * case one signal releases only one thread, at which point it is
 * automatically cleared. Otherwise, signals release all waiting threads
 * to continue immediately until the signal is manually cleared with
 * event_unsignal().
 *
 * @{
 */

#include "kernel/event.h"

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <sys/types.h>
#include <zircon/types.h>

#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>

/**
 * @brief  Destroy an event object.
 *
 * Event's resources are freed and it may no longer be
 * used until event_init() is called again.
 * Will panic if there are any threads still waiting.
 *
 * @param e        Event object to initialize
 */
void event_destroy(event_t* e) {
  DEBUG_ASSERT(e->magic == EVENT_MAGIC);

  e->magic = 0;
  e->result = INT_MAX;
  e->flags = 0;
  wait_queue_destroy(&e->wait);
}

static zx_status_t event_wait_worker(event_t* e, const Deadline& deadline, bool interruptable,
                                     uint signal_mask) {
  Thread* current_thread = get_current_thread();
  zx_status_t ret = ZX_OK;

  DEBUG_ASSERT(e->magic == EVENT_MAGIC);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  current_thread->interruptable_ = interruptable;

  if (e->result != INT_MAX) {
    ret = e->result;

    /* signaled, we're going to fall through */
    if (e->flags & EVENT_FLAG_AUTOUNSIGNAL) {
      /* autounsignal flag lets one thread fall through before unsignaling */
      e->result = INT_MAX;
    }
  } else {
    /* unsignaled, block here */
    ret = wait_queue_block_etc(&e->wait, deadline, signal_mask, ResourceOwnership::Normal);
  }

  current_thread->interruptable_ = false;

  return ret;
}

/**
 * @brief  Wait for event to be signaled
 *
 * If the event has already been signaled, this function
 * returns immediately.  Otherwise, the current thread
 * goes to sleep until the event object is signaled,
 * the deadline is reached, or the event object is destroyed
 * by another thread.
 *
 * @param e        Event object
 * @param deadline Deadline to abort at, in ns
 * @param interruptable  Allowed to interrupt if thread is signaled
 *
 * @return  0 on success, ZX_ERR_TIMED_OUT on timeout,
 *          other values depending on wait_result value
 *          when event_signal_etc is used.
 */
zx_status_t event_wait_deadline(event_t* e, zx_time_t deadline, bool interruptable) {
  return event_wait_worker(e, Deadline::no_slack(deadline), interruptable, 0);
}

/**
 * @brief  Wait for event to be signaled
 *
 * If the event has already been signaled, this function
 * returns immediately.  Otherwise, the current thread
 * goes to sleep until the event object is signaled or destroyed by another
 * thread, the deadline is reached, or the calling thread is interrupted.
 *
 * @param e        Event object
 * @param deadline Deadline to timeout at
 * @param slack    Allowed deviation from the deadline
 *
 * @return  0 on success, ZX_ERR_TIMED_OUT on timeout,
 *          other values depending on wait_result value
 *          when event_signal_etc is used.
 */
zx_status_t event_wait_interruptable(event_t* e, const Deadline& deadline) {
  return event_wait_worker(e, deadline, true, 0);
}

/**
 * @brief  Wait for event to be signaled, ignoring existing signals in
           |signal_mask|.
 *
 * If the event has already been signaled (except for signals in
 * |signal_mask|), this function returns immediately.
 * Otherwise, the current thread goes to sleep until the event object is
 * signaled, or the event object is destroyed by another thread.
 * There is no deadline, and the caller must be interruptable.
 *
 * @param e        Event object
 *
 * @return  0 on success, other values depending on wait_result value
 *          when event_signal_etc is used.
 */
zx_status_t event_wait_with_mask(event_t* e, uint signal_mask) {
  return event_wait_worker(e, Deadline::infinite(), true, signal_mask);
}

static int event_signal_internal(event_t* e, bool reschedule, zx_status_t wait_result)
    TA_REQ(thread_lock) {
  DEBUG_ASSERT(e->magic == EVENT_MAGIC);
  DEBUG_ASSERT(wait_result != INT_MAX);

  int wake_count = 0;

  if (e->result == INT_MAX) {
    if (e->flags & EVENT_FLAG_AUTOUNSIGNAL) {
      /* try to release one thread and leave unsignaled if successful */
      if ((wake_count = wait_queue_wake_one(&e->wait, reschedule, wait_result)) <= 0) {
        /*
         * if we didn't actually find a thread to wake up, go to
         * signaled state and let the next call to event_wait
         * unsignal the event.
         */
        e->result = wait_result;
      }
    } else {
      /* release all threads and remain signaled */
      e->result = wait_result;
      wake_count = wait_queue_wake_all(&e->wait, reschedule, wait_result);
    }
  }

  return wake_count;
}

/**
 * @brief  Signal an event
 *
 * Signals an event.  If EVENT_FLAG_AUTOUNSIGNAL is set in the event
 * object's flags, only one waiting thread is allowed to proceed.  Otherwise,
 * all waiting threads are allowed to proceed until such time as
 * event_unsignal() is called.
 *
 * @param e           Event object
 * @param reschedule  If true, waiting thread(s) are executed immediately,
 *                    and the current thread resumes only after the
 *                    waiting threads have been satisfied. If false,
 *                    waiting threads are placed at the head of the run
 *                    queue.
 * @param wait_result What status event_wait_deadline will return to the
 *                    thread or threads that are woken up.
 *
 * @return  Returns the number of threads that have been unblocked.
 */
int event_signal_etc(event_t* e, bool reschedule, zx_status_t wait_result) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return event_signal_internal(e, reschedule, wait_result);
}

/**
 * @brief  Signal an event
 *
 * Signals an event.  If EVENT_FLAG_AUTOUNSIGNAL is set in the event
 * object's flags, only one waiting thread is allowed to proceed.  Otherwise,
 * all waiting threads are allowed to proceed until such time as
 * event_unsignal() is called.
 *
 * @param e           Event object
 * @param reschedule  If true, waiting thread(s) are executed immediately,
 *                    and the current thread resumes only after the
 *                    waiting threads have been satisfied. If false,
 *                    waiting threads are placed at the head of the run
 *                    queue.
 *
 * @return  Returns the number of threads that have been unblocked.
 */
int event_signal(event_t* e, bool reschedule) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return event_signal_internal(e, reschedule, ZX_OK);
}

/* same as above, but the thread lock must already be held */
int event_signal_thread_locked(event_t* e) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  return event_signal_internal(e, false, ZX_OK);
}

/**
 * @brief  Clear the "signaled" property of an event
 *
 * Used mainly for event objects without the EVENT_FLAG_AUTOUNSIGNAL
 * flag.  Once this function is called, threads that call event_wait()
 * functions will once again need to wait until the event object
 * is signaled.
 *
 * @param e  Event object
 *
 * @return  Returns ZX_OK on success.
 */
zx_status_t event_unsignal(event_t* e) {
  DEBUG_ASSERT(e->magic == EVENT_MAGIC);

  e->result = INT_MAX;

  return ZX_OK;
}
