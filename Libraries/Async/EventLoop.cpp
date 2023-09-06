// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#include "EventLoop.h"
#include "../Foundation/Optional.h"
#include "../Foundation/Result.h"
#include "../Threading/Threading.h" // EventObject

#if SC_PLATFORM_WINDOWS
#include "EventLoopInternalWindows.inl"
#elif SC_PLATFORM_EMSCRIPTEN
#include "EventLoopInternalEmscripten.inl"
#elif SC_PLATFORM_APPLE
#include "EventLoopInternalApple.inl"
#endif

template <>
void SC::OpaqueFuncs<SC::EventLoop::InternalTraits>::construct(Handle& buffer)
{
    new (&buffer.reinterpret_as<Object>(), PlacementNew()) Object();
}
template <>
void SC::OpaqueFuncs<SC::EventLoop::InternalTraits>::destruct(Object& obj)
{
    obj.~Object();
}

SC::ReturnCode SC::EventLoop::startTimeout(AsyncTimeout& async, IntegerMilliseconds expiration,
                                           Function<void(AsyncTimeoutResult&)>&& callback)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback = callback;
    updateTime();
    async.expirationTime = loopTime.offsetBy(expiration);
    async.timeout        = expiration;
    return true;
}

SC::ReturnCode SC::EventLoop::startRead(AsyncRead& async, FileDescriptor::Handle fileDescriptor, Span<char> readBuffer,
                                        Function<void(AsyncReadResult&)>&& callback)
{
    SC_TRY_MSG(readBuffer.sizeInBytes() > 0, "EventLoop::startRead - Zero sized read buffer"_a8);
    SC_TRY_IF(queueSubmission(async));
    async.callback       = callback;
    async.fileDescriptor = fileDescriptor;
    async.readBuffer     = readBuffer;
    return true;
}

SC::ReturnCode SC::EventLoop::startWrite(AsyncWrite& async, FileDescriptor::Handle fileDescriptor,
                                         Span<const char> writeBuffer, Function<void(AsyncWriteResult&)>&& callback)
{
    SC_TRY_MSG(writeBuffer.sizeInBytes() > 0, "EventLoop::startWrite - Zero sized write buffer"_a8);
    SC_TRY_IF(queueSubmission(async));
    async.callback       = callback;
    async.fileDescriptor = fileDescriptor;
    async.writeBuffer    = writeBuffer;
    return true;
}

SC::ReturnCode SC::EventLoop::startAccept(AsyncAccept& async, const SocketDescriptor& socketDescriptor,
                                          Function<void(AsyncAcceptResult&)>&& callback)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback = callback;
    SC_TRY_IF(socketDescriptor.get(async.handle, "Invalid handle"_a8));
    SC_TRY_IF(socketDescriptor.getAddressFamily(async.addressFamily));
    return true;
}

SC::ReturnCode SC::EventLoop::startConnect(AsyncConnect& async, const SocketDescriptor& socketDescriptor,
                                           SocketIPAddress ipAddress, Function<void(AsyncConnectResult&)>&& callback)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback = callback;
    SC_TRY_IF(socketDescriptor.get(async.handle, "Invalid handle"_a8));
    async.ipAddress = ipAddress;
    return true;
}

SC::ReturnCode SC::EventLoop::startSend(AsyncSend& async, const SocketDescriptor& socketDescriptor,
                                        Span<const char> data, Function<void(AsyncSendResult&)>&& callback)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback = callback;
    SC_TRY_IF(socketDescriptor.get(async.handle, "Invalid handle"_a8));
    async.data = data;
    return true;
}

SC::ReturnCode SC::EventLoop::startReceive(AsyncReceive& async, const SocketDescriptor& socketDescriptor,
                                           Span<char> data, Function<void(AsyncReceiveResult&)>&& callback)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback = callback;
    SC_TRY_IF(socketDescriptor.get(async.handle, "Invalid handle"_a8));
    async.data = data;
    return true;
}

SC::ReturnCode SC::EventLoop::startWakeUp(AsyncWakeUp& async, Function<void(AsyncWakeUpResult&)>&& callback,
                                          EventObject* eventObject)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback    = callback;
    async.eventObject = eventObject;
    return true;
}

SC::ReturnCode SC::EventLoop::startProcessExit(AsyncProcessExit&                         async,
                                               Function<void(AsyncProcessExitResult&)>&& callback,
                                               ProcessDescriptor::Handle                 process)
{
    SC_TRY_IF(queueSubmission(async));
    async.callback = callback;
    async.handle   = process;
    return true;
}

