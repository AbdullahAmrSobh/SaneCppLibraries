// Copyright (c) Stefano Cristiano
// SPDX-License-Identifier: MIT

#include "../../Foundation/Deferred.h"
#include "../Async.h"

#if SC_ASYNC_USE_EPOLL

#include <errno.h>        // For error handling
#include <fcntl.h>        // For fcntl function (used for setting non-blocking mode)
#include <signal.h>       // For signal-related functions
#include <sys/epoll.h>    // For epoll functions
#include <sys/signalfd.h> // For signalfd functions
#include <sys/socket.h>   // For socket-related functions
#include <sys/stat.h>

#else

#include <errno.h>     // For error handling
#include <netdb.h>     // socketlen_t/getsocketopt/send/recv
#include <sys/event.h> // kqueue
#include <sys/time.h>  // timespec
#include <sys/wait.h>  // WIFEXITED / WEXITSTATUS
#include <unistd.h>    // read/write/pread/pwrite

#endif

struct SC::AsyncEventLoop::InternalPosix
{
    FileDescriptor loopFd;

    AsyncFilePoll  wakeupPoll;
    PipeDescriptor wakeupPipe;
#if SC_ASYNC_USE_EPOLL
    FileDescriptor signalProcessExitDescriptor;
    AsyncFilePoll  signalProcessExit;
#endif
    InternalPosix() {}
    ~InternalPosix() { SC_TRUST_RESULT(close()); }

    const InternalPosix& getPosix() const { return *this; }

    [[nodiscard]] Result close()
    {
#if SC_ASYNC_USE_EPOLL
        SC_TRY(signalProcessExitDescriptor.close());
#endif
        SC_TRY(wakeupPipe.readPipe.close());
        SC_TRY(wakeupPipe.writePipe.close());
        return loopFd.close();
    }

#if SC_ASYNC_USE_EPOLL
    [[nodiscard]] static Result addEventWatcher(AsyncRequest& async, int fileDescriptor, int32_t filter)
    {
        struct epoll_event event = {0};
        event.events             = filter;
        event.data.ptr           = &async; // data.ptr is a user data pointer
        FileDescriptor::Handle loopFd;
        SC_TRY(async.eventLoop->internal.get().getPosix().loopFd.get(loopFd, Result::Error("loop")));

        int res = ::epoll_ctl(loopFd, EPOLL_CTL_ADD, fileDescriptor, &event);
        if (res == -1)
        {
            return Result::Error("epoll_ctl");
        }
        return Result(true);
    }
#endif

    [[nodiscard]] Result createEventLoop(AsyncEventLoop::Options options = AsyncEventLoop::Options())
    {
        if (options.apiType == AsyncEventLoop::Options::ApiType::ForceUseIOURing)
        {
            return Result::Error("createEventLoop: Cannot use io_uring");
        }
#if SC_ASYNC_USE_EPOLL
        const int newQueue = ::epoll_create1(O_CLOEXEC); // Use epoll_create1 instead of kqueue
#else
        const int     newQueue = ::kqueue();
#endif
        if (newQueue == -1)
        {
            // TODO: Better error handling
            return Result::Error("AsyncEventLoop::InternalPosix::createEventLoop() failed");
        }
        SC_TRY(loopFd.assign(newQueue));
        return Result(true);
    }

    [[nodiscard]] Result createSharedWatchers(AsyncEventLoop& eventLoop)
    {
#if SC_ASYNC_USE_EPOLL
        SC_TRY(createProcessSignalWatcher(eventLoop));
#endif
        SC_TRY(createWakeup(eventLoop));
        SC_TRY(eventLoop.runNoWait());   // Register the read handle before everything else
        eventLoop.decreaseActiveCount(); // WakeUp doesn't keep the queue active. Must be after runNoWait().
#if SC_ASYNC_USE_EPOLL
        eventLoop.decreaseActiveCount(); // Process watcher doesn't keep the queue active. Must be after runNoWait().
#endif
        return Result(true);
    }

