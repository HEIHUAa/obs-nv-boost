#pragma once

#include <obs-module.h>

/* nvEncodeAPI.h 会以非 LEAN 方式包含 windows.h，导致 guiddef.h 在 INITGUID
 * 生效前定型（DEFINE_GUID 退化为 extern 声明，IID_ 符号无人定义），
 * 必须先定义 WIN32_LEAN_AND_MEAN 再包含 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif
#include <ffnvcodec/nvEncodeAPI.h>

#include <ffnvcodec/dynlink_cuda.h>

/* Missing from FFmpeg headers */
typedef CUresult CUDAAPI tcuMemHostRegister(void *p, size_t bytesize, unsigned int Flags);
typedef CUresult CUDAAPI tcuMemHostUnregister(void *p);

/* Missing from FFmpeg headers prior to SDK 11.0 */
#if NVENCAPI_MAJOR_VERSION < 11
typedef CUresult CUDAAPI tcuArray3DCreate(CUarray *pHandle, const CUDA_ARRAY3D_DESCRIPTOR *pAllocateArray);
typedef CUresult CUDAAPI tcuArrayDestroy(CUarray hArray);
#endif

#define CUDA_ERROR_INVALID_GRAPHICS_CONTEXT 219
#define CUDA_ARRAY3D_SURFACE_LDST 0x02

typedef struct CudaFunctions {
	tcuInit *cuInit;

	tcuDeviceGetCount *cuDeviceGetCount;
	tcuDeviceGet *cuDeviceGet;
	tcuDeviceGetAttribute *cuDeviceGetAttribute;

	tcuCtxCreate_v2 *cuCtxCreate;
	tcuCtxDestroy_v2 *cuCtxDestroy;
	tcuCtxPushCurrent_v2 *cuCtxPushCurrent;
	tcuCtxPopCurrent_v2 *cuCtxPopCurrent;

	tcuArray3DCreate *cuArray3DCreate;
	tcuArrayDestroy *cuArrayDestroy;
	tcuMemcpy2D_v2 *cuMemcpy2D;

	tcuGetErrorName *cuGetErrorName;
	tcuGetErrorString *cuGetErrorString;

	tcuMemHostRegister *cuMemHostRegister;
	tcuMemHostUnregister *cuMemHostUnregister;

#ifndef _WIN32
	tcuGLGetDevices_v2 *cuGLGetDevices;
	tcuGraphicsGLRegisterImage *cuGraphicsGLRegisterImage;
	tcuGraphicsUnregisterResource *cuGraphicsUnregisterResource;
	tcuGraphicsMapResources *cuGraphicsMapResources;
	tcuGraphicsUnmapResources *cuGraphicsUnmapResources;
	tcuGraphicsSubResourceGetMappedArray *cuGraphicsSubResourceGetMappedArray;
#endif
} CudaFunctions;

extern CudaFunctions *cu;

bool init_cuda(obs_encoder_t *encoder);
bool cuda_get_error_desc(CUresult res, const char **name, const char **desc);

struct nvenc_data;
bool cuda_error_check(struct nvenc_data *enc, CUresult res, const char *func, const char *call);

/* CUDA error handling */
#define CU_FAILED(call)                                        \
	if (!cuda_error_check(enc, call, __FUNCTION__, #call)) \
		return false;

#define CU_CHECK(call)                                           \
	if (!cuda_error_check(enc, call, __FUNCTION__, #call)) { \
		success = false;                                 \
		goto unmap;                                      \
	}
