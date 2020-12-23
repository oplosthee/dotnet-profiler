#pragma once

#include "CorProfiler.h"
#include <map>

FunctionID FunctionMapper(FunctionID funcId, BOOL* pbHookFunction);

#ifdef _WIN64

EXTERN_C void FunctionEnterNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_INFO* argumentInfo);
EXTERN_C void FunctionLeaveNaked(FunctionID funcId, UINT_PTR clientdata, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_RANGE* retvalRange);
EXTERN_C void FunctionTailcallNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func);

#else

void FunctionEnterNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_INFO* argumentInfo);
void FunctionLeaveNaked(FunctionID funcId, UINT_PTR clientdata, COR_PRF_FRAME_INFO func, COR_PRF_FUNCTION_ARGUMENT_RANGE* retvalRange);
void FunctionTailcallNaked(FunctionID funcId, UINT_PTR clientData, COR_PRF_FRAME_INFO func);

#endif
