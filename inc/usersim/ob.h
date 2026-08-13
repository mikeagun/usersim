// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "usersim/ke.h"

CXPLAT_EXTERN_C_BEGIN

_IRQL_requires_max_(DISPATCH_LEVEL) USERSIM_API LONG_PTR ObfReferenceObject(_In_ PVOID object);

_IRQL_requires_max_(DISPATCH_LEVEL) USERSIM_API LONG_PTR ObfDereferenceObject(_In_ PVOID object);

typedef struct _OBJECT_TYPE* POBJECT_TYPE;

USERSIM_API extern POBJECT_TYPE* ExEventObjectType;
USERSIM_API extern POBJECT_TYPE* IoFileObjectType;

typedef struct _OBJECT_HANDLE_INFORMATION
{
    ULONG HandleAttributes;
    ACCESS_MASK GrantedAccess;
} OBJECT_HANDLE_INFORMATION, *POBJECT_HANDLE_INFORMATION;

/**
 * @brief Reference an object by handle.
 *
 * When ExEventObjectType is requested and the handle refers to a Win32 event,
 * the returned object is a KEVENT bound to that event, so it can be passed to
 * KeSetEvent() and the other event routines. Two things differ from kernel mode:
 * each reference returns a distinct object rather than the same object with a
 * higher reference count, so object pointers cannot be compared for identity;
 * and each reference holds a duplicated handle until it is released, so callers
 * should release with ObfDereferenceObject() rather than relying on teardown.
 *
 * Any other value is returned unchanged, as are all other object types.
 */
_IRQL_requires_max_(PASSIVE_LEVEL) USERSIM_API NTSTATUS ObReferenceObjectByHandle(
    _In_ HANDLE handle,
    _In_ ACCESS_MASK desired_access,
    _In_opt_ POBJECT_TYPE object_type,
    _In_ KPROCESSOR_MODE access_mode,
    _Out_ PVOID* object,
    _Out_opt_ POBJECT_HANDLE_INFORMATION handle_information);

_IRQL_requires_max_(PASSIVE_LEVEL) USERSIM_API NTSTATUS
ObOpenObjectByPointer(
    _In_ PVOID object,
    _In_ ULONG handle_attributes,
    _In_opt_ void* passed_access_state,
    _In_ ACCESS_MASK desired_access,
    _In_opt_ POBJECT_TYPE object_type,
    _In_ KPROCESSOR_MODE access_mode,
    _Out_ HANDLE* handle);

USERSIM_API
NTSTATUS
ObCloseHandle(_In_ _Post_ptr_invalid_ HANDLE handle, _In_ KPROCESSOR_MODE previous_mode);

/**
 * @brief Release any event objects created by ObReferenceObjectByHandle() that
 * were not released by ObfDereferenceObject(). Called during platform teardown.
 */
USERSIM_API
void
usersim_clean_up_ob();

CXPLAT_EXTERN_C_END
