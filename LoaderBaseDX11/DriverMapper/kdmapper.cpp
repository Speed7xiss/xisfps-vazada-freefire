#include "include/kdmapper.hpp"
#include <Windows.h>
#include <iostream>
#include <stdio.h>
#include <stdarg.h>

#include "include/utils.hpp"
#include "include/intel_driver.hpp"
#include "include/nt.hpp"
#include "include/portable_executable.hpp"
#include "include/llvm.h"
#include "include/xorstr.hpp"

static void LogKdmHollow(const char* /*fmt*/, ...) {
}

static bool HollowWriteChunked(uint64_t target_va, void* buffer, uint32_t size) {
	const uint32_t CHUNK = 0x1000;
	uint8_t* src = (uint8_t*)buffer;
	uint32_t offset = 0;
	uint32_t failed_chunks = 0;
	uint32_t total_chunks = 0;

	while (offset < size) {
		uint32_t this_chunk = (size - offset < CHUNK) ? (size - offset) : CHUNK;
		total_chunks++;
		if (!intel_driver::WriteToReadOnlyMemory(target_va + offset, src + offset, this_chunk)) {
			LogKdmHollow("[ERRO] Chunk %u falhou em VA=0x%llx size=0x%X\n",
				total_chunks, target_va + offset, this_chunk);
			failed_chunks++;
		}
		offset += this_chunk;
	}

	LogKdmHollow("[INFO] HollowWriteChunked: %u chunks total, %u falhas\n",
		total_chunks, failed_chunks);
	return failed_chunks == 0;
}

void RelocateImageByDelta(portable_executable::vec_relocs relocs, const ULONG64 delta) {
	for (const auto& current_reloc : relocs) {
		for (auto i = 0u; i < current_reloc.count; ++i) {
			const uint16_t type = current_reloc.item[i] >> 12;
			const uint16_t offset = current_reloc.item[i] & 0xFFF;

			if (type == IMAGE_REL_BASED_DIR64)
				*reinterpret_cast<ULONG64*>(current_reloc.address + offset) += delta;
		}
	}
}

// Fix cookie by @Jerem584
bool FixSecurityCookie(void* local_image, ULONG64 kernel_image_base)
{
	auto headers = portable_executable::GetNtHeaders(local_image);
	if (!headers)
		return false;

	auto load_config_directory = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
	if (!load_config_directory)
	{
		kdmLog(L"[+] Load config directory wasn't found, probably StackCookie not defined, fix cookie skipped" << std::endl);
		return true;
	}

	auto load_config_struct = (PIMAGE_LOAD_CONFIG_DIRECTORY)((uintptr_t)local_image + load_config_directory);
	auto stack_cookie = load_config_struct->SecurityCookie;
	if (!stack_cookie)
	{
		kdmLog(L"[+] StackCookie not defined, fix cookie skipped" << std::endl);
		return true; // as I said, it is not an error and we should allow that behavior
	}

	stack_cookie = stack_cookie - (uintptr_t)kernel_image_base + (uintptr_t)local_image; //since our local image is already relocated the base returned will be kernel address

	if (*(uintptr_t*)(stack_cookie) != 0x2B992DDFA232) {
		kdmLog(L"[-] StackCookie already fixed!? this probably wrong" << std::endl);
		return false;
	}

	kdmLog(L"[+] Fixing stack cookie" << std::endl);

	auto new_cookie = 0x2B992DDFA232 ^ GetCurrentProcessId() ^ GetCurrentThreadId(); // here we don't really care about the value of stack cookie, it will still works and produce nice result
	if (new_cookie == 0x2B992DDFA232)
		new_cookie = 0x2B992DDFA233;

	*(uintptr_t*)(stack_cookie) = new_cookie; // the _security_cookie_complement will be init by the driver itself if they use crt
	return true;
}

bool ResolveImports(portable_executable::vec_imports imports) {
	for (const auto& current_import : imports) {
		ULONG64 Module = kdmUtils::GetKernelModuleAddress(current_import.module_name);
		if (!Module) {
#if !defined(DISABLE_OUTPUT)
			std::cout << "[-] Dependency " << current_import.module_name << " wasn't found" << std::endl;
#endif
			return false;
		}

		for (auto& current_function_data : current_import.function_datas) {
			ULONG64 function_address = intel_driver::GetKernelModuleExport(Module, current_function_data.name);

			if (!function_address) {
				//Lets try with ntoskrnl
				if (Module != intel_driver::ntoskrnlAddr) {
					function_address = intel_driver::GetKernelModuleExport(intel_driver::ntoskrnlAddr, current_function_data.name);
					if (!function_address) {
#if !defined(DISABLE_OUTPUT)
						std::cout << "[-] Failed to resolve import " << current_function_data.name << " (" << current_import.module_name << ")" << std::endl;
#endif
						return false;
					}
				}
			}

			*current_function_data.address = function_address;
		}
	}

	return true;
}

