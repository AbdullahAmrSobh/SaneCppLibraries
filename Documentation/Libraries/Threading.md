@page library_threading Threading

@brief 🟥 Atomic, thread, mutex, condition variable

[TOC]

Threading is a library defining basic primitives for user-space threading and synchronization.

# Features
| Class                 | Description                       |
|:----------------------|:----------------------------------|
| SC::Thread            | @copybrief SC::Thread             |
| SC::Mutex             | @copybrief SC::Mutex              |
| SC::ConditionVariable | @copybrief SC::ConditionVariable  |
| SC::Atomic            | @copybrief SC::Atomic             |
| SC::EventObject       | @copybrief SC::EventObject        |

# Status
🟥 Draft  
Only the features needed for other libraries have been implemented so far.
The Atomic header is really only being implemented for a few data types and needs some love to extend and improve it.

# Description

## SC::Thread
@copydoc SC::Thread

## SC::Mutex
@copydoc SC::Mutex

## SC::EventObject
@copydoc SC::EventObject

## SC::Atomic
@copydoc SC::Atomic

# Roadmap
🟨 MVP
- Scoped Lock / Unlock

🟩 Usable
- Semaphores

🟦 Complete Features:
- Support more types in Atomic<T>

💡 Unplanned Features:
- ReadWrite Lock
- Barrier
