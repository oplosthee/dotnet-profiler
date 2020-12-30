#include "Callbacks.h"

static const int QUEUE_SIZE_THRESHOLD = 16 * 1024;

std::map<thread::id, std::wstring> queueMap;

void EnqueuePacket(WCHAR* data, BOOL flush)
{
    // The queue map gets populated on thread creation, so it can be guaranteed the key exists.
    auto it = queueMap.find(std::this_thread::get_id());

    it->second.append(data);

    DWORD cbToWrite = (lstrlen(data) + 1) * sizeof(WCHAR);
    DWORD queueLength = it->second.length() * sizeof(WCHAR);
    if (flush || (queueLength + cbToWrite > QUEUE_SIZE_THRESHOLD))
    {
        DWORD cbWritten;
        BOOL fSuccess = WriteFile(
            hPipe,
            it->second.c_str(),
            queueLength,
            &cbWritten,
            NULL
        );

        if (!fSuccess)
            Log(L"WriteFile to pipe failed.");

        it->second.clear();
    }
}

extern "C" void _stdcall SendFunctionPacket(WCHAR *type, FunctionID funcId)
{
    WCHAR packet[1024];
    thread::id threadId = std::this_thread::get_id();
    wsprintf(packet, L"PROFILER\x01%d\x01%ls\x01%d\r\n", threadId, type, funcId);

    EnqueuePacket(packet, false);
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
                WCHAR packet[1024];
                thread::id threadId = std::this_thread::get_id();
                wsprintf(packet, L"PROFILER\x01%d\x01%ls\x01%d\x01%ls\x01%ls\r\n", threadId, L"MAP", funcId, typeNameBuffer, methodNameBuffer);

                EnqueuePacket(packet, true);
            }
        }
    }

    return funcId;
}

#ifdef _WIN64

void __fastcall FunctionEnterNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_INFO* argumentInfo)
{
    SendFunctionPacket(L"ENTER", funcId);
}

void __fastcall FunctionLeaveNaked(FunctionID funcId, UINT_PTR clientdata, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_RANGE* retvalRange)
{
    SendFunctionPacket(L"LEAVE", funcId);
}

void __fastcall FunctionTailcallNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func)
{
    SendFunctionPacket(L"TAILCALL", funcId);
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
