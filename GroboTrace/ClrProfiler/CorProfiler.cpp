// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "CorProfiler.h"
#include "corhlpr.h"
#include "CComPtr.h"
#include "profiler_pal.h"
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include "Callbacks.h"

#define BUFSIZE 512

// Global static singletons
HANDLE hPipe;
CorProfiler* corProfiler = NULL;


CorProfiler::CorProfiler() : refCount(0), corProfilerInfo(nullptr), callback(nullptr), init(nullptr), failed(false)
{
}

CorProfiler::~CorProfiler()
{
    if (this->corProfilerInfo != nullptr)
    {
        this->corProfilerInfo->Release();
        this->corProfilerInfo = nullptr;
    }
}

void DebugOutput(char* str)
{
#ifdef _DEBUG
	OutputDebugStringA(str);
#else
#endif
}

void DebugOutput(WCHAR* str)
{
#ifdef _DEBUG
	OutputDebugStringW(str);
#else
#endif
}

#define USE_SETTINGS

void Log(wstring str)
{
	OutputDebugString((L"grobotrace: " + str).c_str());
}

DWORD STDMETHODCALLTYPE Suicide(void *p)
{
	for (int i = 0; i < 10; ++i)
	{
		Sleep(1000);
		Log(L"Requesting profiler detach");
		if (corProfiler->corProfilerInfo->RequestProfilerDetach(0) == S_OK)
			break;
	}
	return 0;
}

#ifdef USE_SETTINGS

vector<wstring> ParseLine(const wstring& str)
{
	int n = str.length();
	int state = 0;
	vector<wstring> result;
	vector<WCHAR> cur;
	for (int i = 0; i <= n; ++i)
	{
		auto c = i < n ? str[i] : ' ';
		switch (state)
		{
		case 0:
			if (c != ' ' && c != '\t')
			{
				if (c == '"')
					state = 2;
				else {
					cur.push_back(c);
					state = 1;
				}
			}
			break;
		case 1:
			if (c == ' ' || c == '\t')
			{
				result.push_back(wstring(cur.begin(), cur.end()));
				cur.clear();
				state = 0;
			}
			else cur.push_back(c);
			break;
		case 2:
			if (c == '"')
			{
				result.push_back(wstring(cur.begin(), cur.end()));
				cur.clear();
				state = 0;
			}
			else if (c == '\\')
				state = 3;
			else
				cur.push_back(c);
			break;
		case 3:
			if (c == '"')
				cur.push_back('"');
			else if (c == '\\')
				cur.push_back('\\');
			else
			{
				cur.push_back('\\');
				cur.push_back(c);
			}
			state = 2;
			break;
		}
	}
	return result;
}

bool NeedProfile(const wstring& settingsFileName)
{
	WCHAR fullFileName[1024];
	WCHAR str[1024];
	auto len = GetModuleFileNameW(nullptr, fullFileName, 1024);
	Log(L"Asked to profile:");
	Log(fullFileName);

	auto settingsStream = wifstream(settingsFileName);
	if (!settingsStream.is_open())
		return false;

	wstring fileName;
	for (int i = len - 1; i >= 0; --i)
		if (fullFileName[i] == '\\')
		{
			fileName = wstring(&fullFileName[i + 1]);
			break;
		}

	bool needProfile = false;
	while (!settingsStream.eof())
	{
		wstring cur;
		getline(settingsStream, cur);
		auto parsedLine = ParseLine(cur);

		if (parsedLine.size() == 0)
			continue;
		const auto& processName = parsedLine[0];
		if (processName != fileName)
			continue;
		if (parsedLine.size() == 1)
		{
			needProfile = true;
			break;
		}
		auto commandLine = wstring(GetCommandLine());
		for (int i = 1; i < parsedLine.size(); ++i)
			needProfile |= commandLine.find(parsedLine[i]) != string::npos;
	}
	settingsStream.close();
	return needProfile;
}

#endif

LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\tracer");

