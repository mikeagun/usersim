// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT

#include "kernel_um.h"
#include "ob_internal.h"
#include "platform.h"
#include "usersim/ob.h"
#include "utilities.h"

#include <map>
#include <mutex>
#include <set>

// Guards _object_references and _event_objects. The object manager can be driven
// from multiple threads, so the containers need to be serialized.
static std::mutex _ob_lock;

static std::map<PVOID, ULONG> _object_references;

// Event objects created by ObReferenceObjectByHandle(), keyed by the KEVENT
// address handed out to the caller. Keyed on the object rather than on the
// handle because Windows recycles handle values: a closed handle can be reissued
// for an unrelated object, so a handle value is not a stable identity.
static std::set<PVOID> _event_objects;

// The object types are opaque to callers, which only ever pass them back in. Give
// the event type a distinct non-null identity so that a caller asking for an
// event can be told apart from a caller that passes no object type at all.
static int _ExEventObjectTypeIdentity = 0;
static POBJECT_TYPE _ExEventObjectType = (POBJECT_TYPE)&_ExEventObjectTypeIdentity;
USERSIM_API __declspec(align(8)) POBJECT_TYPE* ExEventObjectType = &_ExEventObjectType;

static POBJECT_TYPE _IoFileObjectType = nullptr;
USERSIM_API __declspec(align(8)) POBJECT_TYPE* IoFileObjectType = &_IoFileObjectType;

/**
 * @brief Release an event object created by ObReferenceObjectByHandle().
 *
 * @param[in] object KEVENT address that was handed out to the caller.
 */
static void
_free_event_object(_In_ _Post_invalid_ PVOID object)
{
    auto handle_event = CONTAINING_RECORD((KEVENT*)object, usersim_handle_event_t, event);
    if (handle_event->handle != nullptr) {
        ::CloseHandle(handle_event->handle);
    }
    delete handle_event;
}

_IRQL_requires_max_(DISPATCH_LEVEL) USERSIM_API LONG_PTR ObfReferenceObject(_In_ PVOID object)
{
    std::unique_lock lock(_ob_lock);
    if (!_object_references.contains(object)) {
        _object_references[object] = 1;
    } else {
        _object_references[object]++;
    }
    return _object_references[object];
}

_IRQL_requires_max_(DISPATCH_LEVEL) USERSIM_API LONG_PTR ObfDereferenceObject(_In_ PVOID object)
{
    bool is_event_object = false;
    ULONG remaining;
    {
        std::unique_lock lock(_ob_lock);
        remaining = --_object_references[object];
        if (remaining == 0) {
            _object_references.erase(object);

            // An event object owns a duplicated handle, so release that rather
            // than treating the object itself as a handle.
            is_event_object = (_event_objects.erase(object) != 0);
        }
    }

    if (remaining == 0) {
        if (is_event_object) {
            _free_event_object(object);
        } else {
            CloseHandle(object);
        }
    }
    return remaining;
}

/**
 * @brief Create an event bound to the caller's Win32 event object.
 *
 * @param[in] handle Event handle to bind to.
 * @param[out] object On success, the address of the new KEVENT.
 * @retval STATUS_SUCCESS An event object was created.
 * @retval STATUS_INVALID_HANDLE The value is not a handle to a Win32 object.
 * @retval STATUS_INSUFFICIENT_RESOURCES An allocation failed.
 */
