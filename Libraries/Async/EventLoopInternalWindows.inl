// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.

#include <WinSock2.h>
// Order is important
#include <MSWSock.h> // AcceptEx
#pragma comment(lib, "Mswsock.lib")

#include "EventLoop.h"
#include "EventLoopWindows.h"

#include "EventLoopInternalWindowsAPI.h"

NTSetInformationFile pNtSetInformationFile = NULL;

struct SC::Async::ProcessExitInternal
{
    EventLoopWindowsOverlapped overlapped;
    EventLoopWindowsWaitHandle waitHandle;
};

struct SC::EventLoop::Internal
{
    FileDescriptor loopFd;
    Async          wakeUpAsync;

    EventLoopWindowsOverlapped wakeUpOverlapped = {&wakeUpAsync};

    ~Internal() { SC_TRUST_RESULT(close()); }
    [[nodiscard]] ReturnCode close() { return loopFd.handle.close(); }

    [[nodiscard]] ReturnCode createEventLoop()
    {
        HANDLE newQueue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        if (newQueue == INVALID_HANDLE_VALUE)
        {
            // TODO: Better CreateIoCompletionPort error handling
            return "EventLoop::Internal::createEventLoop() - CreateIoCompletionPort"_a8;
        }
        SC_TRY_IF(loopFd.handle.assign(newQueue));
        return true;
    }

    [[nodiscard]] ReturnCode createWakeup(EventLoop& loop)
    {
        SC_UNUSED(loop);
        // Optimization: we avoid one indirect function call, see runCompletionFor checking if async == wakeUpAsync
        // wakeUpAsync.callback.bind<Internal, &Internal::runCompletionForWakeUp>(this);
        wakeUpAsync.operation.assignValue(Async::WakeUp());
        // No need to register it with EventLoop as we're calling PostQueuedCompletionStatus manually
        return true;
    }

    [[nodiscard]] Async* getAsync(OVERLAPPED_ENTRY& event) const
    {
        return EventLoopWindowsOverlapped::getUserDataFromOverlapped<Async>(event.lpOverlapped);
    }

    [[nodiscard]] void* getUserData(OVERLAPPED_ENTRY& event) const
    {
        return reinterpret_cast<void*>(event.lpCompletionKey);
    }

    void runCompletionForWakeUp(AsyncResult& result) { result.eventLoop.executeWakeUps(); }

    [[nodiscard]] ReturnCode runCompletionFor(AsyncResult& result, const OVERLAPPED_ENTRY& entry)
    {
        SC_UNUSED(entry);
        switch (result.async.operation.type)
        {
        case Async::Type::Timeout: {
            // This is used for overlapped notifications (like FileSystemWatcher)
            // It would be probably better to create a dedicated enum type for Overlapped Notifications
            break;
        }
        case Async::Type::Read: {
            // TODO: do the actual read operation here
            break;
        }
        case Async::Type::WakeUp: {
            if (&result.async == &wakeUpAsync)
            {
                runCompletionForWakeUp(result);
            }
            break;
        }
        case Async::Type::ProcessExit: {
            // Process has exited
            // Gather exit code
            Async::ProcessExit&         processExit  = result.async.operation.fields.processExit;
            Async::ProcessExitInternal& procInternal = processExit.opaque.get();
            SC_TRY_IF(procInternal.waitHandle.close());
            DWORD processStatus;
            if (GetExitCodeProcess(processExit.handle, &processStatus) == FALSE)
            {
                return "GetExitCodeProcess failed"_a8;
            }
            result.result.fields.processExit.exitStatus.status = static_cast<int32_t>(processStatus);

            break;
        }
        case Async::Type::Accept: {
            Async::Accept& accept = result.async.operation.fields.accept;

            DWORD transferred = 0;
            DWORD flags       = 0;
            BOOL  res         = ::WSAGetOverlappedResult(accept.handle, &accept.support->overlapped.get().overlapped,
                                                         &transferred, FALSE, &flags);
            if (res == FALSE)
            {
                // TODO: report error
                return "WSAGetOverlappedResult error"_a8;
            }
            ::setsockopt(result.result.fields.accept.acceptedClient, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, nullptr, 0);
            SC_TRY_IF(
                accept.support->clientSocket.get(result.result.fields.accept.acceptedClient, "Invalid handle"_a8));
            accept.support->clientSocket.detach();
            return true;
        }
        }
        return true;
    }
};

SC::ReturnCode SC::EventLoop::wakeUpFromExternalThread()
{
    Internal&            self = internal.get();
    FileDescriptorNative loopNativeDescriptor;
    SC_TRY_IF(self.loopFd.handle.get(loopNativeDescriptor, "watchInputs - Invalid Handle"_a8));

    if (PostQueuedCompletionStatus(loopNativeDescriptor, 0, 0, &self.wakeUpOverlapped.overlapped) == FALSE)
    {
        return "EventLoop::wakeUpFromExternalThread() - PostQueuedCompletionStatus"_a8;
    }
    return true;
}