    [[nodiscard]] Result createWakeup(AsyncEventLoop& eventLoop)
    {
        // Create
        SC_TRY(wakeupPipe.createPipe(PipeDescriptor::ReadNonInheritable, PipeDescriptor::WriteNonInheritable));
        SC_TRY(wakeupPipe.readPipe.setBlocking(false));
        SC_TRY(wakeupPipe.writePipe.setBlocking(false));

        // Register
        FileDescriptor::Handle wakeUpPipeDescriptor;
        SC_TRY(wakeupPipe.readPipe.get(
            wakeUpPipeDescriptor,
            Result::Error("AsyncEventLoop::InternalPosix::createSharedWatchers() - AsyncRequest read handle invalid")));
        wakeupPoll.callback.bind<&InternalPosix::completeWakeUp>();
        wakeupPoll.setDebugName("SharedWakeUpPoll");
        SC_TRY(wakeupPoll.start(eventLoop, wakeUpPipeDescriptor));
        return Result(true);
    }

    static void completeWakeUp(AsyncFilePoll::Result& result)
    {
        AsyncFilePoll& async = result.async;
        // TODO: Investigate MACHPORT (kqueue) and eventfd (epoll) to avoid the additional read syscall

        char fakeBuffer[10];
        do
        {
            const ssize_t res = ::read(async.fileDescriptor, fakeBuffer, sizeof(fakeBuffer));

            if (res >= 0 and (static_cast<size_t>(res) == sizeof(fakeBuffer)))
                continue;

            if (res != -1)
                break;

            if (errno == EWOULDBLOCK or errno == EAGAIN)
                break;

        } while (errno == EINTR);
        result.async.eventLoop->executeWakeUps(result);
    }

    Result wakeUpFromExternalThread()
    {
        // TODO: We need an atomic bool swap to wait until next run
        const void* fakeBuffer;
        int         asyncFd;
        ssize_t     writtenBytes;
        SC_TRY(wakeupPipe.writePipe.get(asyncFd, Result::Error("writePipe handle")));
        fakeBuffer = "";
        do
        {
            writtenBytes = ::write(asyncFd, fakeBuffer, 1);
        } while (writtenBytes == -1 && errno == EINTR);

        if (writtenBytes != 1)
        {
            return Result::Error("AsyncEventLoop::wakeUpFromExternalThread - Error in write");
        }
        return Result(true);
    }

#if SC_ASYNC_USE_EPOLL
    // TODO: This should be lazily created on demand
    [[nodiscard]] Result createProcessSignalWatcher(AsyncEventLoop& loop)
    {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);

        if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1)
        {
            return Result::Error("Failed to set signal mask");
        }

        const int signalFd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
        if (signalFd == -1)
        {
            return Result::Error("Failed to create signalfd");
        }

        SC_TRY(signalProcessExitDescriptor.assign(signalFd));
        signalProcessExit.callback.bind<InternalPosix, &InternalPosix::onSIGCHLD>(*this);
        return signalProcessExit.start(loop, signalFd);
    }

    void onSIGCHLD(AsyncFilePoll::Result& result)
    {
        struct signalfd_siginfo siginfo;
        FileDescriptor::Handle  sigHandle;

        const InternalPosix& internal = result.async.eventLoop->internal.get().getPosix();
        (void)(internal.signalProcessExitDescriptor.get(sigHandle, Result::Error("Invalid signal handle")));
        ssize_t size = ::read(sigHandle, &siginfo, sizeof(siginfo));

        if (size == sizeof(siginfo))
        {
            // Check if the received signal is related to process exit
            if (siginfo.ssi_signo == SIGCHLD)
            {
                // Loop all process handles to find if one of our interest has exited
                AsyncProcessExit* current = result.async.eventLoop->activeProcessExits.front;

                while (current)
                {
                    if (siginfo.ssi_pid == current->handle)
                    {
                        AsyncProcessExit::Result processResult(*current, Result(true));
                        processResult.exitStatus.status = siginfo.ssi_status;
                        result.async.eventLoop->removeActiveHandle(*current);
                        current->callback(processResult);
                        // TODO: Handle lazy deactivation for signals when no more processes exist
                        result.reactivateRequest(true);
                        break;
                    }
                    current = static_cast<AsyncProcessExit*>(current->next);
                }
            }
        }
    }