ULONG64 kdmapper::MapDriver(BYTE* data, ULONG64 param1, ULONG64 param2, bool free, bool destroyHeader, AllocationMode mode, bool PassAllocationAddressAsFirstParam, mapCallback callback, NTSTATUS* exitCode, bool hollow_mode, const char* hollow_target) {

	// Em hollow_mode FORÇA destroyHeader=false pra alinhar nossas secoes com
	// as do driver alvo (.text @ 0x1000 RX, .data @ Xdata RW, etc). As page
	// protections do PE original se sobrepoe naturalmente sem precisar de
	// MmSetPageProtection (que falha em paginas de driver ja carregado).
	if (hollow_mode) destroyHeader = false;

	if (hollow_mode) {
		LogKdmHollow("\n========================================================\n");
		LogKdmHollow("=== MapDriver HOLLOW MODE iniciado (PID=%lu) ===\n", GetCurrentProcessId());
		LogKdmHollow("========================================================\n");
	}

	const PIMAGE_NT_HEADERS64 nt_headers = portable_executable::GetNtHeaders(data);

	if (!nt_headers) {
		kdmLog(L"[-] Invalid format of PE image" << std::endl);
		return 0;
	}

	if (nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
		kdmLog(L"[-] Image is not 64 bit" << std::endl);
		return 0;
	}

	ULONG32 image_size = nt_headers->OptionalHeader.SizeOfImage;

	void* local_image_base = VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!local_image_base)
		return 0;

	DWORD TotalVirtualHeaderSize = (IMAGE_FIRST_SECTION(nt_headers))->VirtualAddress;
	image_size = image_size - (destroyHeader ? TotalVirtualHeaderSize : 0);

	ULONG64 kernel_image_base = 0;
	if (hollow_mode) {
		uint64_t target_base = 0;
		uint32_t target_size = 0;
		const std::string target_name = (hollow_target && *hollow_target)
			? std::string(hollow_target)
			: std::string( _("cdrom.sys") );

		LogKdmHollow("[INFO] LogsBypass image_size (apos destroyHeader=%d): 0x%X (%u bytes)\n",
			destroyHeader, image_size, image_size);
		LogKdmHollow("[INFO] Procurando target driver: %s\n", target_name.c_str());

		if (!kdmUtils::GetKernelModuleAddressAndSize(target_name, &target_base, &target_size)) {
			LogKdmHollow("[ERRO] %s NAO encontrado em PsLoadedModuleList!\n", target_name.c_str());
			LogKdmHollow("       Provavelmente o servico PEAUTH nao esta rodando.\n");
			LogKdmHollow("       Tente: sc start PEAUTH (ou rodar conteudo DRM antes)\n");
			kdmLog(L"[-] HOLLOW: target driver not loaded." << std::endl);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		LogKdmHollow("[OK]   %s encontrado:\n", target_name.c_str());
		LogKdmHollow("       DllBase     = 0x%llx\n", target_base);
		LogKdmHollow("       SizeOfImage = 0x%X (%u bytes)\n", target_size, target_size);

		if (target_size < image_size) {
			LogKdmHollow("[ERRO] %s muito pequeno: %u bytes < %u bytes (LogsBypass)\n",
				target_name.c_str(), target_size, image_size);
			kdmLog(L"[-] HOLLOW: target driver too small for our image" << std::endl);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		LogKdmHollow("[OK]   Tamanho ok: %u bytes >= %u bytes (margem: %u bytes)\n",
			target_size, image_size, target_size - image_size);

		kernel_image_base = target_base;
		kdmLog(L"[+] HOLLOW MODE: target @ 0x" << reinterpret_cast<void*>(kernel_image_base) << L" (" << target_name.c_str() << L")" << std::endl);
	}
	else if (mode == AllocationMode::AllocateIndependentPages)
	{
		kernel_image_base = intel_driver::MmAllocateIndependentPagesEx(image_size);
	}
	else { // AllocatePool by default
		kernel_image_base = intel_driver::AllocatePool(nt::POOL_TYPE::NonPagedPool, image_size);
	}

	if (!kernel_image_base) {
		kdmLog(L"[-] Failed to allocate remote image in kernel" << std::endl);
		if (hollow_mode) LogKdmHollow("[ERRO] kernel_image_base=NULL apos hollow setup\n");

		VirtualFree(local_image_base, 0, MEM_RELEASE);
		return 0;
	}

	do {
		kdmLog(L"[+] Image base has been allocated at 0x" << reinterpret_cast<void*>(kernel_image_base) << std::endl);

		// Copy image headers

		memcpy(local_image_base, data, nt_headers->OptionalHeader.SizeOfHeaders);

		// Copy image sections

		const PIMAGE_SECTION_HEADER current_image_section = IMAGE_FIRST_SECTION(nt_headers);

		for (auto i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
			if ((current_image_section[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) > 0)
				continue;
			auto local_section = reinterpret_cast<void*>(reinterpret_cast<ULONG64>(local_image_base) + current_image_section[i].VirtualAddress);
			memcpy(local_section, reinterpret_cast<void*>(reinterpret_cast<ULONG64>(data) + current_image_section[i].PointerToRawData), current_image_section[i].SizeOfRawData);
		}

		ULONG64 realBase = kernel_image_base;
		if (destroyHeader) {
			kernel_image_base -= TotalVirtualHeaderSize;
			kdmLog(L"[+] Skipped 0x" << std::hex << TotalVirtualHeaderSize << L" bytes of PE Header" << std::endl);
		}

		// Resolve relocs and imports

		RelocateImageByDelta(portable_executable::GetRelocs(local_image_base), kernel_image_base - nt_headers->OptionalHeader.ImageBase);

		if (!FixSecurityCookie(local_image_base, kernel_image_base ))
		{
			kdmLog(L"[-] Failed to fix cookie" << std::endl);
			return 0;
		}

		if (!ResolveImports(portable_executable::GetImports(local_image_base))) {
			kdmLog(L"[-] Failed to resolve imports" << std::endl);
			kernel_image_base = realBase;
			break;
		}

		// Write fixed image to kernel

		PVOID source_buffer = (PVOID)((uintptr_t)local_image_base + (destroyHeader ? TotalVirtualHeaderSize : 0));

		if (hollow_mode) {
			// PRE-WRITE: aplica RWX em TODA a regiao do clfs ANTES de escrever.
			// Fecha race window: se NTFS chamar callback do clfs durante o write,
			// nao crasha mais por NX (.rdata) ou RO (.text). Toda a regiao ja
			// ta RWX antes de qualquer modificacao.
			LogKdmHollow("\n[INFO] Pre-write RWX em toda a regiao: 0x%llx size=0x%X\n",
				realBase, image_size);
			intel_driver::MmSetPageProtection((uint64_t)realBase, image_size, PAGE_EXECUTE_READWRITE);

			LogKdmHollow("[INFO] Iniciando HollowWriteChunked: target=0x%llx size=0x%X\n",
				realBase, image_size);
			if (!HollowWriteChunked(realBase, source_buffer, image_size)) {
				LogKdmHollow("[ERRO] HollowWriteChunked falhou parcialmente — abortando\n");
				kdmLog(L"[-] HOLLOW: failed to write image to cdrom.sys" << std::endl);
				kernel_image_base = realBase;
				break;
			}
			LogKdmHollow("[OK]   Imagem escrita em cdrom.sys com sucesso\n");
		}
		else {
			if (!intel_driver::WriteMemory(realBase, source_buffer, image_size)) {
				kdmLog(L"[-] Failed to write local image to remote image" << std::endl);
				kernel_image_base = realBase;
				break;
			}
		}

		if (hollow_mode || mode == AllocationMode::AllocateIndependentPages)
		{
			auto ProtectionToString = [](ULONG prot) -> const char* {
				switch (prot)
				{
				case PAGE_NOACCESS: return "NOACCESS";
				case PAGE_READONLY: return "READONLY";
				case PAGE_READWRITE: return "READWRITE";
				case PAGE_EXECUTE: return "EXECUTE";
				case PAGE_EXECUTE_READ: return "EXECUTE_READ";
				case PAGE_EXECUTE_READWRITE: return "EXECUTE_READWRITE";
				default: return "UNKNOWN";
				}
				};

			if (hollow_mode) {
				LogKdmHollow("\n[INFO] === Aplicando proteccoes por section ===\n");
			}

			for (int i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
				auto sec = &IMAGE_FIRST_SECTION(nt_headers)[i];
				uintptr_t secAddr = kernel_image_base + sec->VirtualAddress;
				uint32_t secSize = sec->Misc.VirtualSize;

				if (secSize <= 0) {
					kdmLog(L"[*] Skipping empty section: " << (char*)sec->Name << std::endl);
					if (hollow_mode) LogKdmHollow("[SKIP] section %.8s vazia\n", sec->Name);
					continue;
				}

				// Em hollow_mode forca RWX em TODAS as paginas. Necessario porque
				// o driver alvo (cdrom.sys) tem callbacks que o kernel pode chamar
				// em qualquer offset — se cair em .data/.rdata (sem X), BSOD 0xFC
				// (ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY). RWX limpa NX em tudo.
				ULONG prot;
				if (hollow_mode) {
					prot = PAGE_EXECUTE_READWRITE;
				} else {
					prot = PAGE_READONLY;
					if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
						prot = (sec->Characteristics & IMAGE_SCN_MEM_WRITE) ?
							PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
					}
					else if (sec->Characteristics & IMAGE_SCN_MEM_WRITE) {
						prot = PAGE_READWRITE;
					}
					else if (sec->Characteristics & IMAGE_SCN_MEM_READ) {
						prot = PAGE_READONLY;
					}
				}

				kdmLog(L"[+] Setting protection for section: "
					<< (char*)sec->Name
					<< L" Base: 0x" << std::hex << secAddr
					<< L" Size: 0x" << secSize
					<< L" Prot: " << ProtectionToString(prot)
					<< std::dec << std::endl);

				bool ok = intel_driver::MmSetPageProtection(secAddr, secSize, prot);
				if (!ok) {
					kdmLog(L"[-] Failed to set protection for section: " << (char*)sec->Name << std::endl);
				}

				if (hollow_mode) {
					LogKdmHollow("[%s] section %.8s @ 0x%llx size=0x%X prot=%s\n",
						ok ? "OK  " : "FAIL",
						sec->Name, (uint64_t)secAddr, secSize, ProtectionToString(prot));
				}
			}

			if (hollow_mode) {
				LogKdmHollow("[INFO] Proteccoes aplicadas. Pronto pra DriverEntry.\n");
			}
		}

		// Call driver entry point

		const ULONG64 address_of_entry_point = kernel_image_base + nt_headers->OptionalHeader.AddressOfEntryPoint;

		kdmLog(L"[<] Calling DriverEntry 0x" << reinterpret_cast<void*>(address_of_entry_point) << std::endl);

		if (hollow_mode) {
			LogKdmHollow("\n[INFO] Calling DriverEntry @ 0x%llx (cdrom.sys + entry_offset)\n",
				address_of_entry_point);
			LogKdmHollow("       AddressOfEntryPoint = 0x%X\n",
				nt_headers->OptionalHeader.AddressOfEntryPoint);
		}

		if (callback) {
			if (!callback(&param1, &param2, realBase, image_size)) {
				kdmLog(L"[-] Callback returns false, failed!" << std::endl);
				kernel_image_base = realBase;
				break;
			}
		}

		NTSTATUS status = 0;
		if (!intel_driver::CallKernelFunction(&status, address_of_entry_point, (PassAllocationAddressAsFirstParam ? realBase : param1), param2)) {
			kdmLog(L"[-] Failed to call driver entry" << std::endl);
			if (hollow_mode) LogKdmHollow("[ERRO] CallKernelFunction(DriverEntry) falhou\n");
			kernel_image_base = realBase;
			break;
		}

		if (exitCode)
			*exitCode = status;

		kdmLog(L"[+] DriverEntry returned 0x" << std::hex << status << std::endl);
		if (hollow_mode) LogKdmHollow("[OK]   DriverEntry retornou status=0x%X (NT_SUCCESS=%d)\n",
			status, NT_SUCCESS(status) ? 1 : 0);

		if (free && !hollow_mode) {
			kdmLog(L"[+] Freeing memory" << std::endl);
			bool free_status = false;

			if (mode == AllocationMode::AllocateIndependentPages)
			{
				free_status = intel_driver::MmFreeIndependentPages(realBase, image_size);
			}
			else {
				free_status = intel_driver::FreePool(realBase);
			}

			if (free_status) {
				kdmLog(L"[+] Memory has been released" << std::endl);
			}
			else {
				kdmLog(L"[-] WARNING: Failed to free memory!" << std::endl);
			}
		}
		else if (hollow_mode) {
			LogKdmHollow("[INFO] hollow_mode: NAO liberando memoria (cdrom.sys nao pode ser freed)\n");
			LogKdmHollow("[OK]   === HOLLOW MAPPING COMPLETO ===\n");
		}



		VirtualFree(local_image_base, 0, MEM_RELEASE);
		return realBase;

	} while (false);


	VirtualFree(local_image_base, 0, MEM_RELEASE);

	if (hollow_mode) {
		LogKdmHollow("[INFO] hollow_mode: erro no fluxo, mas cdrom.sys NAO sera liberado\n");
		return 0;
	}

	kdmLog(L"[+] Freeing memory" << std::endl);
	bool free_status = false;

	if (mode == AllocationMode::AllocateIndependentPages)
	{
		free_status = intel_driver::MmFreeIndependentPages(kernel_image_base, image_size);
	}
	else {
		free_status = intel_driver::FreePool(kernel_image_base);
	}

	if (free_status) {
		kdmLog(L"[+] Memory has been released" << std::endl);
	}
	else {
		kdmLog(L"[-] WARNING: Failed to free memory!" << std::endl);
	}

	return 0;
}


