//
//  IOSkywalkPacketBufferPool.h
//  itlwm
//
//  Created by qcwap on 2023/6/15.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef IOSkywalkPacketBufferPool_h
#define IOSkywalkPacketBufferPool_h

#include <IOKit/IOService.h>

class IOSkywalkMemorySegment;
class IOSkywalkMemorySegmentDescriptor;
class IOSkywalkPacket;
class IOSkywalkPacketBuffer;
class IOSkywalkPacketDescriptor;
class IOSkywalkPacketBufferDescriptor;

class IOSkywalkPacketBufferPool : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkPacketBufferPool)
    
public:
    struct PoolOptions {
        uint32_t packetCount;
        uint32_t bufferCount;
        uint32_t bufferSize;
        uint32_t maxBuffersPerPacket;
        uint32_t memorySegmentSize;
        uint32_t poolFlags;
        uint64_t pad;
    };
    
public:
    virtual void free() APPLE_KEXT_OVERRIDE;
    bool initWithName(char const*,void *,uint,IOSkywalkPacketBufferPool::PoolOptions const*);
    bool initWithName(char const*,OSObject *,uint,IOSkywalkPacketBufferPool::PoolOptions const*);
    IOReturn allocatePacket(IOSkywalkPacket **,uint);
    IOReturn allocatePacket(uint,IOSkywalkPacket **,uint);
    IOReturn allocatePackets(uint,uint *,IOSkywalkPacket **,uint);
    void deallocatePacket(IOSkywalkPacket *);
    void deallocatePackets(IOSkywalkPacket **,uint);
    void deallocatePacketList(IOSkywalkPacket *);
    void deallocatePacketChain(unsigned long long);
    IOReturn allocatePacketBuffer(IOSkywalkPacketBuffer **,uint);
    IOReturn allocatePacketBuffers(uint *,IOSkywalkPacketBuffer **,uint);
    void deallocatePacketBuffer(IOSkywalkPacketBuffer *);
    void deallocatePacketBuffers(IOSkywalkPacketBuffer **,uint);
    bool newPacket(IOSkywalkPacketDescriptor *,IOSkywalkPacket **);
    bool newPacketBuffer(IOSkywalkPacketBufferDescriptor *,IOSkywalkPacketBuffer **);
    bool newMemorySegment(IOSkywalkMemorySegmentDescriptor *,IOSkywalkMemorySegment **);
    
public:
    static IOSkywalkPacketBufferPool *withName(char const*,OSObject *,uint,IOSkywalkPacketBufferPool::PoolOptions const*);
    
public:
    uint8_t filter[0xB8];
};

#endif /* IOSkywalkPacketBufferPool_h */

