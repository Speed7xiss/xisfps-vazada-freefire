#pragma once

#include "Comm.hpp"
#include <mutex>

// COMMAND_START_WEBSITE não está em Comm.hpp (comando legado do servidor HTTP).
// Definido aqui para compatibilidade de compilação; não é utilizado pelo loader.
#ifndef COMMAND_START_WEBSITE
#define COMMAND_START_WEBSITE 0x1353
#endif

class CommClient
{
private:
    Comm* m_Comm;

    ULONG64 m_CommandId = 0;
    ULONG64 m_TargetPID = 0;
    std::mutex m_Mutex;

    void WaitForResponseBlocking()
    {
        ULONG spinCount = 0;

        while (!WaitResponse())
        {
            if (spinCount < 2048)
            {
                ++spinCount;
                YieldProcessor();
            }
            else if (spinCount < 2112)
            {
                ++spinCount;
                SwitchToThread();
            }
            else
            {
                spinCount = 0;
                Sleep(1);
            }
        }
    }

public:
    explicit CommClient(Comm* comm)
        : m_Comm(comm)
    {
    }

    void Initialize() {
        m_Comm->Magic = ENTRY_USER_MAGIC;
    }


    bool IsConnected()
    {
        return m_Comm &&
            m_Comm->Magic == ENTRY_DRIVER_MAGIC;
    }


    void SendCommand(ULONG64 command)
    {
        ++m_CommandId;

        m_Comm->Command = command;

        MemoryBarrier();
        InterlockedExchange64(
            reinterpret_cast<volatile LONG64*>(&m_Comm->CommandMagic),
            static_cast<LONG64>(m_CommandId));
    }

    void SetPID(ULONG64 pid)
    {
        m_TargetPID = pid;
    }


    bool WaitResponse()
    {
        const ULONG64 result = *reinterpret_cast<volatile ULONG64*>(
            &m_Comm->CommandResultMagic);

        if (result != m_CommandId)
            return false;

        MemoryBarrier();
        return true;
    }


    bool Exit()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        SendCommand(COMMAND_EXIT);

        WaitForResponseBlocking();

        return true;
    }


    bool HeartBeat(ULONG64 value, ULONG64& result)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_Comm->Data.HeartBeat.Value = value;

        SendCommand(COMMAND_HEARTBEAT);

        WaitForResponseBlocking();

        result = m_Comm->Data.HeartBeat.NextValue;

        return true;
    }


    bool SetNewProcess(
        ULONG64 pid,
        ULONG64 address)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_Comm->Data.SetNewProcess.ProcessId = pid;
        m_Comm->Data.SetNewProcess.Address = address;

        SendCommand(COMMAND_SET_NEW_PROCESS);

        WaitForResponseBlocking();

        return true;
    }


    ULONG64 GetProcessBase()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_Comm->Data.GetProcessBase.ProcessId = m_TargetPID;

        SendCommand(COMMAND_GET_PROCESS_BASE);

        WaitForResponseBlocking();
        return m_Comm->Data.GetProcessBase.BaseAddress;
    }

    template<typename T>
    T Read(ULONG64 address)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        T buffer{};

        auto& mem = m_Comm->Data.Memory;

        mem.ProcessId = m_TargetPID;
        mem.Address = address;
        mem.Buffer = reinterpret_cast<ULONG64>(&buffer);
        mem.Size = sizeof(T);

        SendCommand(COMMAND_READ_MEMORY);

        WaitForResponseBlocking();

        return buffer;
    }

    template<typename T>
    T ReadPhysical(ULONG64 physicalAddress)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        T buffer{};

        auto& mem = m_Comm->Data.Memory;

        mem.ProcessId = 0;
        mem.Address = physicalAddress;
        mem.Buffer = reinterpret_cast<ULONG64>(&buffer);
        mem.Size = sizeof(T);
        mem.ResultSize = 0;

        SendCommand(COMMAND_READ_PHYS_MEMORY);

        WaitForResponseBlocking();

        return buffer;
    }

    ULONG64 GetModuleBase(const wchar_t* moduleName)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto& mod = m_Comm->Data.GetModuleBase;

        mod.ProcessId = m_TargetPID;
        mod.BaseAddress = 0;

        wcsncpy_s(
            mod.ModuleName,
            moduleName,
            _TRUNCATE);

        SendCommand(COMMAND_GET_MODULE_BASE);

        WaitForResponseBlocking();

        return mod.BaseAddress;
    }

    template<typename T>
    bool Write(ULONG64 address, const T& value)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto& mem = m_Comm->Data.Memory;

        mem.ProcessId = m_TargetPID;
        mem.Address = address;
        mem.Buffer = reinterpret_cast<ULONG64>(&value);
        mem.Size = sizeof(T);

        SendCommand(COMMAND_WRITE_MEMORY);

        WaitForResponseBlocking();

        return mem.Status == 0 &&
            mem.ResultSize == sizeof(T);
    }

    void StreamMode(HANDLE handle, char affinity) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_Comm->Data.Stream.Handle = reinterpret_cast<ULONG64>(handle);
        m_Comm->Data.Stream.Affinity = affinity;

        SendCommand(COMMAND_STREAM_MODE);

        WaitForResponseBlocking();
    }

    void CleanJournal() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        SendCommand(COMMAND_JOURNAL);

        WaitForResponseBlocking();
    }

    void CleanCsrss() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        SendCommand(COMMAND_CLEAN_CSRSS);

        WaitForResponseBlocking();
    }

    void StartServer() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        SendCommand(COMMAND_START_WEBSITE);

        WaitForResponseBlocking();
    }

    bool DeleteRegistryKey(const wchar_t* path)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto& reg = m_Comm->Data.DeleteRegistryKey;

        wcsncpy_s(
            reg.KeyPath,
            path,
            _TRUNCATE);

        SendCommand(COMMAND_DELETE_REG_KEY);

        WaitForResponseBlocking();

        return true;
    }

    bool DeleteRegistryValue(
        const wchar_t* path,
        const wchar_t* value)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto& reg = m_Comm->Data.DeleteRegistryValue;

        wcsncpy_s(
            reg.KeyPath,
            path,
            _TRUNCATE);

        wcsncpy_s(
            reg.ValueName,
            value,
            _TRUNCATE);

        SendCommand(COMMAND_DELETE_REG_VALUE);

        WaitForResponseBlocking();

        return true;
    }
};

extern std::unique_ptr<CommClient> g_CommClient;
extern "C" __declspec(dllexport) Comm g_Comm;