#endif

    [[nodiscard]] static Result stopSingleWatcherImmediate(AsyncRequest& async, SocketDescriptor::Handle handle,
                                                           int32_t filter)
    {
        FileDescriptor::Handle loopFd;
        SC_TRY(async.eventLoop->internal.get().getPosix().loopFd.get(
            loopFd, Result::Error("AsyncEventLoop::InternalPosix::syncWithKernel() - Invalid Handle")));
#if SC_ASYNC_USE_EPOLL
        struct epoll_event event;
        event.events   = filter;
        event.data.ptr = &async;
        const int res  = ::epoll_ctl(loopFd, EPOLL_CTL_DEL, handle, &event);
#else
        struct kevent kev;
        EV_SET(&kev, handle, filter, EV_DELETE, 0, 0, nullptr);
        const int res = ::kevent(loopFd, &kev, 1, 0, 0, nullptr);
#endif
        if (res == 0 or (errno == EBADF or errno == ENOENT))
        {
            return Result(true);
        }
        return Result::Error("stopSingleWatcherImmediate failed");
    }

    [[nodiscard]] static Result associateExternallyCreatedTCPSocket(SocketDescriptor&) { return Result(true); }
    [[nodiscard]] static Result associateExternallyCreatedFileDescriptor(FileDescriptor&) { return Result(true); }
};

struct SC::AsyncEventLoop::KernelQueuePosix
{
  private:
    static constexpr int totalNumEvents = 1024;

#if SC_ASYNC_USE_EPOLL
    epoll_event events[totalNumEvents];
#else
    struct kevent events[totalNumEvents];
#endif
    int newEvents = 0;

    KernelQueue& parentKernelQueue;

  public:
    KernelQueuePosix(KernelQueue& kq) : parentKernelQueue(kq) { memset(events, 0, sizeof(events)); }
#if SC_PLATFORM_APPLE
    KernelQueuePosix(InternalPosix&) : parentKernelQueue(*this) { memset(events, 0, sizeof(events)); }
#endif
    uint32_t getNumEvents() const { return static_cast<uint32_t>(newEvents); }

    [[nodiscard]] AsyncRequest* getAsyncRequest(uint32_t idx) const
    {
#if SC_ASYNC_USE_EPOLL
        return static_cast<AsyncRequest*>(events[idx].data.ptr);
#else
        return static_cast<AsyncRequest*>(events[idx].udata);
#endif
    }

#if SC_ASYNC_USE_EPOLL
    // In epoll (differently from kqueue) the watcher is immediately added, so we call this 'add' instead of 'set'
    [[nodiscard]] static Result addEventWatcher(AsyncRequest& async, int fileDescriptor, int32_t filter)
    {
        return InternalPosix::addEventWatcher(async, fileDescriptor, filter);
    }
#else
    [[nodiscard]] Result setEventWatcher(AsyncRequest& async, int fileDescriptor, short filter,
                                         unsigned int options = 0)
    {
        EV_SET(events + newEvents, fileDescriptor, filter, EV_ADD | EV_ENABLE, options, 0, &async);
        newEvents += 1;
        if (newEvents >= totalNumEvents)
        {
            SC_TRY(flushQueue(*async.eventLoop));
        }
        return Result(true);
    }

    [[nodiscard]] Result flushQueue(AsyncEventLoop& eventLoop)
    {
        FileDescriptor::Handle loopFd;
        SC_TRY(eventLoop.internal.get().loopFd.get(loopFd, Result::Error("flushQueue() - Invalid Handle")));

        int res;
        do
        {
            res = ::kevent(loopFd, events, newEvents, nullptr, 0, nullptr);
        } while (res == -1 && errno == EINTR);
        if (res != 0)
        {
            return Result::Error("AsyncEventLoop::InternalPosix::flushQueue() - kevent failed");
        }
        newEvents = 0;
        return Result(true);
    }

#endif

    // SYNC WITH KERNEL
    static struct timespec timerToTimespec(const Time::HighResolutionCounter& loopTime,
                                           const Time::HighResolutionCounter* nextTimer)
    {
        struct timespec specTimeout;
        if (nextTimer)
        {
            if (nextTimer->isLaterThanOrEqualTo(loopTime))
            {
                const Time::HighResolutionCounter diff = nextTimer->subtractExact(loopTime);

                specTimeout.tv_sec  = diff.part1;
                specTimeout.tv_nsec = diff.part2;
                return specTimeout;
            }
        }
        specTimeout.tv_sec  = 0;
        specTimeout.tv_nsec = 0;
        return specTimeout;
    }

