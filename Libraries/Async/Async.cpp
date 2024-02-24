// Copyright (c) Stefano Cristiano
// SPDX-License-Identifier: MIT
#include "Async.h"
#include "../Foundation/Result.h"
#include "../Threading/Threading.h" // EventObject

#if SC_PLATFORM_WINDOWS
#include "Internal/AsyncWindows.inl"
#elif SC_PLATFORM_APPLE
#include "Internal/AsyncPosix.inl"
#elif SC_PLATFORM_LINUX
#include "Internal/AsyncLinux.inl" // uses io_uring
#elif SC_PLATFORM_EMSCRIPTEN
#include "Internal/AsyncEmscripten.inl"
#endif

#define SC_ASYNC_ENABLE_LOG 0
#if SC_ASYNC_ENABLE_LOG
#include "../Strings/Console.h"
#else
#if defined(SC_LOG_MESSAGE)
#undef SC_LOG_MESSAGE
#endif
#define SC_LOG_MESSAGE(a, ...)
#endif

template <>
void SC::AsyncEventLoop::InternalOpaque::construct(Handle& buffer)
{
    placementNew(buffer.reinterpret_as<Object>());
}
template <>
void SC::AsyncEventLoop::InternalOpaque::destruct(Object& obj)
{
    obj.~Object();
}
#if SC_ASYNC_ENABLE_LOG
const char* SC::AsyncRequest::TypeToString(Type type)
{
    switch (type)
    {
    case Type::LoopTimeout: return "LoopTimeout";
    case Type::LoopWakeUp: return "LoopWakeUp";
    case Type::ProcessExit: return "ProcessExit";
    case Type::SocketAccept: return "SocketAccept";
    case Type::SocketConnect: return "SocketConnect";
    case Type::SocketSend: return "SocketSend";
    case Type::SocketReceive: return "SocketReceive";
    case Type::SocketClose: return "SocketClose";
    case Type::FileRead: return "FileRead";
    case Type::FileWrite: return "FileWrite";
    case Type::FileClose: return "FileClose";
    case Type::FilePoll: return "FilePoll";
    }
    Assert::unreachable();
}
#endif

SC::Result SC::AsyncRequest::validateAsync()
{
    const bool asyncStateIsFree      = state == AsyncRequest::State::Free;
    const bool asyncIsNotOwnedByLoop = eventLoop == nullptr;
    SC_LOG_MESSAGE("{} {} QUEUE\n", debugName, AsyncRequest::TypeToString(type));
    SC_TRY_MSG(asyncStateIsFree, "Trying to stage AsyncRequest that is in use");
    SC_TRY_MSG(asyncIsNotOwnedByLoop, "Trying to add AsyncRequest belonging to another Loop");
    return Result(true);
}

SC::Result SC::AsyncRequest::queueSubmission(AsyncEventLoop& loop) { return loop.queueSubmission(*this); }

void SC::AsyncRequest::updateTime(AsyncEventLoop& loop) { loop.updateTime(); }

SC::Result SC::AsyncRequest::stop()
{
    if (eventLoop)
        return eventLoop->cancelAsync(*this);
    return SC::Result::Error("stop failed. eventLoop is nullptr");
}

