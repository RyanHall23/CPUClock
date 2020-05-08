#include <iostream>
#include <sstream>
#include "string.h"
#include <atlstr.h>
#include <tchar.h>
#include <windows.h>
#include <stdio.h>
#include <cstdio>
#include <vector>

extern "C" {
#include <Powrprof.h>
}

#pragma comment(lib, "Powrprof.lib")

typedef struct _PROCESSOR_POWER_INFORMATION {
    ULONG  Number;
    ULONG  MaxMhz;
    ULONG  CurrentMhz;
    ULONG  MhzLimit;
    ULONG  MaxIdleState;
    ULONG  CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, * PPROCESSOR_POWER_INFORMATION;


typedef BOOL(WINAPI* LPFN_GLPI)(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
    PDWORD);

std::string GetRegCpu()
{
	std::ostringstream stream;
	CString sMHz;
	DWORD BufSize = _MAX_PATH;
	DWORD dwMHz = _MAX_PATH;
	HKEY hKey;
	std::string errorMessage;

	// open the key where the proc speed is hidden:
	long lError = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		_T("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
		0,
		KEY_READ,
		&hKey);

	if (lError != ERROR_SUCCESS)
	{ // if the key is not found, report

		return errorMessage;
	}
	// query the key;
	RegQueryValueEx(hKey, _T("~MHz"), NULL, NULL, (LPBYTE)&dwMHz, &BufSize);

	// convert the DWORD to a string:
	stream << dwMHz << " mhz";
	std::string cpuSpeed = stream.str();

	return cpuSpeed;
}

DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
    DWORD i;

    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest) ? 1 : 0);
        bitTest /= 2;
    }

    return bitSetCount;
}

size_t cache_line_size() {
    size_t line_size = 0;
    DWORD buffer_size = 0;
    DWORD i = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = 0;

    GetLogicalProcessorInformation(0, &buffer_size);
    buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(buffer_size);
    GetLogicalProcessorInformation(&buffer[0], &buffer_size);

    for (i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
        if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
            line_size = buffer[i].Cache.LineSize;
            break;
        }
    }

    free(buffer);
    return line_size;
}


extern "C" {
#include <powrprof.h>
}
int _cdecl _tmain()
{
    LPFN_GLPI glpi;
    BOOL done = FALSE;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
    DWORD returnLength = 0;
    DWORD logicalProcessorCount = 0;
    DWORD numaNodeCount = 0;
    DWORD processorCoreCount = 0;
    DWORD processorL1CacheCount = 0;
    DWORD processorL2CacheCount = 0;
    DWORD processorL3CacheCount = 0;
    DWORD processorPackageCount = 0;
    DWORD byteOffset = 0;
    PCACHE_DESCRIPTOR Cache;

    glpi = (LPFN_GLPI)GetProcAddress(
        GetModuleHandle(TEXT("kernel32")),
        "GetLogicalProcessorInformation");
    if (NULL == glpi)
    {
        _tprintf(TEXT("\nGetLogicalProcessorInformation is not supported.\n"));
        return (1);
    }

    while (!done)
    {
        DWORD rc = glpi(buffer, &returnLength);

        if (FALSE == rc)
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                if (buffer)
                    free(buffer);

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
                    returnLength);

                if (NULL == buffer)
                {
                    _tprintf(TEXT("\nError: Allocation failure\n"));
                    return (2);
                }
            }
            else
            {
                _tprintf(TEXT("\nError %d\n"), GetLastError());
                return (3);
            }
        }
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
    {
        switch (ptr->Relationship)
        {
        case RelationNumaNode:
            // Non-NUMA systems report a single record of this type.
            numaNodeCount++;
            break;

        case RelationProcessorCore:
            processorCoreCount++;

            // A hyperthreaded core supplies more than one logical processor.
            logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
            break;

        case RelationCache:
            // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
            Cache = &ptr->Cache;
            if (Cache->Level == 1)
            {
                processorL1CacheCount++;
            }
            else if (Cache->Level == 2)
            {
                processorL2CacheCount++;
            }
            else if (Cache->Level == 3)
            {
                processorL3CacheCount++;
            }
            break;

        case RelationProcessorPackage:
            // Logical processors share a physical package.
            processorPackageCount++;
            break;

        default:
            _tprintf(TEXT("\nError: Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n"));
            break;
        }
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

    _tprintf(TEXT("\nGetLogicalProcessorInformation results:\n"));
    _tprintf(TEXT("Number of NUMA nodes: %d\n"),
        numaNodeCount);
    _tprintf(TEXT("Number of physical processor packages: %d\n"),
        processorPackageCount);
    _tprintf(TEXT("Number of processor cores: %d\n"),
        processorCoreCount);
    _tprintf(TEXT("Number of logical processors: %d\n"),
        logicalProcessorCount);
    _tprintf(TEXT("Number of processor L1/L2/L3 caches: %d/%d/%d\n"),
        processorL1CacheCount,
        processorL2CacheCount,
        processorL3CacheCount);

    free(buffer);

    std::cout << "Processor base clock : "<< GetRegCpu() << std::endl;

    ////////////////////////////////
    SYSTEM_INFO si = { 0 };
    GetSystemInfo(&si);

    std::vector<PROCESSOR_POWER_INFORMATION> a(si.dwNumberOfProcessors);
    DWORD dwSize = sizeof(PROCESSOR_POWER_INFORMATION) * si.dwNumberOfProcessors;

    CallNtPowerInformation(ProcessorInformation, NULL, 0, &a[0], dwSize);

    _tprintf(TEXT("Max Mhz: %d\n"),
        a.at(0).MaxMhz);
    _tprintf(TEXT("Mhz Limit: %d\n"),
        a.at(0).MhzLimit);
    _tprintf(TEXT("Current Mhz: %d\n"),
        a.at(0).CurrentMhz);
    _tprintf(TEXT("Max Idle State: %d\n"),
        a.at(0).MaxIdleState);
    _tprintf(TEXT("Current Idle State: %d\n"),
        a.at(0).CurrentIdleState);


    a.clear();
    return 0;
}