    [[nodiscard]] Result syncWithKernel(AsyncEventLoop& eventLoop, SyncMode syncMode)
    {
        const Time::HighResolutionCounter* nextTimer =
            syncMode == SyncMode::ForcedForwardProgress ? eventLoop.findEarliestTimer() : nullptr;
        FileDescriptor::Handle loopFd;
        SC_TRY(
            eventLoop.internal.get().getPosix().loopFd.get(loopFd, Result::Error("syncWithKernel() - Invalid Handle")));

        struct timespec specTimeout;
        // when nextTimer is null, specTimeout is initialized to 0, so that SyncMode::NoWait
        specTimeout = timerToTimespec(eventLoop.loopTime, nextTimer);
        int res;
        do
        {
            auto spec = nextTimer or syncMode == SyncMode::NoWait ? &specTimeout : nullptr;
#if SC_ASYNC_USE_EPOLL
            res = ::epoll_pwait2(loopFd, events, totalNumEvents, spec, 0);
#else
            res = ::kevent(loopFd, events, newEvents, events, totalNumEvents, spec);
#endif
            if (res == -1 && errno == EINTR)
            {
                // Interrupted, we must recompute timeout
                if (nextTimer)
                {
                    eventLoop.updateTime();
                    specTimeout = timerToTimespec(eventLoop.loopTime, nextTimer);
                }
                continue;
            }
            break;
        } while (true);
        if (res == -1)
        {
            return Result::Error("AsyncEventLoop::InternalPosix::poll() - failed");
        }
        newEvents = static_cast<int>(res);
        if (nextTimer)
        {
            eventLoop.executeTimers(parentKernelQueue, *nextTimer);
        }
        return Result(true);
    }

#if SC_ASYNC_USE_EPOLL
    [[nodiscard]] Result validateEvent(uint32_t idx, bool& continueProcessing)
    {
        const epoll_event& event = events[idx];
        continueProcessing       = true;

        if ((event.events & EPOLLERR) != 0 || (event.events & EPOLLHUP) != 0)
        {
            continueProcessing = false;
            return Result::Error("Error in processing event (epoll EPOLLERR or EPOLLHUP)");
        }
        return Result(true);
    }
#else
    [[nodiscard]] Result validateEvent(uint32_t idx, bool& continueProcessing)
    {
        const struct kevent& event = events[idx];
        continueProcessing = (event.flags & EV_DELETE) == 0;
        if ((event.flags & EV_ERROR) != 0)
        {
            return Result::Error("Error in processing event (kqueue EV_ERROR)");
        }
        return Result(true);
    }
#endif

    // TIMEOUT
    // Nothing to do :)

    // WAKEUP
    // Nothing to do :)

    // Socket ACCEPT
    [[nodiscard]] Result setupAsync(AsyncSocketAccept& async)
    {
#if SC_ASYNC_USE_EPOLL
        return addEventWatcher(async, async.handle, EPOLLIN);
#else
        return setEventWatcher(async, async.handle, EVFILT_READ);
#endif
    }

