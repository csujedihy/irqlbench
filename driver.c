#include <ntddk.h>
#include <wdm.h>

// IOCTL code for benchmarking
#define IOCTL_BENCHMARK_IRQL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Structure to hold benchmark results

typedef struct _BENCHMARK_RESULTS {
    ULONGLONG ReadUs;
    ULONGLONG WriteUs;
    ULONGLONG InterlockedIncrementUs;
    ULONGLONG InterlockedIncrementNoFenceUs;
} BENCHMARK_RESULTS, * PBENCHMARK_RESULTS;

// Number of iterations for benchmarking
#define NUM_ITERATIONS 1000000

// Device and symbolic link names
#define DEVICE_NAME L"\\Device\\irqlbench"
#define SYMLINK_NAME L"\\DosDevices\\irqlbench"

#define CONVERT_100NS_TO_US(x) ((x) / 10)

// Benchmark function
void BenchmarkIrql(PBENCHMARK_RESULTS results) {
    ULONGLONG start, end;
    KIRQL originalIrql;
    volatile LONG Counter = 0;
    volatile LONG CounterNoFence = 0;
    ULONG64 Ununsed;

    // Disable interrupts to reduce noise
    _disable();

    // Benchmark read (KeGetCurrentIrql)
    start = KeQueryInterruptTimePrecise(&Ununsed);
    for (ULONG i = 0; i < NUM_ITERATIONS; i++) {
        KeGetCurrentIrql();
    }
    end = KeQueryInterruptTimePrecise(&Ununsed);
    results->ReadUs = CONVERT_100NS_TO_US(end - start);

    // Benchmark write (KeRaiseIrql/KeLowerIrql)
    KIRQL originalIrqlParent;
    KeRaiseIrql(DISPATCH_LEVEL, &originalIrqlParent);

    start = KeQueryInterruptTimePrecise(&Ununsed);
    for (ULONG i = 0; i < NUM_ITERATIONS; i++) {
        KeRaiseIrql(DISPATCH_LEVEL, &originalIrql);
        KeLowerIrql(originalIrql);
    }
    end = KeQueryInterruptTimePrecise(&Ununsed);
    results->WriteUs = CONVERT_100NS_TO_US(end - start);
    KeLowerIrql(originalIrqlParent);

    // Benchmark interlocked increment
    start = KeQueryInterruptTimePrecise(&Ununsed);
    for (ULONG i = 0; i < NUM_ITERATIONS; i++) {
        InterlockedIncrement(&Counter);
    }
    end = KeQueryInterruptTimePrecise(&Ununsed);
    results->InterlockedIncrementUs = CONVERT_100NS_TO_US(end - start);

    // Benchmark interlocked increment without fence
    start = KeQueryInterruptTimePrecise(&Ununsed);
    for (ULONG i = 0; i < NUM_ITERATIONS; i++) {
        InterlockedIncrementNoFence(&CounterNoFence);
    }
    end = KeQueryInterruptTimePrecise(&Ununsed);
    results->InterlockedIncrementNoFenceUs = CONVERT_100NS_TO_US(end - start);

    // Re-enable interrupts
    _enable();
}

// Driver unload routine
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symlinkName;
    PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;

    // Delete symbolic link
    RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlinkName);

    // Delete device object
    if (deviceObject) {
        IoDeleteDevice(deviceObject);
    }

    DbgPrint("irqlbench: Driver unloaded\n");
}

// Device control routine
NTSTATUS DeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytesReturned = 0;
    UNREFERENCED_PARAMETER(DeviceObject);
    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_BENCHMARK_IRQL:
    {
        if (irpSp->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(BENCHMARK_RESULTS)) {
            PBENCHMARK_RESULTS results = (PBENCHMARK_RESULTS)Irp->AssociatedIrp.SystemBuffer;
            BenchmarkIrql(results);
            bytesReturned = sizeof(BENCHMARK_RESULTS);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS CreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    DbgPrint("irqlbench: CreateClose called\n");
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// Driver entry point
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    UNICODE_STRING deviceName, symlinkName;
    PDEVICE_OBJECT deviceObject = NULL;

    UNREFERENCED_PARAMETER(RegistryPath);

    // Initialize device and symbolic link names
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);

    // Create device object
    status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint("irqlbench: Failed to create device (0x%08X)\n", status);
        return status;
    }

    // Create symbolic link
    status = IoCreateSymbolicLink(&symlinkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        DbgPrint("irqlbench: Failed to create symbolic link (0x%08X)\n", status);
        return status;
    }

    // Set up driver routines
    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;

    DbgPrint("irqlbench: Driver loaded successfully\n");
    return STATUS_SUCCESS;
}