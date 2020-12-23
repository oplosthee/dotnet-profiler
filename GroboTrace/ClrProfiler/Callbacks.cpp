#include "Callbacks.h"

struct FunctionData
{
    std::wstring name;
};

std::map<FunctionID, FunctionData> functionMap;

std::vector<clock_t> intervals;
clock_t min = 1000;
clock_t max = -1000;

extern "C" void _stdcall SendPacket(WCHAR *type, FunctionID funcId)
{
    //Log(L"SendPacket Enter");

    /*ClassID classId;
    ModuleID moduleId;
    mdMethodDef methodId;
    corProfiler->corProfilerInfo->GetFunctionInfo(funcId, &classId, &moduleId, &methodId);

    ULONG actualModuleNameSize;
    WCHAR moduleNameBuffer[1024];
    AssemblyID assemblyId;
    corProfiler->corProfilerInfo->GetModuleInfo(moduleId, 0, 1024, &actualModuleNameSize, moduleNameBuffer, &assemblyId);

    ULONG actualAssemblyNameSize;
    WCHAR assemblyNameBuffer[1024];
    corProfiler->corProfilerInfo->GetAssemblyInfo(assemblyId, 1024, &actualAssemblyNameSize, assemblyNameBuffer, 0, 0);

    CComPtr<IMetaDataImport> metadataImport;
    corProfiler->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown**>(&metadataImport));

    mdTypeDef typeDefToken;
    WCHAR methodNameBuffer[1024];
    ULONG actualMethodNameSize;
    metadataImport->GetMethodProps(methodId, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0);

    WCHAR typeNameBuffer[1024];
    ULONG actualTypeNameSize;
    metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0);*/

    // Retrieve the function metadata from the map
    std::map<FunctionID, FunctionData>::iterator iter = functionMap.find(funcId);
    if (iter != functionMap.end())
    {
        //Log(L"SendPacket Found");
        FunctionData functionData = iter->second;

        WCHAR lpvMessage[1024];
        thread::id threadId = std::this_thread::get_id();
        //Log(L"SendPacket Format");
        // "PROFILER" is prepended so we can determine the bounds of the packet and see whether it is malformed.
        wsprintf(lpvMessage, L"PROFILER\x01%d\x01%ls\x01%d\x01%ls\r\n", threadId, type, funcId, functionData.name.c_str());

        DWORD cbToWrite = (lstrlen(lpvMessage) + 1) * sizeof(WCHAR);
        DWORD cbWritten;
        //Log(L"SendPacket Copied");

        //clock_t t = clock();
        BOOL fSuccess = WriteFile(
            hPipe,
            lpvMessage,
            cbToWrite,
            &cbWritten,
            NULL
        );

        //Log(L"SendPacket Sent");

        if (!fSuccess)
            Log(L"WriteFile to pipe failed.");
    }

    /*
    t = clock() - t;

    intervals.push_back(t);

    if (t > max)
        max = t;

    if (t < min)
        min = t;

    if (intervals.size() >= 100)
    {
        double average = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
        WCHAR buffer[1024];
        swprintf(buffer, L"Last 100 calls: min:%d, max:%d, avg:%f\r\n", min, max, average);
        Log(buffer);

        intervals.clear();
        min = 1000;
        max = -1000;
    }*/
}

FunctionID FunctionMapper(FunctionID funcId, BOOL* pbHookFunction)
{
    //Log(L"FunctionMapper Enter");
    // Check if the function already exists in the map
    std::map<FunctionID, FunctionData>::iterator iter = functionMap.find(funcId);
    // Function does not exist in the map yet, so we retrieve the metadata and store it.
    if (iter == functionMap.end())
    {
        //Log(L"FunctionMapper Not Found");
        HRESULT res = S_OK;
        mdMethodDef methodId;
        CComPtr<IMetaDataImport> metadataImport;
        res = corProfiler->corProfilerInfo->GetTokenAndMetaDataFromFunction(funcId, IID_IMetaDataImport, reinterpret_cast<IUnknown**>(&metadataImport), &methodId);

        if (SUCCEEDED(res))
        {
            //Log(L"FunctionMapper succ1");
            mdTypeDef typeDefToken;
            WCHAR methodNameBuffer[1024];
            ULONG actualMethodNameSize;
            res = metadataImport->GetMethodProps(methodId, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0);

            if (SUCCEEDED(res))
            {
                //Log(L"FunctionMapper succ2");
                WCHAR typeNameBuffer[1024];
                ULONG actualTypeNameSize;
                res = metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0);

                if (SUCCEEDED(res))
                {
                    //Log(L"FunctionMapper Adding");
                    WCHAR data[2048];
                    wsprintf(data, L"%ls\x01%ls", typeNameBuffer, methodNameBuffer);

                    FunctionData functionData = { std::wstring(data) };
                    functionMap.insert(std::pair<FunctionID, FunctionData>(funcId, functionData));
                    //Log(L"FunctionMapper Added");
                }
            }
        }
    }

    //Log(L"FunctionMapper Leave");
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