    [[nodiscard]] static Result teardownAsync(AsyncSocketAccept& async)
    {
#if SC_ASYNC_USE_EPOLL
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EPOLLIN);
#else
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EVFILT_READ);
#endif
    }

    [[nodiscard]] static Result completeAsync(AsyncSocketAccept::Result& result)
    {
        AsyncSocketAccept& async = result.async;
        SocketDescriptor   serverSocket;
        SC_TRY(serverSocket.assign(async.handle));
        auto detach = MakeDeferred([&] { serverSocket.detach(); });
        result.acceptedClient.detach();
        return SocketServer(serverSocket).accept(async.addressFamily, result.acceptedClient);
    }

    // Socket CONNECT
    [[nodiscard]] Result setupAsync(AsyncSocketConnect& async)
    {
#if SC_ASYNC_USE_EPOLL
        return addEventWatcher(async, async.handle, EPOLLOUT);
#else
        return setEventWatcher(async, async.handle, EVFILT_WRITE);
#endif
    }

    static Result teardownAsync(AsyncSocketConnect& async)
    {
#if SC_ASYNC_USE_EPOLL
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EPOLLOUT);
#else
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EVFILT_WRITE);
#endif
    }

    [[nodiscard]] static Result activateAsync(AsyncSocketConnect& async)
    {
        SocketDescriptor client;
        SC_TRY(client.assign(async.handle));
        auto detach = MakeDeferred([&] { client.detach(); });
        auto res    = SocketClient(client).connect(async.ipAddress);
        // we expect connect to fail with
        if (res)
        {
            return Result::Error("connect failed (succeeded?)");
        }
        if (errno != EAGAIN and errno != EINPROGRESS)
        {
            return Result::Error("connect failed (socket is in blocking mode)");
        }
        return Result(true);
    }

    [[nodiscard]] static Result completeAsync(AsyncSocketConnect::Result& result)
    {
        AsyncSocketConnect& async = result.async;

        int       errorCode;
        socklen_t errorSize = sizeof(errorCode);
        const int socketRes = ::getsockopt(async.handle, SOL_SOCKET, SO_ERROR, &errorCode, &errorSize);

        // TODO: This is making a syscall for each connected socket, we should probably aggregate them
        // And additionally it's stupid as probably WRITE will be subscribed again anyway
        // But probably this means to review the entire process of async stop
#if SC_ASYNC_USE_EPOLL
        SC_TRUST_RESULT(InternalPosix::stopSingleWatcherImmediate(result.async, async.handle, EPOLLOUT));
#else
        SC_TRUST_RESULT(InternalPosix::stopSingleWatcherImmediate(result.async, async.handle, EVFILT_WRITE));
#endif
        if (socketRes == 0)
        {
            SC_TRY_MSG(errorCode == 0, "connect SO_ERROR");
            return Result(true);
        }
        return Result::Error("connect getsockopt failed");
    }

    // Socket SEND
    [[nodiscard]] Result setupAsync(AsyncSocketSend& async)
    {
#if SC_ASYNC_USE_EPOLL
        return Result(addEventWatcher(async, async.handle, EPOLLOUT));
#else
        return Result(setEventWatcher(async, async.handle, EVFILT_WRITE));
#endif
    }

    [[nodiscard]] static Result teardownAsync(AsyncSocketSend& async)
    {
#if SC_ASYNC_USE_EPOLL
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EPOLLOUT);
#else
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EVFILT_WRITE);
#endif
    }

    [[nodiscard]] static Result completeAsync(AsyncSocketSend::Result& result)
    {
        AsyncSocketSend& async = result.async;
        const ssize_t    res   = ::send(async.handle, async.data.data(), async.data.sizeInBytes(), 0);
        SC_TRY_MSG(res >= 0, "error in send");
        SC_TRY_MSG(size_t(res) == async.data.sizeInBytes(), "send didn't send all data");
        return Result(true);
    }

    // Socket RECEIVE
    [[nodiscard]] Result setupAsync(AsyncSocketReceive& async)
    {
#if SC_ASYNC_USE_EPOLL
        return Result(addEventWatcher(async, async.handle, EPOLLIN | EPOLLRDHUP));
#else
        return Result(setEventWatcher(async, async.handle, EVFILT_READ));
#endif
    }

    [[nodiscard]] static Result teardownAsync(AsyncSocketReceive& async)
    {
#if SC_ASYNC_USE_EPOLL
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EPOLLIN | EPOLLRDHUP);
#else
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EVFILT_READ);
#endif
    }

    [[nodiscard]] static Result completeAsync(AsyncSocketReceive::Result& result)
    {
        AsyncSocketReceive& async = result.async;
        const ssize_t       res   = ::recv(async.handle, async.data.data(), async.data.sizeInBytes(), 0);
        SC_TRY_MSG(res >= 0, "error in recv");
        return Result(async.data.sliceStartLength(0, static_cast<size_t>(res), result.readData));
    }

    // Socket CLOSE
    [[nodiscard]] Result setupAsync(AsyncSocketClose& async)
    {
        async.eventLoop->scheduleManualCompletion(async);
        async.code = ::close(async.handle);
        SC_TRY_MSG(async.code == 0, "Close returned error");
        return Result(true);
    }

    // File READ
