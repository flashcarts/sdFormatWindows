// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#define _FILE_OFFSET_BITS 64
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>     // open()...
#include <sys/stat.h>  // S_IRUSR, S_IWUSR...
#include "types.h"
#include "blockdev.h"

#include <string>
#include <windows.h>
#include <winioctl.h>

//#define REDIRECT_FOR_DEBUG (1)


// TODO: implement rw
int BlockDev::open(const char *const path, const bool /* rw */) noexcept
{
	int res = 0;

	// Construct full path from a given drive letter path
	std::string fullPath = path;
	fullPath =  "\\\\.\\" + fullPath;

	// Get the physical drive number that this drive letter resides in
	HANDLE handle = CreateFile(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
	STORAGE_DEVICE_NUMBER number;
	DWORD bytesReturned = 0;
	DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number), &bytesReturned, nullptr);
	DWORD physicalDriveNumber = number.DeviceNumber;
	// ...and construct full path to it
	pDrvPath = "\\\\.\\PhysicalDrive" + std::to_string(physicalDriveNumber);

	// Close the handle for volume. We will reopen with the physical drive.
	// Wait for it to complete.
	while(CloseHandle(handle) == 0);

	do
	{
		// Switch handle to the physical drive where we will perform operations
		handle = CreateFile(pDrvPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);

		// Get disk size
		DISK_GEOMETRY_EX diskGeometryEx;
		u64 diskSize;
		if(!DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &diskGeometryEx, sizeof(diskGeometryEx), nullptr, nullptr))
		{
			printf("Error retrieving disk size\n");
			res = GetLastError();
			break;
		}
		diskSize = (u64)diskGeometryEx.DiskSize.QuadPart;

		// Lock volume. Will return error 5 == access denied without it.
		if (!DeviceIoControl (handle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL))
		{
			printf("Error locking volume\n");
			res = GetLastError();
			break;
		}

		// Dismount volume. Will return error 5 == access denied without it.
		if (!DeviceIoControl (handle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL))
		{
			printf("Error dismounting volume\n");
			res = GetLastError();
			break;
		}

		m_handle = handle;
		m_sectors = diskSize / m_sectorSize;
	} while(0);

	if(res != 0)
	{
		printf("Failed to open block device, GetLastError(): %d\n", res);
		if(handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
			m_handle = INVALID_HANDLE_VALUE;
		}
	}
	return res;
}

int BlockDev::read(void *buf, const u64 sector, const u64 count) const noexcept
{
	int res = 0;
	DWORD offset = sector * m_sectorSize;
	u64 totSize = count * m_sectorSize;
	u8 *_buf = reinterpret_cast<u8*>(buf);
	while(totSize > 0)
	{
		DWORD _read = 0;
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		SetFilePointer(m_handle, offset, nullptr, FILE_BEGIN);
		if(!ReadFile(m_handle, _buf, blkSize, &_read, nullptr))
		{
			res = GetLastError();
			break;
		}

		_buf += _read;
		offset += _read;
		totSize -= _read;
	}

	if(res == 0)
		printf("Failed to read from block device, GetLastError() == %d\n", res);
	return res;
}

int BlockDev::write(const void *buf, const u64 sector, const u64 count) noexcept
{
	// Mark as dirty since we are about to write data.
	m_dirty = true;

	int res = 0;
	DWORD offset = sector * m_sectorSize;
	u64 totSize = count * m_sectorSize;
	const u8 *_buf = reinterpret_cast<const u8*>(buf);
	while(totSize > 0)
	{
		DWORD written = 0;
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		SetFilePointer(m_handle, offset, nullptr, FILE_BEGIN);
		if(!WriteFile(m_handle, _buf, blkSize, &written, nullptr))
		{
			res = GetLastError();
			break;
		}

		_buf += written;
		offset += written;
		totSize -= written;
	}

	if(res != 0)
		printf("Failed to write to block device, GetLastError() == %d\n", res);
	return res;
}

/* FCNET CHANGE START - use eraseAll to clear drive partitions */
// We ignore the secure erase option.
// Hijack this function, which will use Win32 API to "clean" the drive of partition tables.
// On Windows this must be done before we can write arbitrary MBR.
// We also have to close and reopen the handle during this process, so we unconstify this function.
int BlockDev::eraseAll(const bool /* secure */) /* const */ noexcept
{
	int res = 0;
	DWORD bytesReturned = 0;
	CREATE_DISK diskStruct = {};
	diskStruct.PartitionStyle = PARTITION_STYLE_MBR;

	if(!DeviceIoControl(m_handle, IOCTL_DISK_CREATE_DISK, &diskStruct, sizeof(CREATE_DISK), NULL, 0, &bytesReturned, NULL))
	{
		res = GetLastError();
		printf("Failed to discard all data on device, GetLastError() = %d\n", res);
		return res;
	}

	// we must reopen drive for Windows to detect that the partition table is gone
	// Unlock volume.
	if (!DeviceIoControl(m_handle, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
		res = GetLastError();
		printf("Error unlocking volume, GetLastError() == %d\n", res);
		return res;
	}

	while(CloseHandle(m_handle) == 0);

	// reopen the handle. Now Windows should let us do anything
	m_handle = CreateFile(pDrvPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);

	return res;
}
/* FCNET CHANGE END - use eraseAll to clear drive partitions */

// TODO: Should we return any error that is not EINTR?
void BlockDev::close(void) noexcept
{
	if(m_dirty)
	{
		// Flush all writes to the device.
		FlushFileBuffers(m_handle);
	}

	// Close the file descriptor.
	while(CloseHandle(m_handle) == 0);
	m_handle = INVALID_HANDLE_VALUE;

	m_dirty = false;
	m_sectors = 0;
}
