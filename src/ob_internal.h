// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT

#pragma once

#include "usersim/ke.h"

/**
 * @file
 * @brief Internal definitions shared between the object manager simulation and
 * the kernel dispatcher object simulation.
 */

CXPLAT_EXTERN_C_BEGIN

/**
 * @brief An event that is bound to a Win32 event object.
 *
 * ObReferenceObjectByHandle() returns a PKEVENT for ExEventObjectType, so that
 * callers can pass the result to KeSetEvent() and friends the way they would in
 * kernel mode. The KEVENT must be the first member: the address of the KEVENT is
 * what is handed out as the object, and the containing structure is recovered
 * from it with CONTAINING_RECORD.
 *
 * The event carries USERSIM_OBJECT_TYPE_EVENT_HANDLE rather than
 * USERSIM_OBJECT_TYPE_EVENT so that the dispatcher routines can tell the two
 * apart without a lookup.
 */
typedef struct _usersim_handle_event
{
    KEVENT event;  ///< Must be first.
    HANDLE handle; ///< Win32 event object this event is bound to.
} usersim_handle_event_t;

/**
 * @brief Get the Win32 event object an event is bound to.
 *
 * @param[in] event Event to query.
 * @returns The bound handle, or nullptr if the event is a plain in-memory event.
 */
inline HANDLE
usersim_get_event_handle(_In_ const KEVENT* event)
{
    if (event->object_type != USERSIM_OBJECT_TYPE_EVENT_HANDLE) {
        return nullptr;
    }
    return CONTAINING_RECORD(event, usersim_handle_event_t, event)->handle;
}

CXPLAT_EXTERN_C_END
