/*
 * Copyright (c) 1998-2014 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
															   * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "IOAudioDebug.h"
#include "IOAudioEngine.h"
#include "IOAudioStream.h"
#include "IOAudioTypes.h"

// Kernel FP registers are not preserved across a context switch (osfmk/arm64/
// cswitch.s:87-101), so the float add below runs with preemption disabled.
extern "C" {
void _disable_preemption(void);
void _enable_preemption(void);
}

// With preemption disabled a page fault cannot be served: vm_fault() returns
// KERN_FAILURE when get_preemption_level() != 0 (osfmk/vm/vm_fault.c:3983) and
// the abort handler panics ("Kernel data abort"). The client source buffer is a
// wired, lazily entered kernel mapping of user memory, and only its first page
// has been touched by registerClientBuffer64 (it reads the descriptor), so the
// first time the add loop reaches the second page it faulted (DARW-322). Touch
// every page of a range before disabling preemption so the loop never faults.
static inline void prefaultRange(const void *p, size_t bytes)
{
	const uintptr_t kStride = 4096;			// never larger than the VM page size, so no page is skipped
	uintptr_t a = (uintptr_t)p;
	uintptr_t end = a + bytes;
	if (bytes == 0) return;
	for (uintptr_t q = a; q < end; q = (q & ~(kStride - 1)) + kStride) {
		(void)*(volatile const uint8_t *)q;
	}
	(void)*(volatile const uint8_t *)(end - 1);
}


IOReturn IOAudioEngine::mixOutputSamples(const void *sourceBuf, void *mixBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
	IOReturn result = kIOReturnBadArgument;
	
    if (sourceBuf && mixBuf) 
	{
		const float * floatSource1Buf;
		const float * floatSource2Buf;
		float * floatMixBuf;
		
        UInt32 numSamplesLeft = numSampleFrames * streamFormat->fNumChannels;
		
		__darwin_size_t numSamps = numSamplesLeft;
 		
		floatMixBuf = &(((float *)mixBuf)[firstSampleFrame * streamFormat->fNumChannels]);
		floatSource2Buf = floatMixBuf;
		floatSource1Buf = (const float *)sourceBuf;
		
		__darwin_ptrdiff_t strideOne=1, strideTwo=1, resultStride=1;
		
		// vDSP_vadd (vecLib is userland-only, absent from the kernel SDK) with
		// unit strides is exactly resultBuf[i] = source1[i] + source2[i]; the
		// output may alias source2 (floatMixBuf == floatSource2Buf), which
		// element-wise in-order addition handles.
		prefaultRange(floatSource1Buf, numSamps * sizeof(float));
		prefaultRange(floatMixBuf, numSamps * sizeof(float));
		_disable_preemption();
		for (__darwin_size_t i = 0; i < numSamps; i++)
			floatMixBuf[i * resultStride] = floatSource1Buf[i * strideOne] + floatSource2Buf[i * strideTwo];
		_enable_preemption();
		
        result = kIOReturnSuccess;
    }
    
    return result;
}
