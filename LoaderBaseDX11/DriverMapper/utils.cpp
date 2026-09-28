#include "include/utils.hpp"
#include <Windows.h>
#include <iostream>
#include <vector>
#include <fstream>

#include "include/nt.hpp"

// NtQuerySystemInformation — resolved dynamically to avoid the
// SYSTEM_INFORMATION_CLASS enum dependency (which winternl.h may omit when
// pulled in via Windows.h in certain SDK/PCH configurations).
namespace {
    typedef NTSTATUS (NTAPI* PfnNtQuerySysInfo)(ULONG, PVOID, ULONG, PULONG);
    static PfnNtQuerySysInfo s_NtQuerySystemInformation = nullptr;

    inline NTSTATUS CallNtQuerySystemInformation(ULONG cls, PVOID buf, ULONG len, PULONG ret) {
        if (!s_NtQuerySystemInformation) {
            s_NtQuerySystemInformation = reinterpret_cast<PfnNtQuerySysInfo>(
                ::GetProcAddress(::GetModuleHandleA("ntdll.dll"),
                                 "NtQuerySystemInformation"));
        }
        if (!s_NtQuerySystemInformation) return STATUS_NOT_IMPLEMENTED;
        return s_NtQuerySystemInformation(cls, buf, len, ret);
    }
}

std::wstring kdmUtils::GetFullTempPath() {
	wchar_t temp_directory[MAX_PATH + 1] = { 0 };
	const uint32_t get_temp_path_ret = GetTempPathW(sizeof(temp_directory) / 2, temp_directory);
	if (!get_temp_path_ret || get_temp_path_ret > MAX_PATH + 1) {
		kdmLog(L"[-] Failed to get temp path" << std::endl);
		return L"";
	}
	if (temp_directory[wcslen(temp_directory) - 1] == L'\\')
		temp_directory[wcslen(temp_directory) - 1] = 0x0;

	return std::wstring(temp_directory);
}

bool kdmUtils::ReadFileToMemory(const std::wstring& file_path, std::vector<BYTE>* out_buffer) {
	std::ifstream file_ifstream(file_path, std::ios::binary);

	if (!file_ifstream)
		return false;

	out_buffer->assign((std::istreambuf_iterator<char>(file_ifstream)), std::istreambuf_iterator<char>());
	file_ifstream.close();

	return true;
}

bool kdmUtils::CreateFileFromMemory(const std::wstring& desired_file_path, const char* address, size_t size) {
	std::ofstream file_ofstream(desired_file_path.c_str(), std::ios_base::out | std::ios_base::binary);

	if (!file_ofstream.write(address, size)) {
		file_ofstream.close();
		return false;
	}

	file_ofstream.close();
	return true;
}

