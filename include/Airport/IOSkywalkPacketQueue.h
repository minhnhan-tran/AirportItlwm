//
//  IOSkywalkPacketQueue.h
//
//  Minimal declarations of IOSkywalkFamily's packet queue classes (macOS 26),
//  recovered from the kernel collection symbols. Only the STATIC factories and
//  NON-virtual members are declared on purpose: the vtable layout is not
//  reproduced, so these classes must never be subclassed or have virtual
//  methods (enable/disable/synchronize...) called through them. Objects are
//  created with withPool(), handed to the family through
//  IOSkywalkEthernetInterface::registerEthernetInterface (which enables them),
//  and released with OSObject::release().
//
//  Mangled symbols verified in com.apple.iokit.IOSkywalkFamily:
//    IOSkywalkTxSubmissionQueue::withPool  @0xffffff8002b11a86
//    IOSkywalkRxSubmissionQueue::withPool  @0xffffff8002b13054
//    IOSkywalkRxCompletionQueue::withPool  @0xffffff8002b13e04
//    IOSkywalkTxCompletionQueue::withPool  @0xffffff8002b17860
//

#ifndef IOSkywalkPacketQueue_h
#define IOSkywalkPacketQueue_h

#include <IOKit/IOService.h>
#include "IOSkywalkPacketBufferPool.h"

class IOSkywalkPacket;

class IOSkywalkPacketQueue : public OSObject {
public:
    // Non-virtual state getters were not needed; intentionally empty.
};

class IOSkywalkTxSubmissionQueue : public IOSkywalkPacketQueue {
public:
    // Called by the family with the packets the stack wants transmitted.
    // Return value: number of packets consumed (0 on failure).
    typedef uint32_t (*PacketAction)(OSObject *owner, IOSkywalkTxSubmissionQueue *queue,
                                     IOSkywalkPacket * const *packets, uint32_t count, void *refcon);

    static IOSkywalkTxSubmissionQueue *withPool(IOSkywalkPacketBufferPool *pool,
                                                uint32_t queueId, uint32_t capacity,
                                                OSObject *owner, PacketAction action,
                                                void *refcon, uint32_t options);
};

class IOSkywalkRxSubmissionQueue : public IOSkywalkPacketQueue {
public:
    typedef uint32_t (*PacketAction)(OSObject *owner, IOSkywalkRxSubmissionQueue *queue,
                                     IOSkywalkPacket * const *packets, uint32_t count, void *refcon);

    static IOSkywalkRxSubmissionQueue *withPool(IOSkywalkPacketBufferPool *pool,
                                                uint32_t queueId, uint32_t capacity, uint32_t extra,
                                                OSObject *owner, PacketAction action,
                                                void *refcon, uint32_t options);
};

class IOSkywalkRxCompletionQueue : public IOSkywalkPacketQueue {
public:
    typedef uint32_t (*PacketAction)(OSObject *owner, IOSkywalkRxCompletionQueue *queue,
                                     IOSkywalkPacket **packets, uint32_t count, void *refcon);

    static IOSkywalkRxCompletionQueue *withPool(IOSkywalkPacketBufferPool *pool,
                                                uint32_t queueId, uint32_t capacity,
                                                OSObject *owner, PacketAction action,
                                                void *refcon, uint32_t options);

    // Non-virtual: hand received packets to the stack.
    IOReturn enqueuePackets(IOSkywalkPacket * const *packets, uint32_t count, uint32_t flags);
};

class IOSkywalkTxCompletionQueue : public IOSkywalkPacketQueue {
public:
    typedef uint32_t (*PacketAction)(OSObject *owner, IOSkywalkTxCompletionQueue *queue,
                                     IOSkywalkPacket **packets, uint32_t count, void *refcon);

    static IOSkywalkTxCompletionQueue *withPool(IOSkywalkPacketBufferPool *pool,
                                                uint32_t queueId, uint32_t capacity,
                                                OSObject *owner, PacketAction action,
                                                void *refcon, uint32_t options);

    // Non-virtual: report transmitted packets back to the stack.
    IOReturn enqueuePackets(IOSkywalkPacket * const *packets, uint32_t count, uint32_t flags);
};

#endif /* IOSkywalkPacketQueue_h */
