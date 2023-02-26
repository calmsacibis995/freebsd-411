/*-
 * Copyright (c) 2000 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/sys/kern/subr_taskqueue.c,v 1.1.2.3 2003/09/10 00:40:39 ken Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/taskqueue.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kthread.h>

#include <machine/ipl.h>

MALLOC_DEFINE(M_TASKQUEUE, "taskqueue", "Task Queues");

static STAILQ_HEAD(taskqueue_list, taskqueue) taskqueue_queues;
static struct proc *taskqueue_thread_proc;

struct taskqueue {
	STAILQ_ENTRY(taskqueue)	tq_link;
	STAILQ_HEAD(, task)	tq_queue;
	const char		*tq_name;
	taskqueue_enqueue_fn	tq_enqueue;
	void			*tq_context;
	int			tq_draining;
};

struct taskqueue *
taskqueue_create(const char *name, int mflags,
		 taskqueue_enqueue_fn enqueue, void *context)
{
	struct taskqueue *queue;
	static int once = 1;
	int s;

	queue = malloc(sizeof(struct taskqueue), M_TASKQUEUE, mflags);
	if (!queue)
		return 0;
	STAILQ_INIT(&queue->tq_queue);
	queue->tq_name = name;
	queue->tq_enqueue = enqueue;
	queue->tq_context = context;
	queue->tq_draining = 0;

	s = splhigh();
	if (once) {
		STAILQ_INIT(&taskqueue_queues);
		once = 0;
	}
	STAILQ_INSERT_TAIL(&taskqueue_queues, queue, tq_link);
	splx(s);

	return queue;
}

void
taskqueue_free(struct taskqueue *queue)
{
	int s = splhigh();
	queue->tq_draining = 1;
	splx(s);

	taskqueue_run(queue);

	s = splhigh();
	STAILQ_REMOVE(&taskqueue_queues, queue, taskqueue, tq_link);
	splx(s);

	free(queue, M_TASKQUEUE);
}

struct taskqueue *
taskqueue_find(const char *name)
{
	struct taskqueue *queue;
	int s;

	s = splhigh();
	STAILQ_FOREACH(queue, &taskqueue_queues, tq_link)
		if (!strcmp(queue->tq_name, name)) {
			splx(s);
			return queue;
		}
	splx(s);
	return 0;
}

int
taskqueue_enqueue(struct taskqueue *queue, struct task *task)
{
	struct task *ins;
	struct task *prev;

	int s = splhigh();

	/*
	 * Don't allow new tasks on a queue which is being freed.
	 */
	if (queue->tq_draining) {
		splx(s);
		return EPIPE;
	}

	/*
	 * Count multiple enqueues.
	 */
	if (task->ta_pending) {
		task->ta_pending++;
		splx(s);
		return 0;
	}

	/*
	 * Optimise the case when all tasks have the same priority.
	 */
	prev = STAILQ_LAST(&queue->tq_queue, task, ta_link);
	if (!prev || prev->ta_priority >= task->ta_priority) {
		STAILQ_INSERT_TAIL(&queue->tq_queue, task, ta_link);
	} else {
		prev = 0;
		for (ins = STAILQ_FIRST(&queue->tq_queue); ins;
		     prev = ins, ins = STAILQ_NEXT(ins, ta_link))
			if (ins->ta_priority < task->ta_priority)
				break;

		if (prev)
			STAILQ_INSERT_AFTER(&queue->tq_queue, prev, task, ta_link);
		else
			STAILQ_INSERT_HEAD(&queue->tq_queue, task, ta_link);
	}

	task->ta_pending = 1;
	if (queue->tq_enqueue)
		queue->tq_enqueue(queue->tq_context);

	splx(s);

	return 0;
}

void
taskqueue_run(struct taskqueue *queue)
{
	int s;
	struct task *task;
	int pending;

	s = splhigh();
	while (STAILQ_FIRST(&queue->tq_queue)) {
		/*
		 * Carefully remove the first task from the queue and
		 * zero its pending count.
		 */
		task = STAILQ_FIRST(&queue->tq_queue);
		STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
		pending = task->ta_pending;
		task->ta_pending = 0;
		splx(s);

		task->ta_func(task->ta_context, pending);

		s = splhigh();
	}
	splx(s);
}

static void
taskqueue_swi_enqueue(void *context)
{
	setsofttq();
}

static void
taskqueue_swi_run(void)
{
	taskqueue_run(taskqueue_swi);
}

static void
taskqueue_kthread(void *arg)
{
	int s;

	for (;;) {
		taskqueue_run(taskqueue_thread);
		s = splhigh();
		if (STAILQ_EMPTY(&taskqueue_thread->tq_queue))
			tsleep(&taskqueue_thread, PWAIT, "tqthr", 0);
		splx(s);
	}
}

static void
taskqueue_thread_enqueue(void *context)
{
	wakeup(&taskqueue_thread);
}

TASKQUEUE_DEFINE(swi, taskqueue_swi_enqueue, 0,
		 register_swi(SWI_TQ, taskqueue_swi_run));
TASKQUEUE_DEFINE(thread, taskqueue_thread_enqueue, 0,
		 kthread_create(taskqueue_kthread, NULL,
		 &taskqueue_thread_proc, "taskqueue"));
