#include <windows.h>
#include <stdio.h>

#define IOCTL_BENCHMARK_IRQL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _BENCHMARK_RESULTS {
    ULONGLONG ReadUs;
    ULONGLONG WriteUs;
    ULONGLONG InterlockedIncrementUs;
    ULONGLONG InterlockedIncrementNoFenceUs;
} BENCHMARK_RESULTS, * PBENCHMARK_RESULTS;

int main() {
    HANDLE hDevice;
    BENCHMARK_RESULTS results = { 0 };
    DWORD bytesReturned;

    // Open device
    hDevice = CreateFileW(L"\\\\.\\irqlbench", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device: %d\n", GetLastError());
        return 1;
    }

    // Send IOCTL to trigger benchmark
    if (!DeviceIoControl(hDevice, IOCTL_BENCHMARK_IRQL, NULL, 0, &results, sizeof(results), &bytesReturned, NULL)) {
        printf("DeviceIoControl failed: %d\n", GetLastError());
        CloseHandle(hDevice);
        return 1;
    }

    // Print results
    printf("IRQL Read (KeGetCurrentIrql): %llu us\n", results.ReadUs);
    printf("IRQL Write (KeRaiseIrql/KeLowerIrql): %llu us\n", results.WriteUs);
    printf("InterlockedIncrement: %llu us\n", results.InterlockedIncrementUs);
    printf("InterlockedIncrementNoFence: %llu us\n", results.InterlockedIncrementNoFenceUs);

    CloseHandle(hDevice);
    return 0;
}