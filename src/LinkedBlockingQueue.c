#include "LinkedBlockingQueue.h"

// Destroy the linked blocking queue and associated mutex and event
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead) {
    LC_ASSERT(queueHead->shutdown || queueHead->draining || queueHead->lifetimeSize == 0);
    
    PltDeleteMutex(&queueHead->mutex);
    PltDeleteConditionVariable(&queueHead->cond);

    return queueHead->head;
}

// Flush the queue
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead) {
    PLINKED_BLOCKING_QUEUE_ENTRY head;

    PltLockMutex(&queueHead->mutex);

    // Save the old head
    head = queueHead->head;

    // Reinitialize the queue to empty
    if (head != NULL) {
        queueHead->head = NULL;
        queueHead->tail = NULL;
        queueHead->currentSize = 0;
    }
    else {
        LC_ASSERT(queueHead->tail == NULL);
        LC_ASSERT(queueHead->currentSize == 0);
    }

    PltUnlockMutex(&queueHead->mutex);

    return head;
}

// Linked blocking queue init
int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound) {
    int err;

    memset(queueHead, 0, sizeof(*queueHead));

    err = PltCreateMutex(&queueHead->mutex);
    if (err != 0) {
        return err;
    }

    err = PltCreateConditionVariable(&queueHead->cond, &queueHead->mutex);
    if (err != 0) {
        PltDeleteMutex(&queueHead->mutex);
        return err;
    }

    queueHead->sizeBound = sizeBound;

    return 0;
}

void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead) {
    PltLockMutex(&queueHead->mutex);
    queueHead->shutdown = true;
    PltUnlockMutex(&queueHead->mutex);
    PltSignalConditionVariable(&queueHead->cond);
}

void LbqSignalQueueDrain(PLINKED_BLOCKING_QUEUE queueHead) {
    PltLockMutex(&queueHead->mutex);
    queueHead->draining = true;
    PltUnlockMutex(&queueHead->mutex);
    PltSignalConditionVariable(&queueHead->cond);
}

void LbqSignalQueueUserWake(PLINKED_BLOCKING_QUEUE queueHead) {
    PltLockMutex(&queueHead->mutex);
    queueHead->pendingUserWake = true;
    PltUnlockMutex(&queueHead->mutex);
    PltSignalConditionVariable(&queueHead->cond);
}

int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead) {
    return queueHead->currentSize;
}

int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    return LbqOfferQueueItemPriority(queueHead, data, entry, NULL, NULL, 0, NULL);
}

int LbqOfferQueueItemPriority(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry,
        bool (*matches)(void*, void*), bool (*priority)(void*), int eventLimit, void** displaced) {
    bool wasEmpty;
    if (displaced != NULL) *displaced = NULL;
    
    entry->flink = NULL;
    entry->data = data;

    PltLockMutex(&queueHead->mutex);

    if (queueHead->shutdown || queueHead->draining) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    if (priority != NULL) {
        PLINKED_BLOCKING_QUEUE_ENTRY victim = NULL, firstEvent = NULL;
        bool important = priority(data);
        int events = 0;
        for (PLINKED_BLOCKING_QUEUE_ENTRY p = queueHead->head; p != NULL; p = p->flink) {
            if (important && matches(data, p->data)) victim = p;
            if (!priority(p->data)) {
                if (firstEvent == NULL) firstEvent = p;
                ++events;
            }
        }
        if (victim == NULL && (queueHead->currentSize == queueHead->sizeBound ||
                (!important && eventLimit > 0 && events >= eventLimit))) victim = firstEvent;
        if (victim != NULL) {
            if (victim->blink) victim->blink->flink = victim->flink;
            else queueHead->head = victim->flink;
            if (victim->flink) victim->flink->blink = victim->blink;
            else queueHead->tail = victim->blink;
            --queueHead->currentSize;
            *displaced = victim->data;
        }
    }

    if (queueHead->currentSize == queueHead->sizeBound) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_BOUND_EXCEEDED;
    }

    wasEmpty = queueHead->head == NULL;
    if (wasEmpty) {
        LC_ASSERT(queueHead->currentSize == 0);
        LC_ASSERT(queueHead->tail == NULL);
        queueHead->head = entry;
        queueHead->tail = entry;
        entry->blink = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize >= 1);
        LC_ASSERT(queueHead->head != NULL);
        queueHead->tail->flink = entry;
        entry->blink = queueHead->tail;
        queueHead->tail = entry;
    }

    queueHead->currentSize++;
    queueHead->lifetimeSize++;

    PltUnlockMutex(&queueHead->mutex);

    if (wasEmpty) {
        // Only call PltSignalConditionVariable() when transitioning from
        // empty -> non-empty to avoid a useless syscall for each new entry.
        PltSignalConditionVariable(&queueHead->cond);
    }

    return LBQ_SUCCESS;
}

// This must be synchronized with LbqFlushQueueItems by the caller
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    PltLockMutex(&queueHead->mutex);

    if (queueHead->shutdown) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    if (queueHead->head == NULL) {
        if (queueHead->draining) {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_INTERRUPTED;
        }
        else {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_NO_ELEMENT;
        }
    }

    *data = queueHead->head->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}

int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    return LbqPollQueueElementMatching(queueHead, data, NULL, NULL);
}

int LbqPollQueueElementMatching(PLINKED_BLOCKING_QUEUE queueHead, void** data,
        bool (*matches)(void*, void*), void* context) {
    PLINKED_BLOCKING_QUEUE_ENTRY entry;

    PltLockMutex(&queueHead->mutex);

    if (queueHead->shutdown) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    if (queueHead->head == NULL) {
        if (queueHead->draining) {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_INTERRUPTED;
        }
        else {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_NO_ELEMENT;
        }
    }

    if (matches != NULL && !matches(context, queueHead->head->data)) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_NO_ELEMENT;
    }

    entry = queueHead->head;
    queueHead->head = entry->flink;
    queueHead->currentSize--;
    if (queueHead->head == NULL) {
        LC_ASSERT(queueHead->currentSize == 0);
        queueHead->tail = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize != 0);
        queueHead->head->blink = NULL;
    }

    *data = entry->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}

int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    PLINKED_BLOCKING_QUEUE_ENTRY entry;

    PltLockMutex(&queueHead->mutex);

    // Wait for a waking condition: either data available or rundown
    while (queueHead->head == NULL && !queueHead->draining && !queueHead->shutdown && !queueHead->pendingUserWake) {
        PltWaitForConditionVariable(&queueHead->cond, &queueHead->mutex);
    }

    // If we're shutting down, abort immediately, even if there's data available
    if (queueHead->shutdown) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    // If this is a user requested wake, process it now
    if (queueHead->pendingUserWake) {
        queueHead->pendingUserWake = false;
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_USER_WAKE;
    }

    // If we're draining, only abort if we have no data available
    if (queueHead->draining && queueHead->head == NULL) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    // We should have bailed by this point if there was no data
    LC_ASSERT(queueHead->head != NULL);

    entry = queueHead->head;
    queueHead->head = entry->flink;
    queueHead->currentSize--;
    if (queueHead->head == NULL) {
        LC_ASSERT(queueHead->currentSize == 0);
        queueHead->tail = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize != 0);
        queueHead->head->blink = NULL;
    }

    *data = entry->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}