#if SC_ASYNC_USE_EPOLL

    [[nodiscard]] Result setupAsync(AsyncFileRead& async)
    {
        async.flags &= ~AsyncRequest::Flag_RegularFile;
        bool isFile;
        SC_TRY(isRegularFile(async.fileDescriptor, isFile));
        if (isFile)
        {
            async.flags |= AsyncRequest::Flag_RegularFile;
            // TODO: epoll doesn't support regular file descriptors (needs a thread pool).
            return Result(true);
        }
        // TODO: Check if we need EPOLLET for edge-triggered mode
        return addEventWatcher(async, async.fileDescriptor, EPOLLIN);
    }

    [[nodiscard]] static Result activateAsync(AsyncFileRead& async)
    {
        if (async.flags & AsyncRequest::Flag_RegularFile)
        {
            // TODO: This is a synchronous operation! Run this code in a threadpool.
            Result                res(true);
            AsyncFileRead::Result result(async, move(res));

            async.eventLoop->scheduleManualCompletion(async);

            if (executeFileRead(result))
            {
                result.async.syncReadBytes = result.readData.sizeInBytes();
                return Result(true);
            }
            return Result(false);
        }
        return Result(true);
    }

    [[nodiscard]] static Result completeAsync(AsyncFileRead::Result& result)
    {
        if (result.async.flags & AsyncRequest::Flag_RegularFile)
        {
            return Result(result.async.readBuffer.sliceStartLength(0, result.async.syncReadBytes, result.readData));
        }
        else
        {
            return executeFileRead(result);
        }
    }

    [[nodiscard]] static bool isRegularFile(int fd, bool& isFile)
    {
        struct stat file_stat;
        if (::fstat(fd, &file_stat) == -1)
        {
            return false;
        }
        isFile = S_ISREG(file_stat.st_mode);
        return true;
    }
#else

    [[nodiscard]] Result setupAsync(AsyncFileRead& async)
    {
        return setEventWatcher(async, async.fileDescriptor, EVFILT_READ);
    }

    [[nodiscard]] static Result completeAsync(AsyncFileRead::Result& result) { return executeFileRead(result); }

    [[nodiscard]] static Result cancelAsync(AsyncFileRead& async)
    {
        return InternalPosix::stopSingleWatcherImmediate(async, async.fileDescriptor, EVFILT_READ);
    }

#endif

    [[nodiscard]] static Result executeFileRead(AsyncFileRead::Result& result)
    {
        auto    span = result.async.readBuffer;
        ssize_t res;
        do
        {
            if (result.async.offset == 0)
            {
                res = ::read(result.async.fileDescriptor, span.data(), span.sizeInBytes());
            }
            else
            {
                res = ::pread(result.async.fileDescriptor, span.data(), span.sizeInBytes(),
                              static_cast<off_t>(result.async.offset));
            }
        } while ((res == -1) and (errno == EINTR));

        SC_TRY_MSG(res >= 0, "::read failed");
        return Result(result.async.readBuffer.sliceStartLength(0, static_cast<size_t>(res), result.readData));
    }

    // File WRITE
#if SC_ASYNC_USE_EPOLL

    [[nodiscard]] Result setupAsync(AsyncFileWrite& async)
    {
        async.flags &= ~AsyncRequest::Flag_RegularFile;
        bool isFile;
        SC_TRY(isRegularFile(async.fileDescriptor, isFile));
        if (isFile)
        {
            async.flags |= AsyncRequest::Flag_RegularFile;
            // TODO: epoll doesn't support regular file descriptors (needs a thread pool).
            return Result(true);
        }
        // TODO: Check if we need EPOLLET for edge-triggered mode
        return addEventWatcher(async, async.fileDescriptor, EPOLLOUT);
    }

    [[nodiscard]] static Result activateAsync(AsyncFileWrite& async)
    {
        // TODO: This is a synchronous operation! Run this code in a threadpool.
        Result                 res(true);
        AsyncFileWrite::Result result(async, move(res));
        if (async.flags & AsyncRequest::Flag_RegularFile)
        {
            async.eventLoop->scheduleManualCompletion(async);
        }
        Result fileWriteRes = executeFileWrite(result);
        if (res)
        {
            async.syncWrittenBytes = result.writtenBytes;
            return Result(true);
        }
        return fileWriteRes;
    }

    [[nodiscard]] static Result completeAsync(AsyncFileWrite::Result& result)
    {
        result.writtenBytes = result.async.syncWrittenBytes;
        return Result(true);
    }

