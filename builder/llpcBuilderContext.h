/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2019 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/
/**
 ***********************************************************************************************************************
 * @file  llpcBuilderContext.h
 * @brief LLPC header file: declaration of llpc::BuilderContext class for creating and using Llpc::Builder
 ***********************************************************************************************************************
 */
#pragma once

#include "llpc.h"
#include "llpcDebug.h"
#include "llpcTargetInfo.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm
{

class LLVMContext;
class Timer;

} // llvm

namespace Llpc
{

using namespace llvm;

class Builder;
class PassManager;
class PipelineState;

// =====================================================================================================================
// BuilderContext class, used to create Builder objects. State shared between Builder objects is kept here.
class BuilderContext
{
public:
    BuilderContext(LLVMContext& context, bool useBuilderRecorder);

    // Get LLVM context
    LLVMContext& GetContext() const { return m_context; }

    // Set target machine. Returns false on failure.
    bool SetTargetMachine(
        StringRef     gpuName);     // LLVM GPU name, e.g. "gfx900"

    // Get target machine.
    TargetMachine* GetTargetMachine() const
    {
        return &*m_pTargetMachine;
    }

    // Get target info
    const TargetInfo& GetTargetInfo() const
    {
        return m_targetInfo;
    }

    // Create a Builder object
    Builder* CreateBuilder();

    // Create a BuilderImpl object directly, passing in the PipelineState to use. This is used by BuilderReplayer.
    Builder* CreateBuilderImpl(PipelineState* pPipelineState);

    // Adds target passes to pass manager, depending on "-filetype" and "-emit-llvm" options
    void AddTargetPasses(PassManager& passMgr, Timer* pCodeGenTimer, raw_pwrite_stream& outStream);

private:
    LLPC_DISALLOW_DEFAULT_CTOR(BuilderContext)
    LLPC_DISALLOW_COPY_AND_ASSIGN(BuilderContext)

    // -----------------------------------------------------------------------------------------------------------------
    LLVMContext&                    m_context;              // LLVM context
    bool                            m_useBuilderRecorder;   // Whether to create BuilderRecorder or BuilderImpl
    std::unique_ptr<TargetMachine>  m_pTargetMachine;       // TargetMachine
    TargetInfo                      m_targetInfo = {};      // Target info
};

} // Llpc