SC::Result SC::AsyncLoopTimeout::start(AsyncEventLoop& loop, Time::Milliseconds expiration)
{
    SC_TRY(validateAsync());
    updateTime(loop);
    expirationTime = loop.getLoopTime().offsetBy(expiration);
    timeout        = expiration;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncLoopWakeUp::start(AsyncEventLoop& loop, EventObject* eo)
{
    eventObject = eo;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncProcessExit::start(AsyncEventLoop& loop, ProcessDescriptor::Handle process)
{
    handle = process;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncSocketAccept::start(AsyncEventLoop& loop, const SocketDescriptor& socketDescriptor)
{
    SC_TRY(validateAsync());
    SC_TRY(socketDescriptor.get(handle, SC::Result::Error("Invalid handle")));
    SC_TRY(socketDescriptor.getAddressFamily(addressFamily));
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncSocketConnect::start(AsyncEventLoop& loop, const SocketDescriptor& socketDescriptor,
                                         SocketIPAddress socketIpAddress)
{
    SC_TRY(validateAsync());
    SC_TRY(socketDescriptor.get(handle, SC::Result::Error("Invalid handle")));
    ipAddress = socketIpAddress;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncSocketSend::start(AsyncEventLoop& loop, const SocketDescriptor& socketDescriptor,
                                      Span<const char> dataToSend)
{

    SC_TRY(validateAsync());
    SC_TRY(socketDescriptor.get(handle, SC::Result::Error("Invalid handle")));
    data = dataToSend;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncSocketReceive::start(AsyncEventLoop& loop, const SocketDescriptor& socketDescriptor,
                                         Span<char> receiveData)
{
    SC_TRY(validateAsync());
    SC_TRY(socketDescriptor.get(handle, SC::Result::Error("Invalid handle")));
    data = receiveData;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncSocketClose::start(AsyncEventLoop& loop, const SocketDescriptor& socketDescriptor)
{
    SC_TRY(validateAsync());
    SC_TRY(socketDescriptor.get(handle, SC::Result::Error("Invalid handle")));
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncFileRead::start(AsyncEventLoop& loop, FileDescriptor::Handle fd, Span<char> rb)
{
    SC_TRY_MSG(rb.sizeInBytes() > 0, "AsyncEventLoop::startFileRead - Zero sized read buffer");
    SC_TRY(validateAsync());
    fileDescriptor = fd;
    readBuffer     = rb;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncFileWrite::start(AsyncEventLoop& loop, FileDescriptor::Handle fd, Span<const char> wb)
{
    SC_TRY_MSG(wb.sizeInBytes() > 0, "AsyncEventLoop::startFileWrite - Zero sized write buffer");
    SC_TRY(validateAsync());
    fileDescriptor = fd;
    writeBuffer    = wb;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncFileClose::start(AsyncEventLoop& loop, FileDescriptor::Handle fd)
{
    SC_TRY(validateAsync());
    fileDescriptor = fd;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncFilePoll::start(AsyncEventLoop& loop, FileDescriptor::Handle fd)
{
    SC_TRY(validateAsync());
    fileDescriptor = fd;
    SC_TRY(queueSubmission(loop));
    return SC::Result(true);
}

SC::Result SC::AsyncEventLoop::queueSubmission(AsyncRequest& async)
{
    async.eventLoop = this;
    async.state     = AsyncRequest::State::Setup;
    submissions.queueBack(async);
    return Result(true);
}

SC::Result SC::AsyncEventLoop::run()
{
    while (getTotalNumberOfActiveHandle() > 0 or not submissions.isEmpty() or not manualCompletions.isEmpty())
    {
        SC_TRY(runOnce());
    };
    return SC::Result(true);
}

const SC::Time::HighResolutionCounter* SC::AsyncEventLoop::findEarliestTimer() const
{
    const Time::HighResolutionCounter* earliestTime = nullptr;
    for (AsyncRequest* async = activeLoopTimeouts.front; async != nullptr; async = async->next)
    {
        SC_ASSERT_DEBUG(async->type == AsyncRequest::Type::LoopTimeout);
        const auto& expirationTime = static_cast<AsyncLoopTimeout*>(async)->expirationTime;
        if (earliestTime == nullptr or earliestTime->isLaterThanOrEqualTo(expirationTime))
        {
            earliestTime = &expirationTime;
        }
    };
    return earliestTime;
}

void SC::AsyncEventLoop::invokeExpiredTimers()
{
    for (AsyncLoopTimeout* async = activeLoopTimeouts.front; async != nullptr;)
    {
        SC_ASSERT_DEBUG(async->type == AsyncRequest::Type::LoopTimeout);
        const Time::HighResolutionCounter& expirationTime = async->expirationTime;
        AsyncLoopTimeout*                  currentAsync   = async;
        async                                             = static_cast<AsyncLoopTimeout*>(async->next);
        if (loopTime.isLaterThanOrEqualTo(expirationTime))
        {
            activeLoopTimeouts.remove(*currentAsync);
            currentAsync->state = AsyncRequest::State::Free;
            AsyncLoopTimeout::Result result(*currentAsync, Result(true));
            result.async.eventLoop = nullptr; // Allow reusing it
            currentAsync->callback(result);
        }
    }
}

SC::Result SC::AsyncEventLoop::create(Options options)
{
    Internal& self = internal.get();
    SC_TRY(self.createEventLoop(options));
    SC_TRY(self.createSharedWatchers(*this));
    return SC::Result(true);
}

template <typename T>
void SC::AsyncEventLoop::freeAsyncRequests(IntrusiveDoubleLinkedList<T>& linkedList)
{
    for (auto async = linkedList.front; async != nullptr; async = static_cast<T*>(async->next))
    {
        async->state     = AsyncRequest::State::Free;
        async->eventLoop = nullptr;
    }
    linkedList.clear();
}

SC::Result SC::AsyncEventLoop::close()
{
    freeAsyncRequests(submissions);

    freeAsyncRequests(activeLoopTimeouts);
    freeAsyncRequests(activeLoopWakeUps);
    freeAsyncRequests(activeProcessExits);
    freeAsyncRequests(activeSocketAccepts);
    freeAsyncRequests(activeSocketConnects);
    freeAsyncRequests(activeSocketSends);
    freeAsyncRequests(activeSocketReceives);
    freeAsyncRequests(activeSocketCloses);
    freeAsyncRequests(activeFileReads);
    freeAsyncRequests(activeFileWrites);
    freeAsyncRequests(activeFileCloses);
    freeAsyncRequests(activeFilePolls);

    freeAsyncRequests(manualCompletions);
    numberOfActiveHandles = 0;
    numberOfExternals     = 0;
    return internal.get().close();
}

SC::Result SC::AsyncEventLoop::stageSubmission(KernelQueue& queue, AsyncRequest& async)
{
    switch (async.state)
    {
    case AsyncRequest::State::Setup: {
        SC_TRY(setupAsync(queue, async));
        async.state = AsyncRequest::State::Submitting;
        SC_TRY(activateAsync(queue, async));
    }
    break;
    case AsyncRequest::State::Submitting: {
        SC_TRY(activateAsync(queue, async));
    }
    break;
    case AsyncRequest::State::Free: {
        // TODO: Stop the completion, it has been cancelled before being submitted
        SC_ASSERT_RELEASE(false);
    }
    break;
    case AsyncRequest::State::Cancelling: {
        SC_TRY(cancelAsync(queue, async));
        SC_TRY(teardownAsync(queue, async));
    }
    break;
    case AsyncRequest::State::Teardown: {
        SC_TRY(teardownAsync(queue, async));
    }
    break;
    case AsyncRequest::State::Active: {
        SC_ASSERT_DEBUG(false);
        return SC::Result::Error("AsyncEventLoop::processSubmissions() got Active handle");
    }
    break;
    }
    return SC::Result(true);
}

void SC::AsyncEventLoop::increaseActiveCount() { numberOfExternals += 1; }

void SC::AsyncEventLoop::decreaseActiveCount() { numberOfExternals -= 1; }

int SC::AsyncEventLoop::getTotalNumberOfActiveHandle() const { return numberOfActiveHandles + numberOfExternals; }

SC::Result SC::AsyncEventLoop::runOnce() { return runStep(SyncMode::ForcedForwardProgress); }

SC::Result SC::AsyncEventLoop::runNoWait() { return runStep(SyncMode::NoWait); }

SC::Result SC::AsyncEventLoop::completeAndEventuallyReactivate(KernelQueue& queue, AsyncRequest& async,
                                                               Result&& returnCode)
{
    SC_ASSERT_RELEASE(async.state == AsyncRequest::State::Active);
    bool reactivate = false;
    SC_TRY(completeAsync(queue, async, move(returnCode), reactivate));
    if (reactivate)
    {
        removeActiveHandle(async);
        async.state = AsyncRequest::State::Submitting;
        submissions.queueBack(async);
    }
    else
    {
        SC_TRY(teardownAsync(queue, async));
        removeActiveHandle(async);
    }
    if (not returnCode)
    {
        reportError(queue, async, move(returnCode));
    }
    return Result(true);
}

SC::Result SC::AsyncEventLoop::runStep(SyncMode syncMode)
{
    KernelQueue queue(internal.get());
    SC_LOG_MESSAGE("---------------\n");

    while (AsyncRequest* async = submissions.dequeueFront())
    {
        auto res = stageSubmission(queue, *async);
        if (not res)
        {
            reportError(queue, *async, move(res));
        }
    }

    if (getTotalNumberOfActiveHandle() <= 0 and manualCompletions.isEmpty())
    {
        // happens when we do cancelAsync on the last active async for example
        return SC::Result(true);
    }

    if (getTotalNumberOfActiveHandle() > 0)
    {
        // We may have some manualCompletions queued (for SocketClose for example) but no active handles
        SC_LOG_MESSAGE("Active Requests Before Poll = {}\n", getTotalNumberOfActiveHandle());
        SC_TRY(queue.syncWithKernel(*this, syncMode));
        SC_LOG_MESSAGE("Active Requests After Poll = {}\n", getTotalNumberOfActiveHandle());
    }

    runStepExecuteCompletions(queue);
    runStepExecuteManualCompletions(queue);

    SC_LOG_MESSAGE("Active Requests After Completion = {}\n", getTotalNumberOfActiveHandle());
    return SC::Result(true);
}

void SC::AsyncEventLoop::runStepExecuteManualCompletions(KernelQueue& queue)
{
    IntrusiveDoubleLinkedList<AsyncRequest> manualCompletionsToReactivate;
    while (AsyncRequest* async = manualCompletions.dequeueFront())
    {
        if (not completeAndEventuallyReactivate(queue, *async, Result(true)))
        {
            SC_LOG_MESSAGE("Error completing {}", async->debugName);
            continue;
        }
        if (async->state == AsyncRequest::State::Active)
        {
            manualCompletionsToReactivate.queueBack(*async);
        }
    }
    manualCompletions = manualCompletionsToReactivate;
}

void SC::AsyncEventLoop::runStepExecuteCompletions(KernelQueue& queue)
{
    for (uint32_t idx = 0; idx < queue.getNumEvents(); ++idx)
    {
        SC_LOG_MESSAGE(" Iteration = {}\n", idx);
        SC_LOG_MESSAGE(" Active Requests = {}\n", getTotalNumberOfActiveHandle());
        bool continueProcessing = true;

        AsyncRequest* request = queue.getAsyncRequest(idx);
        if (request == nullptr)
        {
            continue;
        }

        AsyncRequest& async  = *request;
        Result        result = Result(queue.validateEvent(idx, continueProcessing));
        if (not result)
        {
            reportError(queue, async, move(result));
            continue;
        }

        if (not continueProcessing)
        {
            continue;
        }
        async.eventIndex = static_cast<int32_t>(idx);
        if (async.state == AsyncRequest::State::Active)
        {
            if (not completeAndEventuallyReactivate(queue, async, move(result)))
            {
                SC_LOG_MESSAGE("Error completing {}", async.debugName);
            }
        }
        else
        {
            SC_ASSERT_RELEASE(async.state != AsyncRequest::State::Free);
            async.state     = AsyncRequest::State::Free;
            async.eventLoop = nullptr;
        }
    }
}

template <typename Lambda>
SC::Result SC::AsyncRequest::applyOnAsync(AsyncRequest& async, Lambda&& lambda)
{
    switch (async.type)
    {
    case AsyncRequest::Type::LoopTimeout: SC_TRY(lambda(*static_cast<AsyncLoopTimeout*>(&async))); break;
    case AsyncRequest::Type::LoopWakeUp: SC_TRY(lambda(*static_cast<AsyncLoopWakeUp*>(&async))); break;
    case AsyncRequest::Type::ProcessExit: SC_TRY(lambda(*static_cast<AsyncProcessExit*>(&async))); break;
    case AsyncRequest::Type::SocketAccept: SC_TRY(lambda(*static_cast<AsyncSocketAccept*>(&async))); break;
    case AsyncRequest::Type::SocketConnect: SC_TRY(lambda(*static_cast<AsyncSocketConnect*>(&async))); break;
    case AsyncRequest::Type::SocketSend: SC_TRY(lambda(*static_cast<AsyncSocketSend*>(&async))); break;
    case AsyncRequest::Type::SocketReceive: SC_TRY(lambda(*static_cast<AsyncSocketReceive*>(&async))); break;
    case AsyncRequest::Type::SocketClose: SC_TRY(lambda(*static_cast<AsyncSocketClose*>(&async))); break;
    case AsyncRequest::Type::FileRead: SC_TRY(lambda(*static_cast<AsyncFileRead*>(&async))); break;
    case AsyncRequest::Type::FileWrite: SC_TRY(lambda(*static_cast<AsyncFileWrite*>(&async))); break;
    case AsyncRequest::Type::FileClose: SC_TRY(lambda(*static_cast<AsyncFileClose*>(&async))); break;
    case AsyncRequest::Type::FilePoll: SC_TRY(lambda(*static_cast<AsyncFilePoll*>(&async))); break;
    }
    return SC::Result(true);
}

SC::Result SC::AsyncEventLoop::setupAsync(KernelQueue& queue, AsyncRequest& async)
{
    SC_LOG_MESSAGE("{} {} SETUP\n", async.debugName, AsyncRequest::TypeToString(async.type));
    return AsyncRequest::applyOnAsync(async, [&](auto& p) { return queue.setupAsync(p); });
}

SC::Result SC::AsyncEventLoop::activateAsync(KernelQueue& queue, AsyncRequest& async)
{
    SC_LOG_MESSAGE("{} {} ACTIVATE\n", async.debugName, AsyncRequest::TypeToString(async.type));
    SC_ASSERT_RELEASE(async.state == AsyncRequest::State::Submitting);
    SC_TRY(AsyncRequest::applyOnAsync(async, [&queue](auto& p) { return queue.activateAsync(p); }));
    async.eventLoop->addActiveHandle(async);
    return Result(true);
}

SC::Result SC::AsyncEventLoop::teardownAsync(KernelQueue& queue, AsyncRequest& async)
{
    SC_LOG_MESSAGE("{} {} TEARDOWN\n", async.debugName, AsyncRequest::TypeToString(async.type));
    SC_TRY(AsyncRequest::applyOnAsync(async, [&queue](auto& p) { return queue.teardownAsync(p); }));
    return Result(true);
}

void SC::AsyncEventLoop::reportError(KernelQueue& queue, AsyncRequest& async, Result&& returnCode)
{
    SC_LOG_MESSAGE("{} ERROR {}\n", async.debugName, AsyncRequest::TypeToString(async.type));
    bool reactivate = false;
    if (async.state == AsyncRequest::State::Active)
    {
        removeActiveHandle(async);
    }
    (void)completeAsync(queue, async, forward<Result>(returnCode), reactivate);
    async.state = AsyncRequest::State::Free;
}

SC::Result SC::AsyncEventLoop::completeAsync(KernelQueue& queue, AsyncRequest& async, Result&& returnCode,
                                             bool& reactivate)
{
    if (returnCode)
    {
        SC_LOG_MESSAGE("{} {} COMPLETE\n", async.debugName, AsyncRequest::TypeToString(async.type));
    }
    else
    {
        SC_LOG_MESSAGE("{} {} COMPLETE (Error = \"{}\")\n", async.debugName, AsyncRequest::TypeToString(async.type),
                       returnCode.message);
    }
    return AsyncRequest::applyOnAsync(async,
                                      [&](auto& p)
                                      {
                                          using Type = typename TypeTraits::RemoveReference<decltype(p)>::type;
                                          typename Type::Result result(p, forward<Result>(returnCode));
                                          if (result.returnCode)
                                              result.returnCode = Result(queue.completeAsync(result));
                                          if (result.async.callback.isValid())
                                              result.async.callback(result);
                                          reactivate = result.shouldBeReactivated;
                                          return SC::Result(true);
                                      });
}

SC::Result SC::AsyncEventLoop::cancelAsync(KernelQueue& queue, AsyncRequest& async)
{
    SC_LOG_MESSAGE("{} {} CANCEL\n", async.debugName, AsyncRequest::TypeToString(async.type));
    SC_TRY(AsyncRequest::applyOnAsync(async, [&](auto& p) { return queue.cancelAsync(p); }))
    if (async.state == AsyncRequest::State::Active)
    {
        removeActiveHandle(async);
    }
    return SC::Result(true);
}

SC::Result SC::AsyncEventLoop::cancelAsync(AsyncRequest& async)
{
    SC_LOG_MESSAGE("{} {} STOP\n", async.debugName, AsyncRequest::TypeToString(async.type));
    const bool asyncStateIsNotFree    = async.state != AsyncRequest::State::Free;
    const bool asyncIsOwnedByThisLoop = async.eventLoop == this;
    SC_TRY_MSG(asyncStateIsNotFree, "Trying to stop AsyncRequest that is not active");
    SC_TRY_MSG(asyncIsOwnedByThisLoop, "Trying to add AsyncRequest belonging to another Loop");
    switch (async.state)
    {
    case AsyncRequest::State::Active: {
        removeActiveHandle(async);
        async.state = AsyncRequest::State::Cancelling;
        submissions.queueBack(async);
        break;
    }
    case AsyncRequest::State::Submitting: {
        async.state = AsyncRequest::State::Teardown;
        break;
    }
    case AsyncRequest::State::Setup: {
        submissions.remove(async);
        break;
    }
    case AsyncRequest::State::Teardown: //
        return SC::Result::Error("Trying to stop AsyncRequest that is already being cancelled (teardown)");
    case AsyncRequest::State::Free: //
        return SC::Result::Error("Trying to stop AsyncRequest that is not active");
    case AsyncRequest::State::Cancelling: //
        return SC::Result::Error("Trying to Stop AsyncRequest that is already being cancelled");
    }
    return Result(true);
}

void SC::AsyncEventLoop::updateTime() { loopTime.snap(); }

void SC::AsyncEventLoop::executeTimers(KernelQueue& queue, const Time::HighResolutionCounter& nextTimer)
{
    const bool timeoutOccurredWithoutIO = queue.getNumEvents() == 0;
    const bool timeoutWasAlreadyExpired = loopTime.isLaterThanOrEqualTo(nextTimer);
    if (timeoutOccurredWithoutIO or timeoutWasAlreadyExpired)
    {
        if (timeoutWasAlreadyExpired)
        {
            // Not sure if this is really possible.
            updateTime();
        }
        else
        {
            loopTime = nextTimer;
        }
        invokeExpiredTimers();
    }
}

SC::Result SC::AsyncEventLoop::wakeUpFromExternalThread(AsyncLoopWakeUp& async)
{
    SC_TRY_MSG(async.eventLoop == this,
               "AsyncEventLoop::wakeUpFromExternalThread - Wakeup belonging to different AsyncEventLoop");
    SC_ASSERT_DEBUG(async.type == AsyncRequest::Type::LoopWakeUp);
    AsyncLoopWakeUp& notifier = *static_cast<AsyncLoopWakeUp*>(&async);
    if (not notifier.pending.exchange(true))
    {
        // This executes if current thread is lucky enough to atomically exchange pending from false to true.
        // This effectively allows coalescing calls from different threads into a single notification.
        SC_TRY(wakeUpFromExternalThread());
    }
    return Result(true);
}

void SC::AsyncEventLoop::executeWakeUps(AsyncResult& result)
{
    for (AsyncLoopWakeUp* async = activeLoopWakeUps.front; async != nullptr;
         async                  = static_cast<AsyncLoopWakeUp*>(async->next))
    {
        SC_ASSERT_DEBUG(async->type == AsyncRequest::Type::LoopWakeUp);
        AsyncLoopWakeUp* notifier = async;
        if (notifier->pending.load() == true)
        {
            AsyncLoopWakeUp::Result asyncResult(*notifier, Result(true));
            asyncResult.async.callback(asyncResult);
            if (notifier->eventObject)
            {
                notifier->eventObject->signal();
            }
            result.reactivateRequest(asyncResult.shouldBeReactivated);
            notifier->pending.exchange(false); // allow executing the notification again
        }
    }
}

void SC::AsyncEventLoop::removeActiveHandle(AsyncRequest& async)
{
    SC_ASSERT_RELEASE(async.state == AsyncRequest::State::Active);
    async.state = AsyncRequest::State::Free;

    if ((async.flags & AsyncRequest::Flag_ManualCompletion) != 0)
    {
        return; // Asyncs flagged to be manually completed, are not added to active handles
    }
    async.eventLoop->numberOfActiveHandles -= 1;
    // clang-format off
    switch (async.type)
    {
        case AsyncRequest::Type::LoopTimeout:   activeLoopTimeouts.remove(*static_cast<AsyncLoopTimeout*>(&async));     break;
        case AsyncRequest::Type::LoopWakeUp:    activeLoopWakeUps.remove(*static_cast<AsyncLoopWakeUp*>(&async));       break;
        case AsyncRequest::Type::ProcessExit:   activeProcessExits.remove(*static_cast<AsyncProcessExit*>(&async));     break;
        case AsyncRequest::Type::SocketAccept:  activeSocketAccepts.remove(*static_cast<AsyncSocketAccept*>(&async));   break;
        case AsyncRequest::Type::SocketConnect: activeSocketConnects.remove(*static_cast<AsyncSocketConnect*>(&async)); break;
        case AsyncRequest::Type::SocketSend:    activeSocketSends.remove(*static_cast<AsyncSocketSend*>(&async));       break;
        case AsyncRequest::Type::SocketReceive: activeSocketReceives.remove(*static_cast<AsyncSocketReceive*>(&async)); break;
        case AsyncRequest::Type::SocketClose:   activeSocketCloses.remove(*static_cast<AsyncSocketClose*>(&async));     break;
        case AsyncRequest::Type::FileRead:      activeFileReads.remove(*static_cast<AsyncFileRead*>(&async));           break;
        case AsyncRequest::Type::FileWrite:     activeFileWrites.remove(*static_cast<AsyncFileWrite*>(&async));         break;
        case AsyncRequest::Type::FileClose:     activeFileCloses.remove(*static_cast<AsyncFileClose*>(&async));         break;
        case AsyncRequest::Type::FilePoll:      activeFilePolls.remove(*static_cast<AsyncFilePoll*>(&async));           break;
    }
    // clang-format on
}

void SC::AsyncEventLoop::addActiveHandle(AsyncRequest& async)
{
    SC_ASSERT_RELEASE(async.state == AsyncRequest::State::Submitting);
    async.state = AsyncRequest::State::Active;

    if ((async.flags & AsyncRequest::Flag_ManualCompletion) != 0)
    {
        return; // Asyncs flagged to be manually completed, are not added to active handles
    }
    async.eventLoop->numberOfActiveHandles += 1;
    // clang-format off
    switch (async.type)
    {
        case AsyncRequest::Type::LoopTimeout:   activeLoopTimeouts.queueBack(*static_cast<AsyncLoopTimeout*>(&async));      break;
        case AsyncRequest::Type::LoopWakeUp:    activeLoopWakeUps.queueBack(*static_cast<AsyncLoopWakeUp*>(&async));        break;
        case AsyncRequest::Type::ProcessExit:   activeProcessExits.queueBack(*static_cast<AsyncProcessExit*>(&async));      break;
        case AsyncRequest::Type::SocketAccept:  activeSocketAccepts.queueBack(*static_cast<AsyncSocketAccept*>(&async));    break;
        case AsyncRequest::Type::SocketConnect: activeSocketConnects.queueBack(*static_cast<AsyncSocketConnect*>(&async));  break;
        case AsyncRequest::Type::SocketSend:    activeSocketSends.queueBack(*static_cast<AsyncSocketSend*>(&async));        break;
        case AsyncRequest::Type::SocketReceive: activeSocketReceives.queueBack(*static_cast<AsyncSocketReceive*>(&async));  break;
        case AsyncRequest::Type::SocketClose:   activeSocketCloses.queueBack(*static_cast<AsyncSocketClose*>(&async));      break;
        case AsyncRequest::Type::FileRead:      activeFileReads.queueBack(*static_cast<AsyncFileRead*>(&async));            break;
        case AsyncRequest::Type::FileWrite:     activeFileWrites.queueBack(*static_cast<AsyncFileWrite*>(&async));          break;
        case AsyncRequest::Type::FileClose:     activeFileCloses.queueBack(*static_cast<AsyncFileClose*>(&async));          break;
        case AsyncRequest::Type::FilePoll: 	    activeFilePolls.queueBack(*static_cast<AsyncFilePoll*>(&async));            break;
    }
    // clang-format on
}

void SC::AsyncEventLoop::scheduleManualCompletion(AsyncRequest& async)
{
    SC_ASSERT_RELEASE(async.state == AsyncRequest::State::Setup or async.state == AsyncRequest::State::Submitting);
    async.eventLoop->manualCompletions.queueBack(async);
    async.flags |= AsyncRequest::Flag_ManualCompletion;
}

SC::Result SC::AsyncEventLoop::createAsyncTCPSocket(SocketFlags::AddressFamily family, SocketDescriptor& outDescriptor)
{
    auto res = outDescriptor.create(family, SocketFlags::SocketStream, SocketFlags::ProtocolTcp,
                                    SocketFlags::NonBlocking, SocketFlags::NonInheritable);
    SC_TRY(res);
    return associateExternallyCreatedTCPSocket(outDescriptor);
}

SC::Result SC::AsyncEventLoop::wakeUpFromExternalThread() { return internal.get().wakeUpFromExternalThread(); }

SC::Result SC::AsyncEventLoop::associateExternallyCreatedTCPSocket(SocketDescriptor& outDescriptor)
{
    return internal.get().associateExternallyCreatedTCPSocket(outDescriptor);
}

SC::Result SC::AsyncEventLoop::associateExternallyCreatedFileDescriptor(FileDescriptor& outDescriptor)
{
    return internal.get().associateExternallyCreatedFileDescriptor(outDescriptor);
}

SC::Result SC::AsyncLoopWakeUp::wakeUp() { return getEventLoop()->wakeUpFromExternalThread(*this); }

#if SC_PLATFORM_LINUX
#else
bool SC::AsyncEventLoop::tryLoadingLiburing() { return false; }
#endif