uint64_t kdmUtils::GetKernelModuleAddress(const std::string& module_name) {
	void* buffer = nullptr;
	DWORD buffer_size = 0;

	NTSTATUS status = CallNtQuerySystemInformation(nt::SystemModuleInformation, buffer, buffer_size, &buffer_size);

	while (status == STATUS_INFO_LENGTH_MISMATCH) {
		if (buffer != nullptr)
			VirtualFree(buffer, 0, MEM_RELEASE);

		buffer = VirtualAlloc(nullptr, buffer_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		status = CallNtQuerySystemInformation(nt::SystemModuleInformation, buffer, buffer_size, &buffer_size);
	}

	if (!NT_SUCCESS(status)) {
		if (buffer != nullptr)
			VirtualFree(buffer, 0, MEM_RELEASE);
		return 0;
	}

	const auto modules = static_cast<nt::PRTL_PROCESS_MODULES>(buffer);
	if (!modules)
		return 0;

	for (auto i = 0u; i < modules->NumberOfModules; ++i) {
		const std::string current_module_name = std::string(reinterpret_cast<char*>(modules->Modules[i].FullPathName) + modules->Modules[i].OffsetToFileName);

		if (!_stricmp(current_module_name.c_str(), module_name.c_str()))
		{
			const uint64_t result = reinterpret_cast<uint64_t>(modules->Modules[i].ImageBase);

			VirtualFree(buffer, 0, MEM_RELEASE);
			return result;
		}
	}

	VirtualFree(buffer, 0, MEM_RELEASE);
	return 0;
}

bool kdmUtils::GetKernelModuleAddressAndSize(const std::string& module_name, uint64_t* outBase, uint32_t* outSize) {
	if (!outBase || !outSize) return false;
	*outBase = 0;
	*outSize = 0;

	void* buffer = nullptr;
	DWORD buffer_size = 0;

	NTSTATUS status = CallNtQuerySystemInformation(nt::SystemModuleInformation, buffer, buffer_size, &buffer_size);

	while (status == STATUS_INFO_LENGTH_MISMATCH) {
		if (buffer != nullptr)
			VirtualFree(buffer, 0, MEM_RELEASE);

		buffer = VirtualAlloc(nullptr, buffer_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		status = CallNtQuerySystemInformation(nt::SystemModuleInformation, buffer, buffer_size, &buffer_size);
	}

	if (!NT_SUCCESS(status)) {
		if (buffer != nullptr)
			VirtualFree(buffer, 0, MEM_RELEASE);
		return false;
	}

	const auto modules = static_cast<nt::PRTL_PROCESS_MODULES>(buffer);
	if (!modules) {
		VirtualFree(buffer, 0, MEM_RELEASE);
		return false;
	}

	for (auto i = 0u; i < modules->NumberOfModules; ++i) {
		const std::string current_module_name = std::string(reinterpret_cast<char*>(modules->Modules[i].FullPathName) + modules->Modules[i].OffsetToFileName);

		if (!_stricmp(current_module_name.c_str(), module_name.c_str()))
		{
			*outBase = reinterpret_cast<uint64_t>(modules->Modules[i].ImageBase);
			*outSize = modules->Modules[i].ImageSize;
			VirtualFree(buffer, 0, MEM_RELEASE);
			return true;
		}
	}

	VirtualFree(buffer, 0, MEM_RELEASE);
	return false;
}

BOOLEAN kdmUtils::bDataCompare(const BYTE* pData, const BYTE* bMask, const char* szMask) {
	for (; *szMask; ++szMask, ++pData, ++bMask)
		if (*szMask == 'x' && *pData != *bMask)
			return 0;
	return (*szMask) == 0;
}

uintptr_t kdmUtils::FindPattern(uintptr_t dwAddress, uintptr_t dwLen, BYTE* bMask, const char* szMask) {
	size_t max_len = dwLen - strlen(szMask);
	for (uintptr_t i = 0; i < max_len; i++)
		if (bDataCompare((BYTE*)(dwAddress + i), bMask, szMask))
			return (uintptr_t)(dwAddress + i);
	return 0;
}

PVOID kdmUtils::FindSection(const char* sectionName, uintptr_t modulePtr, PULONG size) {
	size_t namelength = strlen(sectionName);
	PIMAGE_NT_HEADERS headers = (PIMAGE_NT_HEADERS)(modulePtr + ((PIMAGE_DOS_HEADER)modulePtr)->e_lfanew);
	PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(headers);
	for (DWORD i = 0; i < headers->FileHeader.NumberOfSections; ++i) {
		PIMAGE_SECTION_HEADER section = &sections[i];
		if (memcmp(section->Name, sectionName, namelength) == 0 &&
			namelength == strlen((char*)section->Name)) {
			if (!section->VirtualAddress) {
				return 0;
			}
			if (size) {
				*size = section->Misc.VirtualSize;
			}
			return (PVOID)(modulePtr + section->VirtualAddress);
		}
	}
	return 0;
}

std::wstring kdmUtils::GetCurrentAppFolder() {
	wchar_t buffer[1024];
	GetModuleFileNameW(NULL, buffer, 1024);
	std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
	return std::wstring(buffer).substr(0, pos);
}