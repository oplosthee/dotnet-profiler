// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "ClassFactory.h"
#include "CorProfiler.h"
#include "Callbacks.h"

const IID IID_IUnknown      = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

const IID IID_IClassFactory = { 0x00000001, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

BOOL STDMETHODCALLTYPE DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_DETACH:
            OutputDebugString(L"grobotrace: DLL_PROCESS_DETACH");

            // Iterate through all the thread-local queues and send them to the server.
            std::map<thread::id, std::wstring>::iterator it;
            BOOL fSuccess;

            for (it = queueMap.begin(); it != queueMap.end(); it++)
            {
                DWORD cbWritten;
                fSuccess = WriteFile(
                    hPipe,
                    it->second.c_str(),
                    it->second.length() * sizeof(WCHAR),
                    &cbWritten,
                    NULL
                );

                if (!fSuccess)
                    Log(L"WriteFile to pipe failed.");
            }

            // If we dumped the queues, we finish the communication by sending a "QUIT" packet.
            if (fSuccess)
            {
                WCHAR packet[1024];
                thread::id threadId = std::this_thread::get_id();
                wsprintf(packet, L"PROFILER\x01%d\x01%ls\r\n", threadId, L"QUIT");

                DWORD cbWritten;
                WriteFile(
                    hPipe,
                    packet,
                    (lstrlen(packet) + 1) * sizeof(WCHAR),
                    &cbWritten,
                    NULL
                );
            }

            break;
    }
    return TRUE;
}

extern "C" HRESULT STDMETHODCALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    // {1bde2824-ad74-46f0-95a4-d7e7dab3b6b6}
    const GUID CLSID_CorProfiler = { 0x1bde2824, 0xad74, 0x46f0, { 0x95, 0xa4, 0xd7, 0xe7, 0xda, 0xb3, 0xb6, 0xb6 } };

    if (ppv == nullptr || rclsid != CLSID_CorProfiler)
    {
        return E_FAIL;
    }

    auto factory = new ClassFactory;
    if (factory == nullptr)
    {
        return E_FAIL;
    }

    return factory->QueryInterface(riid, ppv);
}

extern "C" HRESULT STDMETHODCALLTYPE DllCanUnloadNow()
{
    return S_OK;
}