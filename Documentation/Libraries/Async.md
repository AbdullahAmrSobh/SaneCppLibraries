@page library_async Async

@brief 🟨 Async I/O (files, sockets, timers, processes, fs events, threads wake-up)

[TOC]

Async is a multi-platform / event-driven asynchronous I/O library.  

@copydetails group_async

# Features

This is the list of supported async operations:

| Async Operation                                   | Description                       |
|---------------------------------------------------|-----------------------------------|
| [AsyncSocketConnect](@ref SC::AsyncSocketConnect) | @copybrief SC::AsyncSocketConnect | 
| [AsyncSocketAccept](@ref SC::AsyncSocketAccept)   | @copybrief SC::AsyncSocketAccept  |
| [AsyncSocketSend](@ref SC::AsyncSocketSend)       | @copybrief SC::AsyncSocketSend    |
| [AsyncSocketReceive](@ref SC::AsyncSocketReceive) | @copybrief SC::AsyncSocketReceive |
| [AsyncSocketClose](@ref SC::AsyncSocketClose)     | @copybrief SC::AsyncSocketClose   |
| [AsyncFileRead](@ref SC::AsyncFileRead)           | @copybrief SC::AsyncFileRead      |
| [AsyncFileWrite](@ref SC::AsyncFileWrite)         | @copybrief SC::AsyncFileWrite     |
| [AsyncFileClose](@ref SC::AsyncFileClose)         | @copybrief SC::AsyncFileClose     |
| [AsyncLoopTimeout](@ref SC::AsyncLoopTimeout)     | @copybrief SC::AsyncLoopTimeout   |
| [AsyncLoopWakeUp](@ref SC::AsyncLoopWakeUp)       | @copybrief SC::AsyncLoopWakeUp    |
| [AsyncLoopWork](@ref SC::AsyncLoopWork)           | @copybrief SC::AsyncLoopWork      |
| [AsyncProcessExit](@ref SC::AsyncProcessExit)     | @copybrief SC::AsyncProcessExit   |
| [AsyncFilePoll](@ref SC::AsyncFilePoll)           | @copybrief SC::AsyncFilePoll      |

# Status
🟨 MVP  
This is usable but needs some more testing and a few more features.

# Description
@copydetails SC::AsyncRequest

## AsyncEventLoop
@copydoc SC::AsyncEventLoop

### Run modes

Event loop can be run in different ways to allow integrated it in multiple ways in applications.

| Run mode                      | Description                               |
|:------------------------------|:------------------------------------------|
| SC::AsyncEventLoop::run       | @copydoc SC::AsyncEventLoop::run          |
| SC::AsyncEventLoop::runOnce   | @copydoc SC::AsyncEventLoop::runOnce      |
| SC::AsyncEventLoop::runNoWait | @copydoc SC::AsyncEventLoop::runNoWait    |


Alternatively user can explicitly use three methods to submit, poll and dispatch events.
This is very useful to integrate the event loop into applications with other event loops (for example GUI applications).

| Run mode                                  | Description                                       |
|:------------------------------------------|:--------------------------------------------------|
| SC::AsyncEventLoop::submitRequests        | @copydoc SC::AsyncEventLoop::submitRequests       |
| SC::AsyncEventLoop::blockingPoll          | @copydoc SC::AsyncEventLoop::blockingPoll         |
| SC::AsyncEventLoop::dispatchCompletions   | @copydoc SC::AsyncEventLoop::dispatchCompletions  |

## AsyncLoopTimeout
@copydoc SC::AsyncLoopTimeout

## AsyncLoopWakeUp
@copydoc SC::AsyncLoopWakeUp

## AsyncLoopWork
@copydoc SC::AsyncLoopWork

## AsyncProcessExit
@copydoc SC::AsyncProcessExit

## AsyncSocketAccept
@copydoc SC::AsyncSocketAccept

## AsyncSocketConnect
@copydoc SC::AsyncSocketConnect

## AsyncSocketSend
@copydoc SC::AsyncSocketSend

## AsyncSocketReceive
@copydoc SC::AsyncSocketReceive

## AsyncSocketClose
@copydoc SC::AsyncSocketClose

## AsyncFileRead
@copydoc SC::AsyncFileRead

## AsyncFileWrite
@copydoc SC::AsyncFileWrite

## AsyncFileClose
@copydoc SC::AsyncFileClose

## AsyncFilePoll
@copydoc SC::AsyncFilePoll

# Implementation

Library abstracts async operations by exposing a completion based mechanism.
This mechanism currently maps on `kqueue` on macOS and `OVERLAPPED` on Windows.

It currently tries to dynamically load `io_uring` on Linux doing an `epoll` backend fallback in case `liburing` is not available on the system.
There is not need to link `liburing` because the library loads it dynamically and embeds the minimal set of `static` `inline` functions needed to interface with it.

The api works on file and socket descriptors, that can be obtained from the [File](@ref library_file) and [Socket](@ref library_socket) libraries.

## Memory allocation
The entire library is free of allocations, as it uses a double linked list inside SC::AsyncRequest.  
Caller is responsible for keeping AsyncRequest-derived objects memory stable until async callback is called.  
SC::ArenaMap from the [Containers](@ref library_containers) can be used to preallocate a bounded pool of Async objects.

# Roadmap

🟩 Usable Features:
- More comprehensive test suite, testing all cancellations
- FS operations (open stat read write unlink copyfile mkdir chmod etc.)
- UDP Send/Receive
- DNS Resolution

🟦 Complete Features:
- TTY with ANSI Escape Codes

💡 Unplanned Features:
- Signal handling