struct SC::EventLoop::KernelQueue
{
    static constexpr int totalNumEvents = 128;
    OVERLAPPED_ENTRY     events[totalNumEvents];
    ULONG                newEvents = 0;

    [[nodiscard]] ReturnCode stageAsync(EventLoop& eventLoop, Async& async)
    {
        HANDLE loopHandle;
        SC_TRY_IF(eventLoop.internal.get().loopFd.handle.get(loopHandle, "loop handle"_a8));
        switch (async.operation.type)
        {
        case Async::Type::Timeout: eventLoop.activeTimers.queueBack(async); return true;
        case Async::Type::WakeUp: eventLoop.activeWakeUps.queueBack(async); return true;
        case Async::Type::Read: SC_TRY_IF(startReadWatcher(*async.operation.unionAs<Async::Read>())); break;
        case Async::Type::ProcessExit: SC_TRY_IF(startProcessExitWatcher(async)); break;
        case Async::Type::Accept:
            SC_TRY_IF(startAcceptWatcher(loopHandle, *async.operation.unionAs<Async::Accept>()));
            break;
        }
        // On Windows we push directly to activeHandles and not stagedHandles as we've already created kernel objects
        eventLoop.activeHandles.queueBack(async);
        return true;
    }

    [[nodiscard]] static ReturnCode startReadWatcher(Async::Read& async)
    {
        SC_UNUSED(async);
        return true;
    }

    [[nodiscard]] static ReturnCode stopReadWatcher(Async::Read& async)
    {
        SC_UNUSED(async);
        return true;
    }

    [[nodiscard]] static ReturnCode startAcceptWatcher(HANDLE loopHandle, Async::Accept& asyncAccept)
    {
        SC_TRY_IF(Network::init());
        HANDLE listenHandle = reinterpret_cast<HANDLE>(asyncAccept.handle);
        HANDLE iocp         = ::CreateIoCompletionPort(listenHandle, loopHandle, 0, 0);
        SC_TRY_MSG(iocp == loopHandle, "startAcceptWatcher CreateIoCompletionPort failed"_a8);
        return true;
    }

    [[nodiscard]] static ReturnCode stopAcceptWatcher(Async::Accept& asyncAccept)
    {
        // TODO: Make this threadsafe
        if (!pNtSetInformationFile)
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            pNtSetInformationFile =
                reinterpret_cast<NTSetInformationFile>(GetProcAddress(ntdll, "NtSetInformationFile"));
        }

        HANDLE listenHandle = reinterpret_cast<HANDLE>(asyncAccept.handle);

        struct FILE_COMPLETION_INFORMATION file_completion_info;
        file_completion_info.Key  = NULL;
        file_completion_info.Port = NULL;

        struct IO_STATUS_BLOCK status_block;
        memset(&status_block, 0, sizeof(status_block));

        NTSTATUS status = pNtSetInformationFile(listenHandle, &status_block, &file_completion_info,
                                                sizeof(file_completion_info), FileReplaceCompletionInformation);

