/*
 * HIDAPI-compatible interface using hid.dll.
 *
 * Copyright (C) 2012 David Pribyl
 *
 * This file is part of PIC32PROG project, which is distributed
 * under the terms of the GNU General Public License (GPL).
 * See the accompanying file "COPYING" for more details.
 */
#include <wtypes.h>
#include <basetyps.h>
#include <setupapi.h>

#ifdef __CYGWIN__
#include <w32api/hidsdi.h>
/* the definition below is missing from Cygwin' hidsdi.h ... */
HIDAPI VOID NTAPI HidD_GetHidGuid (LPGUID);
#else
#include <ddk/hidsdi.h>
#endif

#include "hidapi.h"

/*
 * Use a series of API calls to find a HID with
 * a specified Vendor IF and Product ID.
 */
HID_API_EXPORT HID_API_CALL
hid_device *hid_open (
    unsigned short vendor_id,
    unsigned short product_id,
    const wchar_t *serial_number)
{
    GUID                        hidguid;
    HANDLE                      hidclass;
    SP_DEVICE_INTERFACE_DATA    iface;
    HANDLE                      dev;
    int                         i;

    HidD_GetHidGuid (&hidguid);

    hidclass = SetupDiGetClassDevs (&hidguid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);

    iface.cbSize = sizeof(iface);

    i = 0;
    dev = NULL;
    while (SetupDiEnumDeviceInterfaces (hidclass, 0, &hidguid,
                                        i, &iface))
    {
        ULONG unused, nbytes = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA detail;
        HIDD_ATTRIBUTES devinfo;

        SetupDiGetDeviceInterfaceDetail (hidclass, &iface,
            NULL, 0, &nbytes, NULL);

        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA) malloc (nbytes);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        SetupDiGetDeviceInterfaceDetail (hidclass, &iface,
            detail, nbytes, &unused, NULL);

        dev = CreateFile (detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            (LPSECURITY_ATTRIBUTES) NULL,
            OPEN_EXISTING, 0, NULL);
        free (detail);

        devinfo.Size = sizeof(devinfo);
        HidD_GetAttributes (dev, &devinfo);

        if (devinfo.VendorID == vendor_id && devinfo.ProductID == product_id)
        {
            // Both the Vendor ID and Product ID match.
            break;
        }

        // The Vendor ID doesn't match.
        // Free the memory used by the detailData structure (no longer needed).
        CloseHandle (dev);
        dev = NULL;

        // If we haven't found the device yet, and haven't tried every
        // available device, try the next one.
        i++;
    }

    // Free the memory reserved for hidclass by SetupDiClassDevs.
    SetupDiDestroyDeviceInfoList (hidclass);
    return (hid_device*) dev;
}

HID_API_EXPORT HID_API_CALL
int hid_write (hid_device *device, const unsigned char *data, size_t nbytes)
{
    HANDLE dev = (HANDLE) device;
    int ret;
    unsigned long bytes_written = 0;
    BYTE buf [256];

    if (! dev || nbytes >= 256)
        return -1;
    buf[0] = 0;
    memcpy (buf + 1, data, nbytes);

    ret = WriteFile (dev, buf, nbytes + 1, &bytes_written, NULL);
    return ret;
}

HID_API_EXPORT HID_API_CALL
int hid_read (hid_device *device, unsigned char *data, size_t nbytes)
{
    HANDLE dev = (HANDLE) device;
    int ret;
    unsigned long bytes_read = 0;
    BYTE buf [256];

    if (! dev || nbytes >= 256)
        return -1;

    ret = ReadFile (dev, buf, nbytes + 1, &bytes_read, NULL);
    if (ret <= 0)
        return ret;

    memcpy (data, buf + 1, bytes_read - 1);
    return bytes_read - 1;
}

HID_API_EXPORT HID_API_CALL
int hid_read_timeout (hid_device *device, unsigned char *data, size_t nbytes, int milliseconds)
{
    HANDLE dev = (HANDLE) device;
    unsigned long bytes_read = 0;
    BYTE buf [256];
    OVERLAPPED ol;
    int ret;

    // Start an Overlapped I/O read.
    ResetEvent (ol.hEvent);
    ret = ReadFile (dev, buf, nbytes + 1, &bytes_read, &ol);
    if (! ret && GetLastError() != ERROR_IO_PENDING) {
        // ReadFile failed.
        CancelIo (dev);
        return -1;
    }

    // See if there is any data yet.
    ret = WaitForSingleObject (ol.hEvent, milliseconds);
    if (ret != WAIT_OBJECT_0) {
        // Timeout expired.
        CancelIo (dev);
        return -1;
    }

    // Get the number of bytes read. The actual data has been copied
    // to the data[] array which was passed to ReadFile().
    ret = GetOverlappedResult (dev, &ol, &bytes_read, 1 /*wait*/);
    if (! ret)
        return -1;

    memcpy (data, buf + 1, bytes_read - 1);
    return bytes_read - 1;
}
