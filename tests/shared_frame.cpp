#include "extension/Linux/SharedFrame.h"
#include <sys/wait.h>
#include <iostream>
using namespace reagba;
int main() {
    try {
        FramePublisher publisher;
        auto* frame=static_cast<SharedFrame*>(mmap(nullptr,sizeof(SharedFrame),PROT_READ|PROT_WRITE,MAP_SHARED,publisher.Descriptor(),0));
        if(frame==MAP_FAILED)throw std::runtime_error("map failed");
        Frame source{};source.fill(0xFFA1B2C3);publisher.Publish(source);
        const auto child=fork();if(child<0)throw std::runtime_error("fork failed");
        if(!child){if(!LockFrame(frame))_exit(2);const bool valid=frame->sequence==1&&frame->pixels==source;pthread_mutex_unlock(&frame->mutex);_exit(valid?0:3);}
        int status=0;waitpid(child,&status,0);if(!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("shared pixels mismatch");
        const auto owner=fork();if(owner<0)throw std::runtime_error("fork failed");
        if(!owner){if(!LockFrame(frame))_exit(4);_exit(0);}
        waitpid(owner,&status,0);
        // A crashed renderer holding the lock cannot stall REAPER or corrupt the next frame.
        publisher.Publish(source);source.fill(0xFF001122);publisher.Publish(source);
        if(!LockFrame(frame))throw std::runtime_error("robust mutex recovery failed");
        bool valid=frame->pixels==source&&frame->sequence==2;pthread_mutex_unlock(&frame->mutex);munmap(frame,sizeof(SharedFrame));
        if(!valid)throw std::runtime_error("recovered frame mismatch");
        std::cout<<"Native frame transfer and crashed renderer recovery passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