        if (!status)
        {
            return "FileReplaceCompletionInformation failed"_a8;
        }
        // This will cause one more event loop run with GetOverlappedIO failing
        return asyncAccept.support->clientSocket.close();
    }

    [[nodiscard]] static ReturnCode startProcessExitWatcher(Async& async)
    {
        const ProcessNative         processHandle   = async.operation.fields.processExit.handle;
        Async::ProcessExitInternal& processInternal = async.operation.fields.processExit.opaque.get();
        processInternal.overlapped.userData         = &async;
        HANDLE waitHandle;
        BOOL result = RegisterWaitForSingleObject(&waitHandle, processHandle, KernelQueue::processExitCallback, &async,
                                                  INFINITE, WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE);
        if (result == FALSE)
        {
            return "RegisterWaitForSingleObject failed"_a8;
        }
        return processInternal.waitHandle.assign(waitHandle);
    }

    [[nodiscard]] static ReturnCode stopProcessExitWatcher(Async::ProcessExit& async)
    {
        return async.opaque.get().waitHandle.close();
    }

    // This is executed on windows thread pool
    static void CALLBACK processExitCallback(void* data, BOOLEAN timeoutOccurred)
    {
        SC_UNUSED(timeoutOccurred);
        Async&               async = *static_cast<Async*>(data);
        FileDescriptorNative loopNativeDescriptor;
        SC_TRUST_RESULT(async.eventLoop->getLoopFileDescriptor(loopNativeDescriptor));
        Async::ProcessExitInternal& internal = async.operation.fields.processExit.opaque.get();

        if (PostQueuedCompletionStatus(loopNativeDescriptor, 0, 0, &internal.overlapped.overlapped) == FALSE)
        {
            // TODO: Report error?
            // return "EventLoop::wakeUpFromExternalThread() - PostQueuedCompletionStatus"_a8;
        }
    }

    [[nodiscard]] ReturnCode pollAsync(EventLoop& self, PollMode pollMode)
    {
        const TimeCounter*   nextTimer = self.findEarliestTimer();
        FileDescriptorNative loopNativeDescriptor;
        SC_TRY_IF(self.internal.get().loopFd.handle.get(loopNativeDescriptor,
                                                        "EventLoop::Internal::poll() - Invalid Handle"_a8));
        IntegerMilliseconds timeout;
        if (nextTimer)
        {
            if (nextTimer->isLaterThanOrEqualTo(self.loopTime))
            {
                timeout = nextTimer->subtractApproximate(self.loopTime).inRoundedUpperMilliseconds();
            }
        }
        const BOOL success = GetQueuedCompletionStatusEx(
            loopNativeDescriptor, events, static_cast<ULONG>(SizeOfArray(events)), &newEvents,
            nextTimer or pollMode == PollMode::NoWait ? static_cast<ULONG>(timeout.ms) : INFINITE, FALSE);
        if (not success and GetLastError() != WAIT_TIMEOUT)
        {
            // TODO: GetQueuedCompletionStatusEx error handling
            return "KernelQueue::poll() - GetQueuedCompletionStatusEx error"_a8;
        }
        if (nextTimer)
        {
            self.executeTimers(*this, *nextTimer);
        }
        return true;
    }

    [[nodiscard]] static ReturnCode activateAsync(EventLoop& eventLoop, Async& async)
    {
        HANDLE loopHandle;
        SC_TRY_IF(eventLoop.internal.get().loopFd.handle.get(loopHandle, "loop handle"_a8));
        switch (async.operation.type)
        {
        case Async::Type::Accept: return activateAcceptWatcher(loopHandle, async);
        default: break;
        }
        return true;
    }

    [[nodiscard]] static ReturnCode activateAcceptWatcher(HANDLE loopHandle, Async& async)
    {
        Async::Accept& asyncAccept  = async.operation.fields.accept;
        SOCKET         clientSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                                                   WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        SC_TRY_MSG(clientSocket != INVALID_SOCKET, "WSASocketW failed"_a8);
        auto   deferDeleteSocket = MakeDeferred([&] { closesocket(clientSocket); });
        HANDLE socketHandle      = reinterpret_cast<HANDLE>(clientSocket);
        HANDLE iocp              = ::CreateIoCompletionPort(socketHandle, loopHandle, 0, 0);
        SC_TRY_MSG(iocp == loopHandle, "CreateIoCompletionPort client"_a8);

        ::SetFileCompletionNotificationModes(socketHandle,
                                             FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE);

        static_assert(sizeof(Async::AcceptSupport::acceptBuffer) == sizeof(struct sockaddr_storage) * 2 + 32);
        Async::AcceptSupport& support = *asyncAccept.support;

        EventLoopWindowsOverlapped& overlapped = support.overlapped.get();

        overlapped.userData = &async;

        DWORD sync_bytes_read = 0;

        BOOL res;
        res =
            ::AcceptEx(asyncAccept.handle, clientSocket, support.acceptBuffer, 0, sizeof(struct sockaddr_storage) + 16,
                       sizeof(struct sockaddr_storage) + 16, &sync_bytes_read, &overlapped.overlapped);
        if (res == FALSE and WSAGetLastError() != WSA_IO_PENDING)
        {
            // TODO: Check AcceptEx WSA error codes
            return "AcceptEx failed"_a8;
        }
        // TODO: Handle synchronous success
        deferDeleteSocket.disarm();
        return support.clientSocket.assign(clientSocket);
    }

    [[nodiscard]] static ReturnCode cancelAsync(EventLoop& eventLoop, Async& async)
    {
        SC_UNUSED(eventLoop);
        switch (async.operation.type)
        {
        case Async::Type::Timeout: return true;
        case Async::Type::WakeUp: return true;
        case Async::Type::Read: return stopReadWatcher(*async.operation.unionAs<Async::Read>());
        case Async::Type::ProcessExit: return stopProcessExitWatcher(*async.operation.unionAs<Async::ProcessExit>());
        case Async::Type::Accept: return stopAcceptWatcher(*async.operation.unionAs<Async::Accept>());
        }
        return false;
    }

  private:
};

template <>
void SC::OpaqueFuncs<SC::EventLoopWindowsOverlappedTraits>::construct(Handle& buffer)
{
    new (&buffer.reinterpret_as<Object>(), PlacementNew()) Object();
}
template <>
void SC::OpaqueFuncs<SC::EventLoopWindowsOverlappedTraits>::destruct(Object& obj)
{
    obj.~Object();
}
template <>
void SC::OpaqueFuncs<SC::EventLoopWindowsOverlappedTraits>::moveConstruct(Handle& buffer, Object&& obj)
{
    new (&buffer.reinterpret_as<Object>(), PlacementNew()) Object(move(obj));
}
template <>
void SC::OpaqueFuncs<SC::EventLoopWindowsOverlappedTraits>::moveAssign(Object& pthis, Object&& obj)
{
    pthis = move(obj);
}
