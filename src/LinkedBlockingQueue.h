#pragma once

#include "Platform.h"
#include "PlatformThreads.h"

#define LBQ_SUCCESS 0
#define LBQ_INTERRUPTED 1
#define LBQ_BOUND_EXCEEDED 2
#define LBQ_NO_ELEMENT 3
#define LBQ_USER_WAKE 4

typedef struct _LINKED_BLOCKING_QUEUE_ENTRY {
    struct _LINKED_BLOCKING_QUEUE_ENTRY* flink;
    struct _LINKED_BLOCKING_QUEUE_ENTRY* blink;
    void* data;
} LINKED_BLOCKING_QUEUE_ENTRY, *PLINKED_BLOCKING_QUEUE_ENTRY;

typedef struct _LINKED_BLOCKING_QUEUE {
    PLT_MUTEX mutex;
    PLT_COND cond;
    PLINKED_BLOCKING_QUEUE_ENTRY head;
    PLINKED_BLOCKING_QUEUE_ENTRY tail;
    int sizeBound;
    int currentSize;
    int lifetimeSize;
    bool shutdown;
    bool draining;
    bool pendingUserWake;
} LINKED_BLOCKING_QUEUE, *PLINKED_BLOCKING_QUEUE;

int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound);
int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry);
// Append an ordered batch atomically, or leave the queue entirely unchanged.
// Also require headroom free slots at admission; they are NOT reserved.
int LbqOfferQueueItems(PLINKED_BLOCKING_QUEUE queueHead, void* const* data,
                      PLINKED_BLOCKING_QUEUE_ENTRY const* entries, int count, int headroom);
int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead);
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueDrain(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueUserWake(PLINKED_BLOCKING_QUEUE queueHead);
int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead);
