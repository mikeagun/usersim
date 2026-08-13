// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT

#if !defined(CMAKE_NUGET)
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif
#include "usersim/ke.h"
#include "usersim/mm.h"
#include "usersim/ob.h"

TEST_CASE("ObfReferenceObject", "[ob]")
{
    int x = 0;
    HANDLE handle = &x;
    void* object;
    REQUIRE(ObReferenceObjectByHandle(handle, 0, nullptr, 0, &object, nullptr) == STATUS_SUCCESS);
    REQUIRE(object == &x);
    REQUIRE(ObfReferenceObject(&x) == 2);
    REQUIRE(ObfReferenceObject(&x) == 3);
    REQUIRE(ObCloseHandle(handle, 0) == STATUS_SUCCESS);
    REQUIRE(ObfDereferenceObject(&x) == 1);
}

TEST_CASE("ObReferenceObjectByHandle pseudo-handle", "[ob]")
{
    // A value that is not a handle to a Win32 object is passed through unchanged,
    // even when an event is requested, so that callers that use a pointer to
    // their own dispatcher object as a pseudo-handle keep working.
    KEVENT event;
    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    void* object = nullptr;
    REQUIRE(ObReferenceObjectByHandle((HANDLE)&event, 0, *ExEventObjectType, 0, &object, nullptr) == STATUS_SUCCESS);
    REQUIRE(object == &event);

    // The object is still the caller's own event.
    REQUIRE(KeSetEvent((PKEVENT)object, 0, FALSE) == 0);
    LARGE_INTEGER timeout = {0};
    REQUIRE(KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout) == STATUS_SUCCESS);
}

TEST_CASE("ObReferenceObjectByHandle event", "[ob]")
{
    HANDLE handle = CreateEvent(nullptr, TRUE /* manual reset */, FALSE, nullptr);
    REQUIRE(handle != nullptr);

    void* object = nullptr;
    REQUIRE(
        ObReferenceObjectByHandle(handle, EVENT_MODIFY_STATE, *ExEventObjectType, 0, &object, nullptr) ==
        STATUS_SUCCESS);
    REQUIRE(object != nullptr);

    // The returned object must be usable as a KEVENT, and signaling it must
    // signal the event the caller's handle refers to.
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_TIMEOUT);
    KeSetEvent((PKEVENT)object, 0, FALSE);
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0);

    // Clearing it must reset the caller's event.
    KeClearEvent((PKEVENT)object);
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_TIMEOUT);

    // KeWaitForSingleObject must observe the same state.
    LARGE_INTEGER timeout = {0};
    REQUIRE(KeWaitForSingleObject(object, Executive, KernelMode, FALSE, &timeout) == STATUS_TIMEOUT);
    REQUIRE(SetEvent(handle));
    REQUIRE(KeWaitForSingleObject(object, Executive, KernelMode, FALSE, &timeout) == STATUS_SUCCESS);

    // Releasing the object must not invalidate the caller's handle.
    REQUIRE(ObfDereferenceObject(object) == 0);
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0);

    REQUIRE(CloseHandle(handle));
}

TEST_CASE("ObReferenceObjectByHandle event independent references", "[ob]")
{
    HANDLE handle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    REQUIRE(handle != nullptr);

    // Each reference produces an independent object, because a handle value can
    // be recycled by the operating system and so is not a stable identity.
    void* first = nullptr;
    void* second = nullptr;
    REQUIRE(
        ObReferenceObjectByHandle(handle, EVENT_MODIFY_STATE, *ExEventObjectType, 0, &first, nullptr) ==
        STATUS_SUCCESS);
    REQUIRE(
        ObReferenceObjectByHandle(handle, EVENT_MODIFY_STATE, *ExEventObjectType, 0, &second, nullptr) ==
        STATUS_SUCCESS);
    REQUIRE(first != second);

    // Both are bound to the same underlying event.
    KeSetEvent((PKEVENT)first, 0, FALSE);
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0);
    KeClearEvent((PKEVENT)second);
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_TIMEOUT);

    REQUIRE(ObfDereferenceObject(first) == 0);
    // Releasing one must not disturb the other.
    KeSetEvent((PKEVENT)second, 0, FALSE);
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0);
    REQUIRE(ObfDereferenceObject(second) == 0);

    REQUIRE(CloseHandle(handle));
}

TEST_CASE("ObOpenObjectByPointer event", "[ob]")
{
    HANDLE handle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    REQUIRE(handle != nullptr);

    void* object = nullptr;
    REQUIRE(
        ObReferenceObjectByHandle(handle, EVENT_MODIFY_STATE, *ExEventObjectType, 0, &object, nullptr) ==
        STATUS_SUCCESS);

    // Opening a handle to an event object must yield a handle to the same event.
    HANDLE opened = nullptr;
    REQUIRE(
        ObOpenObjectByPointer(object, 0, nullptr, EVENT_MODIFY_STATE | SYNCHRONIZE, nullptr, 0, &opened) ==
        STATUS_SUCCESS);
    REQUIRE(opened != nullptr);

    REQUIRE(SetEvent(opened));
    REQUIRE(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0);

    REQUIRE(CloseHandle(opened));
    REQUIRE(ObfDereferenceObject(object) == 0);
    REQUIRE(CloseHandle(handle));
}