static NTSTATUS
_create_event_object(_In_ HANDLE handle, _Outptr_ PVOID* object)
{
    *object = nullptr;

    // Duplicate the handle so this object owns its own reference to the event and
    // releasing it never invalidates the caller's handle. A duplicate refers to
    // the same underlying event, so signaling it wakes anyone waiting on the
    // caller's handle.
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(
            GetCurrentProcess(), handle, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return STATUS_INVALID_HANDLE;
    }

    auto handle_event = new (std::nothrow) usersim_handle_event_t;
    if (handle_event == nullptr) {
        ::CloseHandle(duplicate);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(&handle_event->event, NotificationEvent, FALSE);
    // The Win32 event owns the signaled state and its own reset mode, so the type
    // and signaled fields are not consulted for this kind of event.
    handle_event->event.object_type = USERSIM_OBJECT_TYPE_EVENT_HANDLE;
    handle_event->handle = duplicate;

    try {
        std::unique_lock lock(_ob_lock);
        _event_objects.insert(&handle_event->event);
    } catch (const std::bad_alloc&) {
        ::CloseHandle(duplicate);
        delete handle_event;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *object = &handle_event->event;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL) USERSIM_API NTSTATUS ObReferenceObjectByHandle(
    _In_ HANDLE handle,
    _In_ ACCESS_MASK desired_access,
    _In_opt_ POBJECT_TYPE object_type,
    _In_ KPROCESSOR_MODE access_mode,
    _Out_ PVOID* object,
    _Out_opt_ POBJECT_HANDLE_INFORMATION handle_information)
{
    UNREFERENCED_PARAMETER(desired_access);
    UNREFERENCED_PARAMETER(access_mode);

    if (handle_information != nullptr) {
        *object = handle;
        return STATUS_NOT_SUPPORTED;
    }

    // A caller asking for an event expects something it can pass to KeSetEvent()
    // and the other dispatcher routines, so hand back a KEVENT bound to the event
    // the handle refers to.
    if (object_type != nullptr && object_type == *ExEventObjectType) {
        PVOID event_object = nullptr;
        NTSTATUS status = _create_event_object(handle, &event_object);
        if (NT_SUCCESS(status)) {
            *object = event_object;
            ObfReferenceObject(*object);
            return STATUS_SUCCESS;
        }
        if (status != STATUS_INVALID_HANDLE) {
            // Report the failure, but still write the out parameter.
            *object = handle;
            return status;
        }
        // The value is not a handle to a Win32 object. Fall through to the
        // pass-through behavior below, so callers that use a pointer to their own
        // dispatcher object as a pseudo-handle keep working.
    }

    *object = handle;
    ObfReferenceObject(*object);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL) USERSIM_API NTSTATUS ObOpenObjectByPointer(
    _In_ PVOID object,
    _In_ ULONG handle_attributes,
    _In_opt_ void* passed_access_state,
    _In_ ACCESS_MASK desired_access,
    _In_opt_ POBJECT_TYPE object_type,
    _In_ KPROCESSOR_MODE access_mode,
    _Out_ HANDLE* handle)
{
    UNREFERENCED_PARAMETER(handle_attributes);
    UNREFERENCED_PARAMETER(passed_access_state);
    UNREFERENCED_PARAMETER(object_type);
    UNREFERENCED_PARAMETER(access_mode);

    // An event object is not itself a handle, so open the event it is bound to.
    HANDLE source = (HANDLE)object;
    {
        std::unique_lock lock(_ob_lock);
        if (_event_objects.contains(object)) {
            source = CONTAINING_RECORD((KEVENT*)object, usersim_handle_event_t, event)->handle;
        }
    }

    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), handle, desired_access, FALSE, 0)) {
        return win32_error_to_usersim_error(GetLastError());
    }

    return STATUS_SUCCESS;
}

USERSIM_API
NTSTATUS
ObCloseHandle(_In_ _Post_ptr_invalid_ HANDLE handle, _In_ KPROCESSOR_MODE previous_mode)
{
    UNREFERENCED_PARAMETER(previous_mode);
    ObfDereferenceObject(handle);
    return STATUS_SUCCESS;
}

void
usersim_clean_up_ob()
{
    std::set<PVOID> event_objects;
    {
        std::unique_lock lock(_ob_lock);
        // Callers commonly release objects with ObDereferenceObject(), which this
        // library defines as a no-op, so anything still registered here has not
        // been released and is cleaned up now.
        event_objects.swap(_event_objects);
        for (auto object : event_objects) {
            _object_references.erase(object);
        }
    }

    for (auto object : event_objects) {
        _free_event_object(object);
    }
}