SC::ReturnCode SC::EventLoop::queueSubmission(Async& async)
{
    const bool asyncStateIsFree      = async.state == Async::State::Free;
    const bool asyncIsNotOwnedByLoop = async.eventLoop == nullptr;
    SC_DEBUG_ASSERT(asyncStateIsFree and asyncIsNotOwnedByLoop);
    SC_TRY_MSG(asyncStateIsFree, "Trying to stage Async that is in use"_a8);
    SC_TRY_MSG(asyncIsNotOwnedByLoop, "Trying to add Async belonging to another Loop"_a8);
    async.state = Async::State::Submitting;
    submissions.queueBack(async);
    async.eventLoop = this;
    return true;
}

SC::ReturnCode SC::EventLoop::run()
{
    while (getTotalNumberOfActiveHandle() > 0 or not submissions.isEmpty())
    {
        SC_TRY_IF(runOnce());
    };
    return true;
}

const SC::TimeCounter* SC::EventLoop::findEarliestTimer() const
{
    const TimeCounter* earliestTime = nullptr;
    for (Async* async = activeTimers.front; async != nullptr; async = async->next)
    {
        SC_DEBUG_ASSERT(async->getType() == Async::Type::Timeout);
        const auto& expirationTime = async->asTimeout()->expirationTime;
        if (earliestTime == nullptr or earliestTime->isLaterThanOrEqualTo(expirationTime))
        {
            earliestTime = &expirationTime;
        }
    };
    return earliestTime;
}

void SC::EventLoop::invokeExpiredTimers()
{
    for (Async* async = activeTimers.front; async != nullptr;)
    {
        SC_DEBUG_ASSERT(async->getType() == Async::Type::Timeout);
        const auto& expirationTime = async->asTimeout()->expirationTime;
        if (loopTime.isLaterThanOrEqualTo(expirationTime))
        {
            Async* currentAsync = async;
            async               = async->next;
            activeTimers.remove(*currentAsync);
            currentAsync->state = Async::State::Free;
            AsyncResult::Timeout res(*currentAsync, nullptr);
            res.async.eventLoop = nullptr; // Allow reusing it
            currentAsync->asTimeout()->callback(static_cast<AsyncResult::Timeout&>(res));
        }
        else
        {
            async = async->next;
        }
    }
}

SC::ReturnCode SC::EventLoop::create()
{
    Internal& self = internal.get();
    SC_TRY_IF(self.createEventLoop());
    SC_TRY_IF(self.createWakeup(*this));
    return true;
}

SC::ReturnCode SC::EventLoop::close()
{
    Internal& self = internal.get();
    return self.close();
}

SC::ReturnCode SC::EventLoop::stageSubmissions(SC::EventLoop::KernelQueue& queue)
{
    while (Async* async = submissions.dequeueFront())
    {
        switch (async->state)
        {
        case Async::State::Submitting: {
            SC_TRY_IF(queue.stageAsync(*this, *async));
            SC_TRY_IF(queue.activateAsync(*this, *async));
            async->state = Async::State::Active;
        }
        break;
        case Async::State::Free: {
            // TODO: Stop the completion, it has been cancelled before being submitted
            SC_RELEASE_ASSERT(false);
        }
        break;
        case Async::State::Cancelling: {
            SC_TRY_IF(queue.cancelAsync(*this, *async));
        }
        break;
        case Async::State::Active: {
            SC_DEBUG_ASSERT(false);
            return "EventLoop::processSubmissions() got Active handle"_a8;
        }
        break;
        }
    }
    return true;
}

void SC::EventLoop::increaseActiveCount() { numberOfExternals += 1; }

void SC::EventLoop::decreaseActiveCount() { numberOfExternals -= 1; }

int SC::EventLoop::getTotalNumberOfActiveHandle() const
{
    return numberOfActiveHandles + numberOfTimers + numberOfWakeups + numberOfExternals;
}

SC::ReturnCode SC::EventLoop::runOnce() { return runStep(PollMode::ForcedForwardProgress); }

SC::ReturnCode SC::EventLoop::runNoWait() { return runStep(PollMode::NoWait); }

