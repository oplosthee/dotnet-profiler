#include "Callbacks.h"
#include <map>

static const int MAXIMUM_PACKET_SIZE = 16 * 1024;

thread_local std::wstring queue;

extern "C" void _stdcall SendPacket(WCHAR *type, FunctionID funcId)
{
    WCHAR packet[1024];
    thread::id threadId = std::this_thread::get_id();
    wsprintf(packet, L"PROFILER\x01%d\x01%ls\x01%d\r\n", threadId, type, funcId);

    DWORD cbToWrite = (lstrlen(packet) + 1) * sizeof(WCHAR);
    DWORD queueLength = queue.length() * sizeof(WCHAR);
    if (queueLength + cbToWrite > MAXIMUM_PACKET_SIZE)
    {
        DWORD cbWritten;
        BOOL fSuccess = WriteFile(
            hPipe,
            queue.c_str(),
            queueLength,
            &cbWritten,
            NULL
        );

        if (!fSuccess)
            Log(L"WriteFile to pipe failed.");

        queue.clear();
    }

    queue.append(packet);
}

FunctionID FunctionMapper(FunctionID funcId, BOOL* pbHookFunction)
{
    HRESULT res = S_OK;
    mdMethodDef methodId;
    CComPtr<IMetaDataImport> metadataImport;
    res = corProfiler->corProfilerInfo->GetTokenAndMetaDataFromFunction(funcId, IID_IMetaDataImport, reinterpret_cast<IUnknown**>(&metadataImport), &methodId);

    if (SUCCEEDED(res))
    {
        mdTypeDef typeDefToken;
        WCHAR methodNameBuffer[1024];
        ULONG actualMethodNameSize;
        res = metadataImport->GetMethodProps(methodId, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0);

        if (SUCCEEDED(res))
        {
            WCHAR typeNameBuffer[1024];
            ULONG actualTypeNameSize;
            res = metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0);

            if (SUCCEEDED(res))
            {
                WCHAR lpvMessage[1024];
                thread::id threadId = std::this_thread::get_id();
                wsprintf(lpvMessage, L"PROFILER\x01%d\x01%ls\x01%d\x01%ls\x01%ls\r\n", threadId, L"MAP", funcId, typeNameBuffer, methodNameBuffer);

                // Append the message to the send queue. This might exceed MAXIMUM_PACKET_SIZE by one packet.
                // The buffer on the server is however large enough to accommodate for this.
                queue.append(lpvMessage);
            }
        }
    }

    return funcId;
}

#ifdef _WIN64

void __fastcall FunctionEnterNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_INFO* argumentInfo)
{
    SendPacket(L"ENTER", funcId);
}

void __fastcall FunctionLeaveNaked(FunctionID funcId, UINT_PTR clientdata, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_RANGE* retvalRange)
{
    SendPacket(L"LEAVE", funcId);
}

void __fastcall FunctionTailcallNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func)
{
    SendPacket(L"TAILCALL", funcId);
}

#else

void __declspec(naked) FunctionEnterNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_INFO* argumentInfo)
{
    __asm // TODO: Does not call method with "type" argument - fix if x86 support is necessary.
    {
        push[ESP + 8]
        call FunctionEnterImpl
        ret 16; // Pop the 4 arguments, 4 bytes each, 16 bytes total.
    }
}

void __declspec(naked) FunctionLeaveNaked(FunctionID funcId, UINT_PTR clientdata, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_RANGE* retvalRange)
{
    __asm
    {
        ret 16; // Pop the 4 arguments, 4 bytes each, 16 bytes total.
    }
}

void __declspec(naked) FunctionTailcallNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func)
{
    __asm
    {
        ret 12; // Pop the 3 arguments, 4 bytes each, 12 bytes total.
    }
}

#endif
