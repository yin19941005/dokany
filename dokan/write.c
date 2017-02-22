/*
  Dokan : user-mode file system library for Windows

  Copyright (C) 2015 - 2017 Adrien J. <liryna.stark@gmail.com> and Maxime C. <maxime@islog.com>
  Copyright (C) 2007 - 2011 Hiroki Asakawa <info@dokan-dev.net>

  http://dokan-dev.github.io

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "dokani.h"

BOOL SendWriteRequest(_In_ HANDLE Handle, _In_ PEVENT_INFORMATION EventInfo,
                      _In_ ULONG EventLength, _In_ PVOID Buffer, _In_ ULONG BufferLength, 
                      _Out_ ULONG *ReturnedLengthOutPointer, _Out_ DWORD *LastError) {
  BOOL status = FALSE;
  ULONG returnedLength;

  DbgPrint("SendWriteRequest\n");

  status = DeviceIoControl(Handle,            // Handle to device
                           IOCTL_EVENT_WRITE, // IO Control code
                           EventInfo,         // Input Buffer to driver.
                           EventLength,     // Length of input buffer in bytes.
                           Buffer,          // Output Buffer from driver.
                           BufferLength,    // Length of output buffer in bytes.
                           &returnedLength, // Bytes placed in buffer.
                           NULL             // synchronous call
                           );

  DbgPrint("SendWriteRequest : status = %d, EventLength = %lu, BufferLength = %lu, returnedLength = %lu , EventInfo->SerialNumber = %X, EventInfo->Status = %X \n", status, EventLength, BufferLength, returnedLength, EventInfo->SerialNumber, EventInfo->Status);

  if (returnedLength == 0) {
	  DWORD errorCode = GetLastError();
	  DbgPrint("returnedLength == 0, After SendWriteRequest LastError : %lu \n", errorCode);
  }

  if (!status) {
    DWORD errorCode = GetLastError();
    DbgPrint("Ioctl failed with code %d\n", errorCode);
	*LastError = errorCode;
  }
  else {
	  *LastError = 0;
  }

  *ReturnedLengthOutPointer = returnedLength;

  DbgPrint("SendWriteRequest got %d bytes\n", returnedLength);

  return status;
}

VOID DispatchWrite(HANDLE Handle, PEVENT_CONTEXT EventContext,
                   PDOKAN_INSTANCE DokanInstance) {
  PEVENT_INFORMATION eventInfo;
  PDOKAN_OPEN_INFO openInfo;
  ULONG writtenLength = 0;
  NTSTATUS status;
  DOKAN_FILE_INFO fileInfo;
  BOOL bufferAllocated = FALSE;
  ULONG sizeOfEventInfo = sizeof(EVENT_INFORMATION);
  ULONG returnedLength = 0;
  BOOL SendWriteRequestStatus = TRUE;	// otherwise DokanInstance->DokanOperations->WriteFile cannot be called
  DWORD SendWriteRequestLastError = 0;

  eventInfo = DispatchCommon(EventContext, sizeOfEventInfo, DokanInstance,
                             &fileInfo, &openInfo);

  // Since driver requested bigger memory,
  // allocate enough memory and send it to driver
  if (EventContext->Operation.Write.RequestLength > 0) {
    ULONG contextLength = EventContext->Operation.Write.RequestLength;
    PEVENT_CONTEXT contextBuf = (PEVENT_CONTEXT)malloc(contextLength);
    if (contextBuf == NULL) {
      free(eventInfo);
      return;
    }
    
	DbgPrint("\tWriteFile : Before call SendWriteRequest, contextLength = %lu (contextLength = EventContext->Operation.Write.RequestLength)\n", contextLength);

	SendWriteRequestStatus = SendWriteRequest(Handle, eventInfo, sizeOfEventInfo, contextBuf,
                     contextLength, &returnedLength, &SendWriteRequestLastError);
    EventContext = contextBuf;
    bufferAllocated = TRUE;
  }

  if (EventContext == NULL) {
	  DbgPrint("\tWriteFile : EventContext == NULL (After EventContext = contextBuf). \n");
  }
  else {
	  DbgPrint("\tWriteFile : EventContext->SerialNumber = %X \n", EventContext->SerialNumber);
	  DbgPrint("\tWriteFile : EventContext->Operation.Write.RequestLength = %lu \n", EventContext->Operation.Write.RequestLength);
	  DbgPrint("\tWriteFile : EventContext = %lu \n", EventContext);
	  DbgPrint("\tWriteFile : EventContext + EventContext->Operation.Write.BufferOffset = %I64d \n", (PCHAR)EventContext + EventContext->Operation.Write.BufferOffset);
  }

  CheckFileName(EventContext->Operation.Write.FileName);

  DbgPrint("###WriteFile %04d\n", openInfo != NULL ? openInfo->EventId : -1);

  if (!SendWriteRequestStatus) {
	  if (SendWriteRequestLastError == ERROR_OPERATION_ABORTED) {
		  status = STATUS_CANCELLED;
		  DbgPrint("WriteFile Error : User should already canceled the operation. Return STATUS_CANCELLED. \n");
	  }
	  else {
		  status = DokanNtStatusFromWin32(SendWriteRequestLastError);
		  DbgPrint("Unknown SendWriteRequest Error : LastError from SendWriteRequest = %lu. \nUnknown SendWriteRequest error : EventContext had been destoryed. Status = %X. \n", SendWriteRequestLastError, status);
	  }
  }
  else {
	  // for the case SendWriteRequest success
	  if (DokanInstance->DokanOperations->WriteFile) {
		  status = DokanInstance->DokanOperations->WriteFile(
			  EventContext->Operation.Write.FileName,
			  (PCHAR)EventContext + EventContext->Operation.Write.BufferOffset,
			  EventContext->Operation.Write.BufferLength, &writtenLength,
			  EventContext->Operation.Write.ByteOffset.QuadPart, &fileInfo);
	  }
	  else {
		  status = STATUS_NOT_IMPLEMENTED;
	  }
  }

  if (openInfo != NULL)
    openInfo->UserContext = fileInfo.Context;
  eventInfo->Status = status;
  eventInfo->BufferLength = 0;

  if (status == STATUS_SUCCESS) {
    eventInfo->BufferLength = writtenLength;
    eventInfo->Operation.Write.CurrentByteOffset.QuadPart =
        EventContext->Operation.Write.ByteOffset.QuadPart + writtenLength;
	DbgPrint("\tWriteFile : eventInfo->BufferLength = %lu \n", eventInfo->BufferLength);
	DbgPrint("\tWriteFile : eventInfo->Operation.Write.CurrentByteOffset.QuadPart = %I64d \n", eventInfo->Operation.Write.CurrentByteOffset.QuadPart);
  }

  SendEventInformation(Handle, eventInfo, sizeOfEventInfo, DokanInstance);
  free(eventInfo);

  if (bufferAllocated) {
	  free(EventContext);
  }
  else {
	  DbgPrint("\tWriteFile : bufferAllocated is FALSE. \n");
  }
    
}
