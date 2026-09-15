#pragma once
#include "core/IEmulatorCore.h"
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <new>

namespace reagba {
// Shared only with our renderer helper. No frames are serialized into JavaScript.
struct SharedFrame {
    pthread_mutex_t mutex;
    uint64_t sequence = 0;
    Frame pixels{};
};
inline bool LockFrame(SharedFrame* frame) {
    const int result=pthread_mutex_trylock(&frame->mutex);
    if(result==EOWNERDEAD){pthread_mutex_consistent(&frame->mutex);pthread_mutex_unlock(&frame->mutex);return false;}
    return result==0;
}
class FramePublisher {
    int fd_=-1;
    SharedFrame* frame_=nullptr;
public:
    FramePublisher() {
        fd_=memfd_create("reagba-native-frame",MFD_CLOEXEC);
        if(fd_<0)throw std::runtime_error("Cannot create native framebuffer sharing");
        if(ftruncate(fd_,sizeof(SharedFrame))!=0){close(fd_);fd_=-1;throw std::runtime_error("Cannot size framebuffer");}
        auto* memory=mmap(nullptr,sizeof(SharedFrame),PROT_READ|PROT_WRITE,MAP_SHARED,fd_,0);
        if(memory==MAP_FAILED){close(fd_);fd_=-1;throw std::runtime_error("Cannot map framebuffer");}
        frame_=new(memory)SharedFrame{};
        pthread_mutexattr_t attributes;pthread_mutexattr_init(&attributes);
        pthread_mutexattr_setpshared(&attributes,PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attributes,PTHREAD_MUTEX_ROBUST);
        const int result=pthread_mutex_init(&frame_->mutex,&attributes);pthread_mutexattr_destroy(&attributes);
        if(result){munmap(frame_,sizeof(SharedFrame));frame_=nullptr;close(fd_);fd_=-1;throw std::runtime_error("Cannot initialize framebuffer lock");}
    }
    ~FramePublisher(){if(frame_)munmap(frame_,sizeof(SharedFrame));if(fd_>=0)close(fd_);}
    int Descriptor() const {return fd_;}
    void Publish(const Frame& pixels){if(!LockFrame(frame_))return;frame_->pixels=pixels;++frame_->sequence;pthread_mutex_unlock(&frame_->mutex);}
};
}