#else

    [[nodiscard]] Result setupAsync(AsyncFileWrite& async)
    {
        return setEventWatcher(async, async.fileDescriptor, EVFILT_WRITE);
    }

    [[nodiscard]] static Result completeAsync(AsyncFileWrite::Result& result) { return executeFileWrite(result); }

    [[nodiscard]] static Result cancelAsync(AsyncFileWrite& async)
    {
        return InternalPosix::stopSingleWatcherImmediate(async, async.fileDescriptor, EVFILT_WRITE);
    }

#endif

    [[nodiscard]] static Result executeFileWrite(AsyncFileWrite::Result& result)
    {
        AsyncFileWrite& async = result.async;

        auto    span = async.writeBuffer;
        ssize_t res;
        do
        {
            if (async.offset == 0)
            {
                res = ::write(async.fileDescriptor, span.data(), span.sizeInBytes());
            }
            else
            {
                res = ::pwrite(async.fileDescriptor, span.data(), span.sizeInBytes(), static_cast<off_t>(async.offset));
            }
        } while ((res == -1) and (errno == EINTR));
        SC_TRY_MSG(res >= 0, "::write failed");
        result.writtenBytes = static_cast<size_t>(res);
        return Result(true);
    }

    // File POLL
    [[nodiscard]] Result setupAsync(AsyncFilePoll& async)
    {
#if SC_ASYNC_USE_EPOLL
        // TODO: Check if we need EPOLLET for edge-triggered mode
        return addEventWatcher(async, async.fileDescriptor, EPOLLIN);
#else
        return setEventWatcher(async, async.fileDescriptor, EVFILT_READ);
#endif
    }

    [[nodiscard]] static Result teardownAsync(AsyncFilePoll& async)
    {
#if SC_ASYNC_USE_EPOLL
        return InternalPosix::stopSingleWatcherImmediate(async, async.fileDescriptor, EPOLLIN);
#else
        return InternalPosix::stopSingleWatcherImmediate(async, async.fileDescriptor, EVFILT_READ);
#endif
    }

    // File Close
    [[nodiscard]] Result setupAsync(AsyncFileClose& async)
    {
        async.eventLoop->scheduleManualCompletion(async);
        async.code = ::close(async.fileDescriptor);
        SC_TRY_MSG(async.code == 0, "Close returned error");
        return Result(true);
    }

    // Process EXIT
#if SC_ASYNC_USE_EPOLL

#else

    [[nodiscard]] Result setupAsync(AsyncProcessExit& async)
    {
        return setEventWatcher(async, async.handle, EVFILT_PROC, NOTE_EXIT | NOTE_EXITSTATUS);
    }

    [[nodiscard]] static Result teardownAsync(AsyncProcessExit& async)
    {
        return InternalPosix::stopSingleWatcherImmediate(async, async.handle, EVFILT_PROC);
    }

    [[nodiscard]] Result completeAsync(AsyncProcessExit::Result& result)
    {
        SC_TRY_MSG(result.async.eventIndex >= 0, "Invalid event Index");
        const struct kevent event = events[result.async.eventIndex];
        if ((event.fflags & (NOTE_EXIT | NOTE_EXITSTATUS)) > 0)
        {
            const uint32_t data = static_cast<uint32_t>(event.data);
            if (WIFEXITED(data) != 0)
            {
                result.exitStatus.status = WEXITSTATUS(data);
            }
            return Result(true);
        }
        return Result(false);
    }
#endif

    // Templates

    // clang-format off
    template <typename T> [[nodiscard]] Result setupAsync(T&)     { return Result(true); }
    template <typename T> [[nodiscard]] Result teardownAsync(T&)  { return Result(true); }
    template <typename T> [[nodiscard]] Result activateAsync(T&)  { return Result(true); }
    template <typename T> [[nodiscard]] Result completeAsync(T&)  { return Result(true); }
    template <typename T> [[nodiscard]] Result cancelAsync(T&)    { return Result(true); }
    // clang-format on
};