HRESULT STDMETHODCALLTYPE CorProfiler::Initialize(IUnknown *pICorProfilerInfoUnk)
{
	Log(L"Profiler started");

	corProfiler = this;

    HRESULT queryInterfaceResult = pICorProfilerInfoUnk->QueryInterface(__uuidof(ICorProfilerInfo4), reinterpret_cast<void **>(&this->corProfilerInfo));

    if (FAILED(queryInterfaceResult))
    {
        return E_FAIL;
    }

	FindProfilerFolder();

#ifdef USE_SETTINGS

	bool needProfile = NeedProfile(profilerFolder + L"\\GroboTrace.ini");

	Log(needProfile ? L"will profile" : L"skipped");

    // Only try to connect to the named pipe if the process should be profiled.
    if (needProfile)
    {
        // Pipe code based on MSDN Documentation (https://docs.microsoft.com/en-us/windows/win32/ipc/named-pipe-client)
        while (true)
        {
            hPipe = CreateFile(
                lpszPipename,   // Pipe name
                GENERIC_READ |  // Read and write access
                GENERIC_WRITE,
                0,              // No sharing
                NULL,           // Default security attributes
                OPEN_EXISTING,  // Open existing pipe
                0,              // Default attributes
                NULL            // No template file
            );

            // Break if we connected sucessfully.
            if (hPipe != INVALID_HANDLE_VALUE)
                break;

            // If there was an error other than the pipe being busy, quit.
            if (GetLastError() != ERROR_PIPE_BUSY)
            {
                Log(L"Could not open pipe");
                return E_FAIL;
            }

            // All pipe instances busy, so wait for 20 seconds.
            if (!WaitNamedPipe(lpszPipename, 20000))
            {
                Log(L"All pipe instances busy, 20 second timeout reached.");
                return E_FAIL;
            }
        }

        // Configure the communication over the pipe.
        /*DWORD dwMode = PIPE_READMODE_BYTE;
        BOOL fSucces = SetNamedPipeHandleState(
            hPipe,
            &dwMode,
            NULL,
            NULL
        );

        if (!fSucces)
        {
            Log(L"SetNamedPipeHandleState failed.");
            return E_FAIL;
        }*/
    }

	/*DWORD eventMask = needProfile
        ? COR_PRF_MONITOR_JIT_COMPILATION
        | COR_PRF_ENABLE_REJIT
        | COR_PRF_DISABLE_ALL_NGEN_IMAGES                      // the last two masks are set to enable reJIT - NGENd images must be ignored for this
		| COR_PRF_DISABLE_TRANSPARENCY_CHECKS_UNDER_FULL_TRUST // helps the case where this profiler is used on Full CLR
															   //| COR_PRF_DISABLE_INLINING
        | COR_PRF_MONITOR_ENTERLEAVE
		: COR_PRF_MONITOR_NONE;*/

    DWORD eventMask = needProfile
        ? COR_PRF_MONITOR_ENTERLEAVE
        : COR_PRF_MONITOR_NONE;

#else
	DWORD eventMask = COR_PRF_MONITOR_JIT_COMPILATION
		| COR_PRF_DISABLE_TRANSPARENCY_CHECKS_UNDER_FULL_TRUST /* helps the case where this profiler is used on Full CLR */
															   /*| COR_PRF_DISABLE_INLINING*/
		;
#endif

    auto hr = this->corProfilerInfo->SetEventMask(eventMask);
    this->corProfilerInfo->SetEnterLeaveFunctionHooks2((FunctionEnter2*) &FunctionEnterNaked, (FunctionLeave2*) &FunctionLeaveNaked, (FunctionTailcall2*) &FunctionTailcallNaked);
    this->corProfilerInfo->SetFunctionIDMapper((FunctionIDMapper*) &FunctionMapper);

	InitializeCriticalSection(&criticalSection);

	Log(L"Profiler successfully initialized");

	if (!needProfile)
	{
		DWORD tmp;
		CreateThread(0, 0, Suicide, 0, 0, &tmp);
	}

    return S_OK;
}

void CorProfiler::FindProfilerFolder()
{
	WCHAR fileName[1024];

	int len = GetModuleFileName(GetModuleHandle(L"ClrProfiler.dll"), fileName, 1024);
	for (int i = len - 1; i >= 0; --i)
		if (fileName[i] == '\\')
		{
			fileName[i] = 0;
			break;
		}
	profilerFolder = wstring(fileName);
}

