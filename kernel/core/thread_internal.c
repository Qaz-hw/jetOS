/*
 * Institute for System Programming of the Russian Academy of Sciences
 * Copyright (C) 2016 ISPRAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, Version 3.
 *
 * This program is distributed in the hope # that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License version 3 for more details.
 */

#include "thread_internal.h"
#include <core/sched_arinc.h>

pok_bool_t thread_create(pok_thread_t* t)
{
    pok_partition_arinc_t* part = current_partition_arinc;
    
    t->init_stack_addr = (void*)pok_thread_stack_addr(
        part->base_part.space_id,
        t->user_stack_size,
        &part->user_stack_state);
    
    if(t->init_stack_addr == 0) return FALSE;
    
    /* 
     * Do not modify stack here: it will be filled when thread will run.
     */
    t->sp = 0;
    t->entry_sp_user = 0;
    
    t->priority = t->base_priority;
    t->state = POK_STATE_STOPPED;
    
    t->suspended = FALSE;
    
    delayed_event_init(&t->thread_deadline_event);
    delayed_event_init(&t->thread_delayed_event);
    INIT_LIST_HEAD(&t->wait_elem);
    INIT_LIST_HEAD(&t->eligible_elem);
    INIT_LIST_HEAD(&t->error_elem);
    
    return TRUE;
}

/* 
 * Return true if thread is eligible.
 * 
 * Note: Main thread and error thread are NEVER eligible.
 */
static pok_bool_t thread_is_eligible(pok_thread_t* t)
{
    return !list_empty(&t->eligible_elem);
}

/* 
 * Set thread eligible for running.
 * 
 * Thread shouldn't be eligible already.
 * 
 * Should be executed with local preemption disabled.
 */
static void thread_set_eligible(pok_thread_t* t)
{
    pok_thread_t* other_thread;
    pok_partition_arinc_t* part = current_partition_arinc;

    assert(part->mode == POK_PARTITION_MODE_NORMAL);
    assert(!thread_is_eligible(t));

    list_for_each_entry(other_thread,
        &part->eligible_threads,
        eligible_elem)
    {
        if(other_thread->priority < t->priority)
        {
            list_add_tail(&t->eligible_elem, &other_thread->eligible_elem);
            goto out;
        }

    }
    list_add_tail(&t->eligible_elem, &part->eligible_threads);

out:
    if(t->eligible_elem.prev == &part->eligible_threads)
    {
        // Thread is inserted into the first position.
        pok_sched_local_invalidate(); 
    }
}

void thread_yield(pok_thread_t *t)
{
    pok_partition_arinc_t* part = current_partition_arinc;
    if(thread_is_eligible(t))
    {
        // TODO: Improve performance
        if(t->eligible_elem.prev == &part->eligible_threads)
        {
            // Thread is removed from the first position.
            pok_sched_local_invalidate(); 
        }
        list_del_init(&t->eligible_elem);
        thread_set_eligible(t);
    }
}

/* 
 * Set thread not eligible for running, if it was.
 * 
 * Should be executed with local preemption disabled.
 */
static void thread_set_uneligible(pok_thread_t* t)
{
    pok_partition_arinc_t* part = current_partition_arinc;
    if(!list_empty(&t->eligible_elem))
    {
        if(t->eligible_elem.prev == &part->eligible_threads)
        {
            // Thread is removed from the first position.
            pok_sched_local_invalidate(); 
        }
	list_del_init(&t->eligible_elem);
    }
}

void thread_set_deadline(pok_thread_t* t, pok_time_t deadline_time)
{
    delayed_event_add(&t->thread_deadline_event, deadline_time,
        &current_partition_arinc->queue_deadline);
}

void thread_stop(pok_thread_t* t)
{
    pok_partition_arinc_t* part = current_partition_arinc;

    assert(t->state != POK_STATE_STOPPED);
    t->state = POK_STATE_STOPPED;

    thread_set_uneligible(t);

    // Remove thread from all queues except one for error handler.
    thread_delay_event_cancel(t);

    delayed_event_remove(&t->thread_deadline_event);

    pok_thread_wq_remove(t);

    if(part->lock_level && part->thread_locked == t)
    {
        current_partition_arinc->lock_level = 0;
	pok_sched_local_invalidate(); // Thread could be non-highest priority thread when stopped.
    }

    if(t->is_unrecoverable)
    {
        t->is_unrecoverable = FALSE;
        part->nthreads_unrecoverable--;
    }
}

void thread_start(pok_thread_t* t)
{
    t->state = POK_STATE_RUNNABLE;

    if(!t->suspended)
    {
        thread_set_eligible(t);
    }
}

void thread_wait(pok_thread_t* t)
{
    assert(current_partition_arinc->mode == POK_PARTITION_MODE_NORMAL);

    t->state = POK_STATE_WAITING;
    thread_set_uneligible(t);
}

void thread_wake_up(pok_thread_t* t)
{
    assert(t->state == POK_STATE_WAITING);

    t->state = POK_STATE_RUNNABLE;

    // For the case 3. Cancel possible timeout.
    thread_delay_event_cancel(t);

    if(!list_empty(&t->wait_elem))
    {
        // Case 2.

        // Cancel wait on object.
        list_del_init(&t->wait_elem);
        // Set flag that we has been interrupted by timeout.
        t->wait_private = (void*)(-(unsigned long)POK_ERRNO_TIMEOUT);
    }

    if(!t->suspended)
    {
        thread_set_eligible(t);
    }
}


void thread_wait_timed(pok_thread_t *thread, pok_time_t time)
{
    assert(thread);
    assert(!pok_time_is_infinity(time));

    thread_wait(thread);

    thread_delay_event(thread, time, &thread_wake_up);
}


void thread_suspend(pok_thread_t* t)
{
    t->suspended = TRUE;
    thread_set_uneligible(t);
}

// Special function for resume thread after suspension time is over.
static void thread_resume_waited(pok_thread_t* t)
{
    assert(t->state == POK_STATE_WAITING);

    t->suspended = FALSE;
    t->state = POK_STATE_RUNNABLE;

    // Set flag that we has been interrupted by timeout.
    t->wait_private = (void*)(-(long)POK_ERRNO_TIMEOUT);

    thread_set_eligible(t);
}

void thread_suspend_timed(pok_thread_t* t, pok_time_t time)
{
    t->suspended = TRUE;
    thread_set_uneligible(t);
    thread_wait(t);
    thread_delay_event(t, POK_GETTICK() + time, &thread_resume_waited);
}


void thread_resume(pok_thread_t* t)
{
    t->suspended = FALSE;
    if(delayed_event_is_active(&t->thread_delayed_event)
	&& t->thread_delayed_func == &thread_resume_waited)
    {
	// We are waited on timer for suspencion. Cancel that waiting.
	t->state = POK_STATE_RUNNABLE;

	thread_delay_event_cancel(t);

        // Set flag that we doesn't hit timeout.
        t->wait_private = 0;
    }

    if(t->state == POK_STATE_RUNNABLE)
    {
        thread_set_eligible(t);
    }
}