SC::ReturnCode SC::EventLoop::runStep(PollMode pollMode)
{
    KernelQueue queue;

    auto onErrorRestoreSubmissions = MakeDeferred(
        [this]
        {
            if (not stagedHandles.isEmpty())
            {
                while (Async* async = submissions.dequeueFront())
                {
                    async->state = Async::State::Submitting;
                    submissions.queueBack(*async);
                }
            }
        });
    SC_TRY_IF(stageSubmissions(queue));

    SC_RELEASE_ASSERT(submissions.isEmpty());

    if (stagedHandles.isEmpty() and getTotalNumberOfActiveHandle() <= 0)
        return true; // happens when we do cancelAsync on the last active one
    SC_TRY_IF(queue.pollAsync(*this, pollMode));

    Internal& self = internal.get();
    for (decltype(KernelQueue::newEvents) idx = 0; idx < queue.newEvents; ++idx)
    {
        Async& async = *self.getAsync(queue.events[idx]);
        if (!self.canRunCompletionFor(queue.events[idx]))
            continue;
        void* userData = self.getUserData(queue.events[idx]);
        bool  rearm    = false;
        switch (async.getType())
        {
        case Async::Type::Timeout: {
            AsyncResult::Timeout result(async, userData);
            ReturnCode           res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::Read: {
            AsyncResult::Read result(async, userData);
            ReturnCode        res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::Write: {
            AsyncResult::Write result(async, userData);
            ReturnCode         res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::WakeUp: {
            AsyncResult::WakeUp result(async, userData);
            ReturnCode          res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::ProcessExit: {
            AsyncResult::ProcessExit result(async, userData);
            ReturnCode               res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::Accept: {
            AsyncResult::Accept result(async, userData);
            ReturnCode          res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::Connect: {
            AsyncResult::Connect result(async, userData);
            ReturnCode           res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::Send: {
            AsyncResult::Send result(async, userData);
            ReturnCode        res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        case Async::Type::Receive: {
            AsyncResult::Receive result(async, userData);
            ReturnCode           res = self.runCompletionFor(result, queue.events[idx]);
            if (res and result.async.callback.isValid())
                result.async.callback(result);
            rearm = result.isRearmed();
            break;
        }
        }

        switch (async.state)
        {
        case Async::State::Active: SC_TRY_IF(queue.rearmOrCancel(*this, async, rearm)); break;
        default: break;
        }
    }

    return true;
}

SC::ReturnCode SC::EventLoop::stopAsync(Async& async)
{
    const bool asyncStateIsNotFree    = async.state != Async::State::Free;
    const bool asyncIsOwnedByThisLoop = async.eventLoop == this;
    SC_DEBUG_ASSERT(asyncStateIsNotFree and asyncIsOwnedByThisLoop);
    SC_TRY_MSG(asyncStateIsNotFree, "Trying to stop Async that is not active"_a8);
    SC_TRY_MSG(asyncIsOwnedByThisLoop, "Trying to add Async belonging to another Loop"_a8);
    switch (async.state)
    {
    case Async::State::Active: {
        // We don't know in which queue this is gone so we remove from all
        activeHandles.remove(async);
        activeTimers.remove(static_cast<Async::Timeout&>(async));
        activeWakeUps.remove(async);
        async.state = Async::State::Cancelling;
        submissions.queueBack(async);
        break;
    }
    case Async::State::Submitting: {
        submissions.remove(async);
        break;
    }
    case Async::State::Free: return "Trying to stop Async that is not active"_a8;
    case Async::State::Cancelling: return "Trying to Stop Async that is already being cancelled"_a8;
    }
    return true;
}

void SC::EventLoop::updateTime() { loopTime.snap(); }

void SC::EventLoop::executeTimers(KernelQueue& queue, const TimeCounter& nextTimer)
{
    const bool timeoutOccurredWithoutIO = queue.newEvents == 0;
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

SC::ReturnCode SC::EventLoop::wakeUpFromExternalThread(AsyncWakeUp& wakeUp)
{
    SC_TRY_MSG(wakeUp.eventLoop == this,
               "EventLoop::wakeUpFromExternalThread - Wakeup belonging to different EventLoop"_a8);
    if (not wakeUp.asWakeUp()->pending.exchange(true))
    {
        // This executes if current thread is lucky enough to atomically exchange pending from false to true.
        // This effectively allows coalescing calls from different threads into a single notification.
        SC_TRY_IF(wakeUpFromExternalThread());
    }
    return true;
}

void SC::EventLoop::executeWakeUps(AsyncResult& result)
{
    for (Async* async = activeWakeUps.front; async != nullptr; async = async->next)
    {
        Async::WakeUp* notifier = async->asWakeUp();
        if (notifier->pending.load() == true)
        {
            AsyncResult::WakeUp res(*async, nullptr);
            res.async.callback(res);
            if (notifier->eventObject)
            {
                notifier->eventObject->signal();
            }
            result.rearm(res.isRearmed());
            notifier->pending.exchange(false); // allow executing the notification again
        }
    }
}

void SC::EventLoop::removeActiveHandle(Async& async)
{
    async.eventLoop->activeHandles.remove(async);
    async.eventLoop->numberOfActiveHandles -= 1;
}

void SC::EventLoop::addActiveHandle(Async& async)
{
    async.eventLoop->activeHandles.queueBack(async);
    async.eventLoop->numberOfActiveHandles += 1;
}

SC::ReturnCode SC::EventLoop::getLoopFileDescriptor(SC::FileDescriptor::Handle& fileDescriptor) const
{
    return internal.get().loopFd.get(fileDescriptor, "EventLoop::getLoopFileDescriptor invalid handle"_a8);
}

SC::ReturnCode SC::AsyncWakeUp::wakeUp() { return eventLoop->wakeUpFromExternalThread(*this); }