HRESULT STDMETHODCALLTYPE CorProfiler::Shutdown()
{
	Log(L"Profiler is about to shutdown, saving profiling stats ..");

    Log(L"Calling 'SaveStats' method");
    saveStats();
    Log(L"Successfully called 'SaveStats' method");

    Log(L"Shutting down profiler ..");
    if (this->corProfilerInfo != nullptr)
    {
        this->corProfilerInfo->Release();
        this->corProfilerInfo = nullptr;
    }
	DeleteCriticalSection(&criticalSection);

    CloseHandle(hPipe);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainCreationStarted(AppDomainID appDomainId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainCreationFinished(AppDomainID appDomainId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainShutdownStarted(AppDomainID appDomainId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainShutdownFinished(AppDomainID appDomainId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyLoadStarted(AssemblyID assemblyId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyLoadFinished(AssemblyID assemblyId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyUnloadStarted(AssemblyID assemblyId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyUnloadFinished(AssemblyID assemblyId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleLoadStarted(ModuleID moduleId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleUnloadStarted(ModuleID moduleId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleUnloadFinished(ModuleID moduleId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleAttachedToAssembly(ModuleID moduleId, AssemblyID AssemblyId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassLoadStarted(ClassID classId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassLoadFinished(ClassID classId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassUnloadStarted(ClassID classId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassUnloadFinished(ClassID classId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::FunctionUnloadStarted(FunctionID functionId)
{
    return S_OK;
}



void* allocateForMethodBody(ModuleID moduleId, ULONG size)
{
	IMethodMalloc* pMalloc;

	corProfiler->corProfilerInfo->GetILFunctionBodyAllocator(moduleId, &pMalloc);

	return pMalloc->Alloc(size);
}


mdToken GetTokenFromSig(ModuleID moduleId, char* sig, int len)
{
	CComPtr<IMetaDataEmit> metadataEmit;
	if (FAILED(corProfiler->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataEmit, reinterpret_cast<IUnknown **>(&metadataEmit))))
	{
		OutputDebugString(L"Failed to get metadata emit {C++}");
		return 0;
	}
	
	mdSignature token;
	metadataEmit->GetTokenFromSig(reinterpret_cast<PCCOR_SIGNATURE>(sig), len, &token);

	return token;
}

// A set containing all functions that have been marked for JIT compilation.
unordered_set<UINT_PTR> toInject;
std::vector<ModuleID> jitModuleIds;
std::vector<mdMethodDef> jitMethodIds;

bool isFirst = true;
bool isTracerReady = false;

HRESULT STDMETHODCALLTYPE CorProfiler::JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock)
{
    // TODO: We no longer replace methods but use FunctionEnter2 instead, so skip this code. Remove later.
    return S_OK;

	HRESULT hr;
	mdMethodDef methodDefToken;
	mdTypeDef typeDefToken;
	mdModule moduleToken;
	ClassID classId;
	ModuleID moduleId;
	AssemblyID assemblyId;
	WCHAR methodNameBuffer[1024];
	ULONG actualMethodNameSize;
	WCHAR typeNameBuffer[1024];
	ULONG actualTypeNameSize;
	WCHAR moduleNameBuffer[1024];
	ULONG actualModuleNameSize;
	WCHAR assemblyNameBuffer[1024];
	ULONG actualAssemblyNameSize;
	char str[1024];

	//sprintf(str, "JIT Compilation of the method %I64d", functionId);

	//OutputDebugStringA(str);

	/*if (FAILED(this->corProfilerInfo->GetFunctionInfo2(functionId, 0, 0, 0, 0, 0, &methodGenericParameters, 0)))
	{
		OutputDebugStringW(L"GetFunctionInfo2 failed");
		return S_OK;
	}*/

	//sprintf(str, "MethodGenericParameters %d\r\n", methodGenericParameters);

	//OutputDebugStringA(str);

	//OutputDebugStringW(L"We are dead");

    if (this->failed)
    {
        Log(L"Failed");
        return S_OK;
    }
		
	if (FAILED(this->corProfilerInfo->GetFunctionInfo(functionId, &classId, &moduleId, &methodDefToken)))
	{
        Log(L"GetFunctionInfo failed");
		return S_OK;
	}


	if (FAILED(this->corProfilerInfo->GetModuleInfo(moduleId, 0, 1024, &actualModuleNameSize, moduleNameBuffer, &assemblyId)))
	{
        Log(L"GetModuleInfo failed");
		return S_OK;
	}

	if (FAILED(this->corProfilerInfo->GetAssemblyInfo(assemblyId, 1024, &actualAssemblyNameSize, assemblyNameBuffer, 0, 0)))
	{
        Log(L"GetAssemblyInfo failed");
		return S_OK;
	}

	CComPtr<IMetaDataImport> metadataImport;
	if (FAILED(corProfiler->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport))))
	{
        Log(L"Failed to get IMetadataImport {C++}");
		return S_OK;
	}

	if (FAILED(metadataImport->GetMethodProps(methodDefToken, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0)))
	{
        Log(L"GetMethodProps failed");
		return S_OK;
	}

	if (FAILED(metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0)))
	{
        Log(L"GetTypeDefProps failed");
		return S_OK;
	}


	if (!lstrcmpW(assemblyNameBuffer, L"GroboTrace"))
		return S_OK;

	if (!lstrcmpW(assemblyNameBuffer, L"GroboTrace.Core"))
		return S_OK;

    if (wcsstr(assemblyNameBuffer, L"Grobo"))
        return S_OK;

	if (!lstrcmpW(assemblyNameBuffer, L"GrEmit"))
		return S_OK;

    if (callback == nullptr)
    {
        //if (!lstrcmpW(assemblyNameBuffer, L"System.Core"))
        //    return S_OK;

        // Check ReJIT blog on metadata changes in order to instrument mscorlib.
        if (!lstrcmpW(assemblyNameBuffer, L"mscorlib"))
            return S_OK;
    }

    WCHAR jitDebug[1024];
	wsprintf(jitDebug, L"JIT Compilation of the method %I64d %ls.%ls\r\n", functionId, typeNameBuffer, methodNameBuffer);

    // Check if this function has been seen before, if yes, we skip and JIT compile it.
    // This is done to prevent infinite loops in .NET functions necessary for callback injection.
    // TODO: Place at the top after testing - logic is here now to view debug print above.
    if (!isTracerReady)
    {
        if (!isFirst)
        {
            jitModuleIds.push_back(moduleId);
            jitMethodIds.push_back(methodDefToken);
            return S_OK;
        }
    }

    //Log(jitDebug);

    isFirst = false;

	if (callback == nullptr)
	{
		Log(L"Trying to enter critical section");
		EnterCriticalSection(&criticalSection);
        Log(L"Entered to critical section");
		if (!callback)
		{
			DebugOutput(L"Trying to load .NET lib");
			WCHAR fileName[1024];

			int len = GetModuleFileName(GetModuleHandle(L"ClrProfiler.dll"), fileName, 1024);
			int slashIndex;
			for (int i = len - 1; i >= 0; --i)
				if (fileName[i] == '\\')
				{
					slashIndex = i;
					int k = wsprintf(&fileName[i + 1], L"GroboTrace.Core.dll");
					fileName[i + 1 + k] = 0;
					break;
				}

            Log(fileName);
			auto groboTrace = LoadLibrary(fileName);
			if (!groboTrace)
                Log(L"Failed to load GroboTrace.Core");
			else
                Log(L"Successfully loaded GroboTrace.Core");

			auto procAddr = GetProcAddress(groboTrace, "SetProfilerPath");
			if (!procAddr)
			{
                Log(L"Failed to obtain 'SetProfilerPath' method addr");
				wsprintf(fileName, L"%ld", GetLastError());
                Log(fileName);
				failed = true;
				LeaveCriticalSection(&criticalSection);
				return S_OK;
			}
			else
                Log(L"Successfully got 'SetProfilerPath' method addr");
			setProfilerPath = reinterpret_cast<void(*)(WCHAR*)>(procAddr);

			procAddr = GetProcAddress(groboTrace, "Init");
			if (!procAddr)
			{
                Log(L"Failed to obtain 'Init' method addr");
				wsprintf(fileName, L"%ld", GetLastError());
                Log(fileName);
				failed = true;
				LeaveCriticalSection(&criticalSection);
				return S_OK;
			}
			else
                Log(L"Successfully got 'Init' method addr");
			init = reinterpret_cast<void(*)(void*, void*)>(procAddr);

            procAddr = GetProcAddress(groboTrace, "SaveStats");
            if (!procAddr)
            {
                Log(L"Failed to obtain 'SaveStats' method addr");
                wsprintf(fileName, L"%ld", GetLastError());
                Log(fileName);
                failed = true;
                LeaveCriticalSection(&criticalSection);
                return S_OK;
            }
            else
                Log(L"Successfully got 'SaveStats' method addr");
            saveStats = reinterpret_cast<void(*)()>(procAddr);

            saveStats();
            Log(L"Successfully called 'SaveStats' method");

			fileName[slashIndex] = 0;

			setProfilerPath(fileName);
            Log(L"Successfully called 'SetProfilerPath' method");

			init(static_cast<void*>(&GetTokenFromSig), static_cast<void*>(&CoTaskMemAlloc));
            Log(L"Successfully called 'Init' method");

			procAddr = GetProcAddress(groboTrace, "InstallTracing");
			if (!procAddr)
			{
                Log(L"Failed to obtain 'InstallTracing' method addr");
				wsprintf(fileName, L"%ld", GetLastError());
                Log(fileName);
				failed = true;
				LeaveCriticalSection(&criticalSection);
				return S_OK;
			}
			else
                Log(L"Successfully got 'InstallTracing' method addr");
			callback = reinterpret_cast<SharpResponse(*)(WCHAR*, WCHAR*, FunctionID, mdToken, char*, void*)>(procAddr);
		}
		LeaveCriticalSection(&criticalSection);
	}

	LPCBYTE methodBody;

	IfFailRet(corProfilerInfo->GetILFunctionBody(moduleId, methodDefToken, &methodBody, NULL));

	SharpResponse sharpResponse = SharpResponse();
	sharpResponse.newMethodBody = nullptr;

	sharpResponse = callback(assemblyNameBuffer, moduleNameBuffer, moduleId, methodDefToken, (char*)methodBody, static_cast<void*>(&allocateForMethodBody));

    // Once we get here we know that all functionality necessary to perform injection has been loaded.
    isTracerReady = true;
    /*
    IfFailRet(corProfilerInfo->RequestReJIT(jitModuleIds.size(), jitModuleIds.data(), jitMethodIds.data()));
    jitModuleIds.clear();
    jitMethodIds.clear();
    */

	if (sharpResponse.newMethodBody != nullptr)
	{
		/*OutputDebugStringA("!!! Map entries: ");
		for (unsigned int i = 0; i < sharpResponse.mapEntriesCount; ++i)
		{
			sprintf(str, "Old offset: %u  New offset: %u  fAccurate: %d \r\n", sharpResponse.pMapEntries[i].oldOffset, sharpResponse.pMapEntries[i].newOffset, sharpResponse.pMapEntries[i].fAccurate);

			OutputDebugStringA(str);
		}*/

		IfFailRet(corProfilerInfo->SetILInstrumentedCodeMap(functionId, true, sharpResponse.mapEntriesCount, sharpResponse.pMapEntries));
		IfFailRet(corProfilerInfo->SetILFunctionBody(moduleId, methodDefToken, sharpResponse.newMethodBody));
		//DebugOutput(L"Successfully rewrote method");
	}
	
	return S_OK;

	//mdSignature enterLeaveMethodSignatureToken;
	//metadataEmit->GetTokenFromSig(enterLeaveMethodSignature, sizeof(enterLeaveMethodSignature), &enterLeaveMethodSignatureToken);

	//return RewriteIL(this->corProfilerInfo, nullptr, moduleId, token, functionId, reinterpret_cast<ULONGLONG>(EnterMethodAddress), reinterpret_cast<ULONGLONG>(LeaveMethodAddress), enterLeaveMethodSignatureToken);
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCachedFunctionSearchStarted(FunctionID functionId, BOOL *pbUseCachedFunction)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCachedFunctionSearchFinished(FunctionID functionId, COR_PRF_JIT_CACHE result)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITFunctionPitched(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITInlining(FunctionID callerId, FunctionID calleeId, BOOL *pfShouldInline)
{
	return S_OK;

	HRESULT hr;
	mdToken methodDefToken;
	mdTypeDef typeDefToken;
	mdModule moduleToken;
	ClassID classId;
	ModuleID moduleId;
	AssemblyID assemblyId;
	WCHAR methodNameBuffer[1024];
	ULONG actualMethodNameSize;
	WCHAR typeNameBuffer[1024];
	ULONG actualTypeNameSize;
	WCHAR moduleNameBuffer[1024];
	ULONG actualModuleNameSize;
	WCHAR assemblyNameBuffer[1024];
	ULONG actualAssemblyNameSize;

	OutputDebugString(L"!!!!! Jit inlining");

	IfFailRet(this->corProfilerInfo->GetFunctionInfo(callerId, &classId, &moduleId, &methodDefToken));

	IfFailRet(this->corProfilerInfo->GetModuleInfo(moduleId, 0, 1024, &actualModuleNameSize, moduleNameBuffer, &assemblyId));

	
	CComPtr<IMetaDataImport> metadataImport;
	if (FAILED(corProfiler->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport))))
		OutputDebugStringW(L"Failed to get IMetadataImport {C++}");

	char str[1024];

	IfFailRet(metadataImport->GetMethodProps(methodDefToken, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0));

	IfFailRet(metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0));

	sprintf(str, "From (caller): %ls.%ls\r\n", typeNameBuffer, methodNameBuffer);

	OutputDebugStringA(str);



	IfFailRet(this->corProfilerInfo->GetFunctionInfo(calleeId, &classId, &moduleId, &methodDefToken));

	IfFailRet(this->corProfilerInfo->GetModuleInfo(moduleId, 0, 1024, &actualModuleNameSize, moduleNameBuffer, &assemblyId));

	

	if (FAILED(corProfiler->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport))))
		OutputDebugStringW(L"Failed to get IMetadataImport {C++}");


	IfFailRet(metadataImport->GetMethodProps(methodDefToken, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0));

	IfFailRet(metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0));

	sprintf(str, "To (callee): %ls.%ls\r\n", typeNameBuffer, methodNameBuffer);

	OutputDebugStringA(str);



    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadCreated(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadDestroyed(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadAssignedToOSThread(ThreadID managedThreadId, DWORD osThreadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientInvocationStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientSendingMessage(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientReceivingReply(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientInvocationFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerReceivingMessage(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerInvocationStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerInvocationReturned()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerSendingReply(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::UnmanagedToManagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ManagedToUnmanagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendStarted(COR_PRF_SUSPEND_REASON suspendReason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendAborted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeResumeStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeResumeFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeThreadSuspended(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeThreadResumed(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::MovedReferences(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], ULONG cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectAllocated(ObjectID objectId, ClassID classId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectsAllocatedByClass(ULONG cClassCount, ClassID classIds[], ULONG cObjects[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectReferences(ObjectID objectId, ClassID classId, ULONG cObjectRefs, ObjectID objectRefIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RootReferences(ULONG cRootRefs, ObjectID rootRefIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionThrown(ObjectID thrownObjectId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFunctionEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFunctionLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFilterEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFilterLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchCatcherFound(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionOSHandlerEnter(UINT_PTR __unused)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionOSHandlerLeave(UINT_PTR __unused)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFunctionEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFunctionLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFinallyEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFinallyLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCatcherEnter(FunctionID functionId, ObjectID objectId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCatcherLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::COMClassicVTableCreated(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable, ULONG cSlots)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::COMClassicVTableDestroyed(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCLRCatcherFound()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCLRCatcherExecute()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadNameChanged(ThreadID threadId, ULONG cchName, WCHAR name[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GarbageCollectionStarted(int cGenerations, BOOL generationCollected[], COR_PRF_GC_REASON reason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::SurvivingReferences(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], ULONG cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GarbageCollectionFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::FinalizeableObjectQueued(DWORD finalizerFlags, ObjectID objectID)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RootReferences2(ULONG cRootRefs, ObjectID rootRefIds[], COR_PRF_GC_ROOT_KIND rootKinds[], COR_PRF_GC_ROOT_FLAGS rootFlags[], UINT_PTR rootIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::HandleCreated(GCHandleID handleId, ObjectID initialObjectId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::HandleDestroyed(GCHandleID handleId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::InitializeForAttach(IUnknown *pCorProfilerInfoUnk, void *pvClientData, UINT cbClientData)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ProfilerAttachComplete()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ProfilerDetachSucceeded()
{
	Log(L"Profiler detach succeeded");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITCompilationStarted(FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl *pFunctionControl)
{
    HRESULT hr;

    ULONG actualModuleNameSize;
    WCHAR moduleNameBuffer[1024];
    AssemblyID assemblyId;
    if (FAILED(this->corProfilerInfo->GetModuleInfo(moduleId, 0, 1024, &actualModuleNameSize, moduleNameBuffer, &assemblyId)))
    {
        DebugOutput(L"[ReJIT] GetModuleInfo failed");
        return S_OK;
    }

    ULONG actualAssemblyNameSize;
    WCHAR assemblyNameBuffer[1024];
    if (FAILED(this->corProfilerInfo->GetAssemblyInfo(assemblyId, 1024, &actualAssemblyNameSize, assemblyNameBuffer, 0, 0)))
    {
        DebugOutput(L"[ReJIT] GetAssemblyInfo failed");
        return S_OK;
    }

    CComPtr<IMetaDataImport> metadataImport;
    if (FAILED(corProfiler->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown**>(&metadataImport))))
    {
        DebugOutput(L"[ReJIT] Failed to get IMetadataImport {C++}");
        return S_OK;
    }

    mdTypeDef typeDefToken;
    WCHAR methodNameBuffer[1024];
    ULONG actualMethodNameSize;
    if (FAILED(metadataImport->GetMethodProps(methodId, &typeDefToken, methodNameBuffer, 1024, &actualMethodNameSize, 0, 0, 0, 0, 0)))
    {
        DebugOutput(L"[ReJIT] GetMethodProps failed");
        return S_OK;
    }

    WCHAR typeNameBuffer[1024];
    ULONG actualTypeNameSize;
    if (FAILED(metadataImport->GetTypeDefProps(typeDefToken, typeNameBuffer, 1024, &actualTypeNameSize, 0, 0)))
    {
        DebugOutput(L"[ReJIT] GetTypeDefProps failed");
        return S_OK;
    }

    WCHAR jitDebug[1024];
    wsprintf(jitDebug, L"ReJIT of the method %ls.%ls\r\n", typeNameBuffer, methodNameBuffer);
    Log(jitDebug);

    LPCBYTE methodBody;
    IfFailRet(corProfilerInfo->GetILFunctionBody(moduleId, methodId, &methodBody, NULL));

    SharpResponse sharpResponse = SharpResponse();
    sharpResponse.newMethodBody = nullptr;
    sharpResponse = callback(assemblyNameBuffer, moduleNameBuffer, moduleId, methodId, (char*)methodBody, static_cast<void*>(&allocateForMethodBody));

    if (sharpResponse.newMethodBody != nullptr)
    {
        IfFailRet(pFunctionControl->SetILInstrumentedCodeMap(sharpResponse.mapEntriesCount, sharpResponse.pMapEntries));
        IfFailRet(pFunctionControl->SetILFunctionBody(sharpResponse.methodBodySize, sharpResponse.newMethodBody));
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::MovedReferences2(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::SurvivingReferences2(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ConditionalWeakTableElementReferences(ULONG cRootRefs, ObjectID keyRefIds[], ObjectID valueRefIds[], GCHandleID rootIds[])
{
    return S_OK;
}

