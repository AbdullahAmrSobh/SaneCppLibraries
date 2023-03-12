// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#include <Windows.h>

struct SC::Loop::Internal
{
    bool           inited = false;
    FileDescriptor loopFd;

    Vector<FileDescriptorNative> watchersQueue;

    OVERLAPPED async;
    ~Internal() { SC_TRUST_RESULT(close()); }
    [[nodiscard]] ReturnCode close() { return loopFd.handle.close(); }

    [[nodiscard]] ReturnCode createLoop()
    {
        HANDLE newQueue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        if (newQueue == INVALID_HANDLE_VALUE)
        {
            // TODO: Better CreateIoCompletionPort error handling
            return "Loop::Internal::createLoop() - CreateIoCompletionPort"_a8;
        }
        SC_TRY_IF(loopFd.handle.assign(newQueue));
        return true;
    }

    [[nodiscard]] ReturnCode createLoopAsyncWakeup() { return true; }

    struct KernelQueue
    {
        static constexpr int totalNumEvents = 128;
        OVERLAPPED_ENTRY     events[totalNumEvents];
        ULONG                newEvents = 0;

        [[nodiscard]] ReturnCode addReadWatcher(FileDescriptor& loopFd, FileDescriptorNative fileDescriptor)
        {
            return true;
        }

        [[nodiscard]] ReturnCode poll(FileDescriptor& loopFd, IntegerMilliseconds* actualTimeout)
        {
            FileDescriptorNative loopNativeDescriptor;
            SC_TRY_IF(loopFd.handle.get(loopNativeDescriptor, "KernelQueue::poll() - Invalid Handle"_a8));

            const BOOL success = GetQueuedCompletionStatusEx(
                loopNativeDescriptor, events, static_cast<ULONG>(ConstantArraySize(events)), &newEvents,
                actualTimeout ? static_cast<ULONG>(actualTimeout->ms) : INFINITE, FALSE);
            if (not success and GetLastError() != WAIT_TIMEOUT)
            {
                // TODO: GetQueuedCompletionStatusEx error handling
                return "KernelQueue::poll() - GetQueuedCompletionStatusEx error"_a8;
            }
            return true;
        }

      private:
    };
};

SC::ReturnCode SC::Loop::wakeUpFromExternalThread()
{
    Internal&            self = internal.get();
    FileDescriptorNative loopNativeDescriptor;
    SC_TRY_IF(self.loopFd.handle.get(loopNativeDescriptor, "watchInputs - Invalid Handle"_a8));

    if (not PostQueuedCompletionStatus(loopNativeDescriptor, 0, 0, nullptr))
    {
        return "Loop::wakeUpFromExternalThread() - PostQueuedCompletionStatus"_a8;
    }
    return true;
}
