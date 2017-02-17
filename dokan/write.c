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

VOID SendWriteRequest(HANDLE Handle, PEVENT_INFORMATION EventInfo,
                      ULONG EventLength, PVOID Buffer, ULONG BufferLength) {
  BOOL status;
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

  DbgPrint("SendWriteRequest : status = %d, EventLength = %lu, BufferLength = %lu, returnedLength = %lu \n", status, EventLength, BufferLength, returnedLength);

  if (returnedLength == 0) {
	  DWORD errorCode = GetLastError();
	  DbgPrint("returnedLength == 0, After SendWriteRequest LastError : %lu \n", errorCode);
  }

  if (!status) {
    DWORD errorCode = GetLastError();
    DbgPrint("Ioctl failed with code %d\n", errorCode);
  }

  DbgPrint("SendWriteRequest got %d bytes\n", returnedLength);
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

	SendWriteRequest(Handle, eventInfo, sizeOfEventInfo, contextBuf,
                     contextLength);
    EventContext = contextBuf;
    bufferAllocated = TRUE;
  }

  if (EventContext == NULL) {
	  DbgPrint("\tWriteFile : EventContext == NULL (After EventContext = contextBuf). \n");
  }
  else {
	  DbgPrint("\tWriteFile : EventContext->SerialNumber = %lu \n", EventContext->SerialNumber);
	  DbgPrint("\tWriteFile : EventContext->Operation.Write.RequestLength = %lu \n", EventContext->Operation.Write.RequestLength);
	  DbgPrint("\tWriteFile : EventContext = %lu \n", EventContext);
	  DbgPrint("\tWriteFile : EventContext + EventContext->Operation.Write.BufferOffset = %I64d \n", (PCHAR)EventContext + EventContext->Operation.Write.BufferOffset);
  }

  CheckFileName(EventContext->Operation.Write.FileName);

  DbgPrint("###WriteFile %04d\n", openInfo != NULL ? openInfo->EventId : -1);

  if (DokanInstance->DokanOperations->WriteFile) {
    DbgPrint("\tWriteFile : Just Before Call to DokanInstance->DokanOperations->WriteFile Function. \n");
    status = DokanInstance->DokanOperations->WriteFile(
        EventContext->Operation.Write.FileName,
        (PCHAR)EventContext + EventContext->Operation.Write.BufferOffset,
        EventContext->Operation.Write.BufferLength, &writtenLength,
        EventContext->Operation.Write.ByteOffset.QuadPart, &fileInfo);
	DbgPrint("\tWriteFile : Just After Call to DokanInstance->DokanOperations->WriteFile Function. \n");
  } else {
    status = STATUS_NOT_IMPLEMENTED;
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
  
  DbgPrint("\tWriteFile : Just Before Calling SendEventInformation. \n");
  SendEventInformation(Handle, eventInfo, sizeOfEventInfo, DokanInstance);
  DbgPrint("\tWriteFile : Just After Calling SendEventInformation. \n");

  free(eventInfo);
  DbgPrint("\tWriteFile : Just After Calling free(eventInfo). \n");

  if (bufferAllocated) {
	  DbgPrint("\tWriteFile : Just Before Calling free(EventContext).. \n");
	  free(EventContext);
	  DbgPrint("\tWriteFile : Just After Calling free(EventContext). \n");
  }
  else {
	  DbgPrint("\tWriteFile : bufferAllocated is FALSE. \n");
  }
    
}
