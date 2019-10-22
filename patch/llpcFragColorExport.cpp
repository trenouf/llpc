/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All Rights Reserved.
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
 * @file  llpcFragColorExport.cpp
 * @brief LLPC source file: contains implementation of class Llpc::FragColorExport.
 ***********************************************************************************************************************
 */
#define DEBUG_TYPE "llpc-frag-color-export"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "llpcContext.h"
#include "llpcDebug.h"
#include "llpcFragColorExport.h"
#include "llpcIntrinsDefs.h"
#include "llpcPipelineState.h"

using namespace llvm;

namespace Llpc
{

// =====================================================================================================================
FragColorExport::FragColorExport(
    PipelineState*  pPipelineState) // [in] Pipeline state
    :
    m_pModule(pPipelineState->GetModule()),
    m_pContext(static_cast<Context*>(&m_pModule->getContext())),
    m_pPipelineState(pPipelineState)
{
}

// =====================================================================================================================
// Executes fragment color export operations based on the specified output type and its location.
Value* FragColorExport::Run(
    Value*       pOutput,       // [in] Fragment color output
    uint32_t     location,      // Location of fragment color output
    Instruction* pInsertPos)    // [in] Where to insert fragment color export instructions
{
    auto pResUsage = m_pContext->GetShaderResourceUsage(ShaderStageFragment);

    Type* pOutputTy = pOutput->getType();
    const uint32_t origLoc = pResUsage->inOutUsage.fs.outputOrigLocs[location];

    ExportFormat expFmt = EXP_FORMAT_ZERO;
    if (m_pPipelineState->GetColorExportState().dualSourceBlendEnable)
    {
        // Dual source blending is enabled
        expFmt= ComputeExportFormat(pOutputTy, 0);
    }
    else
    {
        expFmt = ComputeExportFormat(pOutputTy, origLoc);
    }

    pResUsage->inOutUsage.fs.expFmts[location] = expFmt;
    if (expFmt == EXP_FORMAT_ZERO)
    {
        // Clear channel mask if shader export format is ZERO
        pResUsage->inOutUsage.fs.cbShaderMask &= ~(0xF << (4 * origLoc));
    }

    const uint32_t bitWidth = pOutputTy->getScalarSizeInBits();
    BasicType outputType = pResUsage->inOutUsage.fs.outputTypes[origLoc];
    const bool signedness = ((outputType == BasicType::Int8) ||
                             (outputType == BasicType::Int16) ||
                             (outputType == BasicType::Int));

    auto pCompTy = pOutputTy->isVectorTy() ? pOutputTy->getVectorElementType() : pOutputTy;
    uint32_t compCount = pOutputTy->isVectorTy() ? pOutputTy->getVectorNumElements() : 1;

    Value* comps[4] = { nullptr };
    if (compCount == 1)
    {
        comps[0] = pOutput;
    }
    else
    {
        for (uint32_t i = 0; i < compCount; ++i)
        {
            comps[i] = ExtractElementInst::Create(pOutput,
                                                  ConstantInt::get(m_pContext->Int32Ty(), i),
                                                  "",
                                                  pInsertPos);
        }
    }

    bool comprExp = false;
    bool needPack = false;

    std::vector<Value*> args;

    const auto pUndefFloat     = UndefValue::get(m_pContext->FloatTy());
    const auto pUndefFloat16   = UndefValue::get(m_pContext->Float16Ty());
    const auto pUndefFloat16x2 = UndefValue::get(m_pContext->Float16x2Ty());

    switch (expFmt)
    {
    case EXP_FORMAT_ZERO:
        {
            break;
        }
    case EXP_FORMAT_32_R:
        {
            compCount = 1;
            comps[0] = ConvertToFloat(comps[0], signedness, pInsertPos);
            comps[1] = pUndefFloat;
            comps[2] = pUndefFloat;
            comps[3] = pUndefFloat;
            break;
        }
    case EXP_FORMAT_32_GR:
        {
            if (compCount >= 2)
            {
                compCount = 2;
                comps[0] = ConvertToFloat(comps[0], signedness, pInsertPos);
                comps[1] = ConvertToFloat(comps[1], signedness, pInsertPos);
                comps[2] = pUndefFloat;
                comps[3] = pUndefFloat;
            }
            else
            {
                compCount = 1;
                comps[0] = ConvertToFloat(comps[0], signedness, pInsertPos);
                comps[1] = pUndefFloat;
                comps[2] = pUndefFloat;
                comps[3] = pUndefFloat;
            }
            break;
        }
    case EXP_FORMAT_32_AR:
        {
            if (compCount == 4)
            {
                compCount = 2;
                comps[0] = ConvertToFloat(comps[0], signedness, pInsertPos);
                comps[1] = ConvertToFloat(comps[3], signedness, pInsertPos);
                comps[2] = pUndefFloat;
                comps[3] = pUndefFloat;
            }
            else
            {
                compCount = 1;
                comps[0] = ConvertToFloat(comps[0], signedness, pInsertPos);
                comps[1] = pUndefFloat;
                comps[2] = pUndefFloat;
                comps[3] = pUndefFloat;
            }
            break;
        }
    case EXP_FORMAT_32_ABGR:
       {
            for (uint32_t i = 0; i < compCount; ++i)
            {
                comps[i] = ConvertToFloat(comps[i], signedness, pInsertPos);
            }

            for (uint32_t i = compCount; i < 4; ++i)
            {
                comps[i] = pUndefFloat;
            }
            break;
        }
    case EXP_FORMAT_FP16_ABGR:
        {
            comprExp = true;

            if (bitWidth == 8)
            {
                needPack = true;

                // Cast i8 to float16
                LLPC_ASSERT(pCompTy->isIntegerTy());
                for (uint32_t i = 0; i < compCount; ++i)
                {
                    if (signedness)
                    {
                        // %comp = sext i8 %comp to i16
                        comps[i] = new SExtInst(comps[i], m_pContext->Int16Ty(), "", pInsertPos);
                    }
                    else
                    {
                        // %comp = zext i8 %comp to i16
                        comps[i] = new ZExtInst(comps[i], m_pContext->Int16Ty(), "", pInsertPos);
                    }

                    // %comp = bitcast i16 %comp to half
                    comps[i] = new BitCastInst(comps[i], m_pContext->Float16Ty(), "", pInsertPos);
                }

                for (uint32_t i = compCount; i < 4; ++i)
                {
                    comps[i] = pUndefFloat16;
                }
            }
            else if (bitWidth == 16)
            {
                needPack = true;

                if (pCompTy->isIntegerTy())
                {
                    // Cast i16 to float16
                    for (uint32_t i = 0; i < compCount; ++i)
                    {
                        // %comp = bitcast i16 %comp to half
                        comps[i] = new BitCastInst(comps[i], m_pContext->Float16Ty(), "", pInsertPos);
                    }
                }

                for (uint32_t i = compCount; i < 4; ++i)
                {
                    comps[i] = pUndefFloat16;
                }
            }
            else
            {
                if (pCompTy->isIntegerTy())
                {
                    // Cast i32 to float
                    for (uint32_t i = 0; i < compCount; ++i)
                    {
                        // %comp = bitcast i32 %comp to float
                        comps[i] = new BitCastInst(comps[i], m_pContext->FloatTy(), "", pInsertPos);
                    }
                }

                for (uint32_t i = compCount; i < 4; ++i)
                {
                    comps[i] = pUndefFloat;
                }

                std::vector<Attribute::AttrKind> attribs;
                attribs.push_back(Attribute::ReadNone);

                // Do packing
                args.clear();
                args.push_back(comps[0]);
                args.push_back(comps[1]);
                comps[0] = EmitCall(m_pModule,
                                    "llvm.amdgcn.cvt.pkrtz",
                                    m_pContext->Float16x2Ty(),
                                    args,
                                    attribs,
                                    pInsertPos);

                if (compCount > 2)
                {
                    args.clear();
                    args.push_back(comps[2]);
                    args.push_back(comps[3]);
                    comps[1] = EmitCall(m_pModule,
                                        "llvm.amdgcn.cvt.pkrtz",
                                        m_pContext->Float16x2Ty(),
                                        args,
                                        attribs,
                                        pInsertPos);
                }
                else
                {
                    comps[1] = pUndefFloat16x2;
                }
            }

            break;
        }
    case EXP_FORMAT_UNORM16_ABGR:
    case EXP_FORMAT_SNORM16_ABGR:
        {
            comprExp = true;
            needPack = true;

            for (uint32_t i = 0; i < compCount; ++i)
            {
                // Convert the components to float value if necessary
                comps[i] = ConvertToFloat(comps[i], signedness, pInsertPos);
            }

            LLPC_ASSERT(compCount <= 4);
            // Make even number of components;
            if ((compCount % 2) != 0)
            {
                comps[compCount] = ConstantFP::get(m_pContext->FloatTy(), 0.0);
                compCount++;
            }

            StringRef funcName = (expFmt == EXP_FORMAT_SNORM16_ABGR) ?
                ("llvm.amdgcn.cvt.pknorm.i16") : ("llvm.amdgcn.cvt.pknorm.u16");

            for (uint32_t i = 0; i < compCount; i += 2)
            {
                args.clear();
                args.push_back(comps[i]);
                args.push_back(comps[i + 1]);
                Value* pComps = EmitCall(m_pModule,
                                            funcName,
                                            m_pContext->Int16x2Ty(),
                                            args,
                                            NoAttrib,
                                            pInsertPos);

                pComps = new BitCastInst(pComps, m_pContext->Float16x2Ty(), "", pInsertPos);

                comps[i] = ExtractElementInst::Create(pComps,
                                                        ConstantInt::get(m_pContext->Int32Ty(), 0),
                                                        "",
                                                        pInsertPos);

                comps[i + 1] = ExtractElementInst::Create(pComps,
                                                            ConstantInt::get(m_pContext->Int32Ty(), 1),
                                                            "",
                                                            pInsertPos);

            }

            for (uint32_t i = compCount; i < 4; ++i)
            {
                comps[i] = pUndefFloat16;
            }

            break;
        }
    case EXP_FORMAT_UINT16_ABGR:
    case EXP_FORMAT_SINT16_ABGR:
        {
            comprExp = true;
            needPack = true;

            for (uint32_t i = 0; i < compCount; ++i)
            {
                // Convert the components to int value if necessary
                comps[i] = ConvertToInt(comps[i], signedness, pInsertPos);
            }

            LLPC_ASSERT(compCount <= 4);
            // Make even number of components;
            if ((compCount % 2) != 0)
            {
                comps[compCount] = ConstantInt::get(m_pContext->Int32Ty(), 0),
                compCount++;
            }

            StringRef funcName = (expFmt == EXP_FORMAT_SINT16_ABGR) ?
                ("llvm.amdgcn.cvt.pk.i16") : ("llvm.amdgcn.cvt.pk.u16");

            for (uint32_t i = 0; i < compCount; i += 2)
            {
                args.clear();
                args.push_back(comps[i]);
                args.push_back(comps[i + 1]);
                Value* pComps = EmitCall(m_pModule,
                                            funcName,
                                            m_pContext->Int16x2Ty(),
                                            args,
                                            NoAttrib,
                                            pInsertPos);

                pComps = new BitCastInst(pComps, m_pContext->Float16x2Ty(), "", pInsertPos);

                comps[i] = ExtractElementInst::Create(pComps,
                                                        ConstantInt::get(m_pContext->Int32Ty(), 0),
                                                        "",
                                                        pInsertPos);

                comps[i + 1] = ExtractElementInst::Create(pComps,
                                                            ConstantInt::get(m_pContext->Int32Ty(), 1),
                                                            "",
                                                            pInsertPos);
            }

            for (uint32_t i = compCount; i < 4; ++i)
            {
                comps[i] = pUndefFloat16;
            }

            break;
        }
    default:
        {
            LLPC_NEVER_CALLED();
            break;
        }
    }

    Value* pExport = nullptr;

    if (expFmt == EXP_FORMAT_ZERO)
    {
        // Do nothing
    }
    else if (comprExp)
    {
        // 16-bit export (compressed)
        if (needPack)
        {
            // Do packing

            // %comp[0] = insertelement <2 x half> undef, half %comp[0], i32 0
            comps[0] = InsertElementInst::Create(pUndefFloat16x2,
                                                 comps[0],
                                                 ConstantInt::get(m_pContext->Int32Ty(), 0),
                                                 "",
                                                 pInsertPos);

            // %comp[0] = insertelement <2 x half> %comp[0], half %comp[1], i32 1
            comps[0] = InsertElementInst::Create(comps[0],
                                                 comps[1],
                                                 ConstantInt::get(m_pContext->Int32Ty(), 1),
                                                 "",
                                                 pInsertPos);

            if (compCount > 2)
            {
                // %comp[1] = insertelement <2 x half> undef, half %comp[2], i32 0
                comps[1] = InsertElementInst::Create(pUndefFloat16x2,
                                                     comps[2],
                                                     ConstantInt::get(m_pContext->Int32Ty(), 0),
                                                     "",
                                                     pInsertPos);

                // %comp[1] = insertelement <2 x half> %comp[1], half %comp[3], i32 1
                comps[1] = InsertElementInst::Create(comps[1],
                                                     comps[3],
                                                     ConstantInt::get(m_pContext->Int32Ty(), 1),
                                                     "",
                                                     pInsertPos);
            }
            else
            {
                comps[1] = pUndefFloat16x2;
            }
        }

        args.clear();
        args.push_back(ConstantInt::get(m_pContext->Int32Ty(), EXP_TARGET_MRT_0 + location)); // tgt
        args.push_back(ConstantInt::get(m_pContext->Int32Ty(), (compCount > 2) ? 0xF : 0x3)); // en
        args.push_back(comps[0]);                                                             // src0
        args.push_back(comps[1]);                                                             // src1
        args.push_back(ConstantInt::get(m_pContext->BoolTy(), false));                        // done
        args.push_back(ConstantInt::get(m_pContext->BoolTy(), true));                         // vm

        pExport = EmitCall(m_pModule, "llvm.amdgcn.exp.compr.v2f16", m_pContext->VoidTy(), args, NoAttrib, pInsertPos);
    }
    else
    {
        // 32-bit export
        args.clear();
        args.push_back(ConstantInt::get(m_pContext->Int32Ty(), EXP_TARGET_MRT_0 + location)); // tgt
        args.push_back(ConstantInt::get(m_pContext->Int32Ty(), (1 << compCount) - 1));        // en
        args.push_back(comps[0]);                                                             // src0
        args.push_back(comps[1]);                                                             // src1
        args.push_back(comps[2]);                                                             // src2
        args.push_back(comps[3]);                                                             // src3
        args.push_back(ConstantInt::get(m_pContext->BoolTy(), false));                        // done
        args.push_back(ConstantInt::get(m_pContext->BoolTy(), true));                         // vm

        pExport = EmitCall(m_pModule, "llvm.amdgcn.exp.f32", m_pContext->VoidTy(), args, NoAttrib, pInsertPos);
    }

    return pExport;
}

// =====================================================================================================================
// Determines the shader export format for a particular fragment color output. Value should be used to do programming
// for SPI_SHADER_COL_FORMAT.
ExportFormat FragColorExport::ComputeExportFormat(
    Type*    pOutputTy,  // [in] Type of fragment data output
    uint32_t location    // Location of fragment data output
    ) const
{
    const auto pCbState = &m_pPipelineState->GetColorExportState();
    const auto pTarget = &m_pPipelineState->GetColorExportFormat(location);

    const bool blendEnabled = pTarget->blendEnable;

    const bool shaderExportsAlpha = (pOutputTy->isVectorTy() && (pOutputTy->getVectorNumElements() == 4));

    // NOTE: Alpha-to-coverage only cares at the output from target #0.
    const bool enableAlphaToCoverage = (pCbState->alphaToCoverageEnable && (location == 0));

    const bool isUnorm = (pTarget->nfmt == Builder::BufNumFormatUNORM);
    const bool isSnorm = (pTarget->nfmt == Builder::BufNumFormatSNORM);
    bool isFloat = (pTarget->nfmt == Builder::BufNumFormatFLOAT);
    const bool isUint = (pTarget->nfmt == Builder::BufNumFormatUINT);
    const bool isSint = (pTarget->nfmt == Builder::BufNumFormatSINT);
    const bool isSrgb = (pTarget->nfmt == Builder::BufNumFormatSRGB);

    if ((pTarget->dfmt == Builder::BufDataFormat8_8_8) || (pTarget->dfmt == Builder::BufDataFormat8_8_8_BGR))
    {
        // These three-byte formats are handled by pretending they are float.
        isFloat = true;
    }

    const uint32_t maxCompBitCount = GetMaxComponentBitCount(pTarget->dfmt);

    const bool hasAlpha = HasAlpha(pTarget->dfmt);
    const bool alphaExport = (shaderExportsAlpha &&
                              (hasAlpha || pTarget->blendSrcAlphaToColor || enableAlphaToCoverage));

    const CompSetting compSetting = ComputeCompSetting(pTarget->dfmt);

    // Start by assuming EXP_FORMAT_ZERO (no exports)
    ExportFormat expFmt = EXP_FORMAT_ZERO;

    GfxIpVersion gfxIp = m_pPipelineState->GetGfxIpVersion();
    auto pGpuWorkarounds = m_pPipelineState->GetGpuWorkarounds();

    bool gfx8RbPlusEnable = false;
    if ((gfxIp.major == 8) && (gfxIp.minor == 1))
    {
        gfx8RbPlusEnable = true;
    }

    if (pTarget->dfmt == Builder::BufDataFormatInvalid)
    {
        expFmt = EXP_FORMAT_ZERO;
    }
    else if ((compSetting == CompSetting::OneCompRed) &&
             (alphaExport == false)                   &&
             (isSrgb == false)                        &&
             ((gfx8RbPlusEnable == false) || (maxCompBitCount == 32)))
    {
        // NOTE: When Rb+ is enabled, "R8 UNORM" and "R16 UNORM" shouldn't use "EXP_FORMAT_32_R", instead
        // "EXP_FORMAT_FP16_ABGR" and "EXP_FORMAT_UNORM16_ABGR" should be used for 2X exporting performance.
        expFmt = EXP_FORMAT_32_R;
    }
    else if (((isUnorm || isSnorm) && (maxCompBitCount <= 10)) ||
             (isFloat && (maxCompBitCount <= 16)) ||
             (isSrgb && (maxCompBitCount == 8)))
    {
        expFmt = EXP_FORMAT_FP16_ABGR;
    }
    else if (isSint &&
             ((maxCompBitCount == 16) ||
              ((pGpuWorkarounds->gfx6.cbNoLt16BitIntClamp == false) && (maxCompBitCount < 16))) &&
             (enableAlphaToCoverage == false))
    {
        // NOTE: On some hardware, the CB will not properly clamp its input if the shader export format is "UINT16"
        // "SINT16" and the CB format is less than 16 bits per channel. On such hardware, the workaround is picking
        // an appropriate 32-bit export format. If this workaround isn't necessary, then we can choose this higher
        // performance 16-bit export format in this case.
        expFmt = EXP_FORMAT_SINT16_ABGR;
    }
    else if (isSnorm && (maxCompBitCount == 16) && (blendEnabled == false))
    {
        expFmt = EXP_FORMAT_SNORM16_ABGR;
    }
    else if (isUint &&
             ((maxCompBitCount == 16) ||
              ((pGpuWorkarounds->gfx6.cbNoLt16BitIntClamp == false) && (maxCompBitCount < 16))) &&
             (enableAlphaToCoverage == false))
    {
        // NOTE: On some hardware, the CB will not properly clamp its input if the shader export format is "UINT16"
        // "SINT16" and the CB format is less than 16 bits per channel. On such hardware, the workaround is picking
        // an appropriate 32-bit export format. If this workaround isn't necessary, then we can choose this higher
        // performance 16-bit export format in this case.
        expFmt = EXP_FORMAT_UINT16_ABGR;
    }
    else if (isUnorm && (maxCompBitCount == 16) && (blendEnabled == false))
    {
        expFmt = EXP_FORMAT_UNORM16_ABGR;
    }
    else if (((isUint || isSint) ||
              (isFloat && (maxCompBitCount > 16)) ||
              ((isUnorm || isSnorm) && (maxCompBitCount == 16)))  &&
             ((compSetting == CompSetting::OneCompRed) ||
              (compSetting == CompSetting::OneCompAlpha) ||
              (compSetting == CompSetting::TwoCompAlphaRed)))
    {
        expFmt = EXP_FORMAT_32_AR;
    }
    else if (((isUint || isSint) ||
              (isFloat && (maxCompBitCount > 16)) ||
              ((isUnorm || isSnorm) && (maxCompBitCount == 16)))  &&
             (compSetting == CompSetting::TwoCompGreenRed) && (alphaExport == false))
    {
        expFmt = EXP_FORMAT_32_GR;
    }
    else if (((isUnorm || isSnorm) && (maxCompBitCount == 16)) ||
             (isUint || isSint) ||
             (isFloat && (maxCompBitCount >  16)))
    {
        expFmt = EXP_FORMAT_32_ABGR;
    }

    return expFmt;
}

// =====================================================================================================================
// This is the helper function for the algorithm to determine the shader export format.
CompSetting FragColorExport::ComputeCompSetting(
    Builder::BufDataFormat dfmt // Color attachment data format
    ) const
{
    CompSetting compSetting = CompSetting::Invalid;
    switch (GetNumChannels(dfmt))
    {
    case 1:
        compSetting = CompSetting::OneCompRed;
        break;
    case 2:
        compSetting = CompSetting::TwoCompGreenRed;
        break;
    }
    return compSetting;
}

// =====================================================================================================================
// Get the number of channels
uint32_t FragColorExport::GetNumChannels(
    Builder::BufDataFormat dfmt // Color attachment data format
    ) const
{
    switch (dfmt)
    {
    case Builder::BufDataFormatInvalid:
    case Builder::BufDataFormatReserved:
    case Builder::BufDataFormat8:
    case Builder::BufDataFormat16:
    case Builder::BufDataFormat32:
    case Builder::BufDataFormat64:
        return 1;
    case Builder::BufDataFormat4_4:
    case Builder::BufDataFormat8_8:
    case Builder::BufDataFormat16_16:
    case Builder::BufDataFormat32_32:
    case Builder::BufDataFormat64_64:
        return 2;
    case Builder::BufDataFormat8_8_8:
    case Builder::BufDataFormat8_8_8_BGR:
    case Builder::BufDataFormat10_11_11:
    case Builder::BufDataFormat11_11_10:
    case Builder::BufDataFormat32_32_32:
    case Builder::BufDataFormat64_64_64:
    case Builder::BufDataFormat5_6_5:
    case Builder::BufDataFormat5_6_5_BGR:
        return 3;
    case Builder::BufDataFormat10_10_10_2:
    case Builder::BufDataFormat2_10_10_10:
    case Builder::BufDataFormat8_8_8_8:
    case Builder::BufDataFormat16_16_16_16:
    case Builder::BufDataFormat32_32_32_32:
    case Builder::BufDataFormat8_8_8_8_BGRA:
    case Builder::BufDataFormat2_10_10_10_BGRA:
    case Builder::BufDataFormat64_64_64_64:
    case Builder::BufDataFormat4_4_4_4:
    case Builder::BufDataFormat4_4_4_4_BGRA:
    case Builder::BufDataFormat5_6_5_1:
    case Builder::BufDataFormat5_6_5_1_BGRA:
    case Builder::BufDataFormat1_5_6_5:
    case Builder::BufDataFormat5_9_9_9:
        return 4;
    }
    return 0;
}

// =====================================================================================================================
// Checks whether the alpha channel is present in the specified color attachment format.
bool FragColorExport::HasAlpha(
    Builder::BufDataFormat dfmt // Color attachment data format
    ) const
{
    switch (dfmt)
    {
    case Builder::BufDataFormat10_10_10_2:
    case Builder::BufDataFormat2_10_10_10:
    case Builder::BufDataFormat8_8_8_8:
    case Builder::BufDataFormat16_16_16_16:
    case Builder::BufDataFormat32_32_32_32:
    case Builder::BufDataFormat8_8_8_8_BGRA:
    case Builder::BufDataFormat2_10_10_10_BGRA:
    case Builder::BufDataFormat64_64_64_64:
    case Builder::BufDataFormat4_4_4_4:
    case Builder::BufDataFormat4_4_4_4_BGRA:
    case Builder::BufDataFormat5_6_5_1:
    case Builder::BufDataFormat5_6_5_1_BGRA:
    case Builder::BufDataFormat1_5_6_5:
    case Builder::BufDataFormat5_9_9_9:
        return true;
    default:
        return false;
    }
}

// =====================================================================================================================
// Gets the maximum bit-count of any component in specified color attachment format.
uint32_t FragColorExport::GetMaxComponentBitCount(
    Builder::BufDataFormat dfmt // Color attachment data format
    ) const
{
    switch (dfmt)
    {
    case Builder::BufDataFormatInvalid:
    case Builder::BufDataFormatReserved:
        return 0;
    case Builder::BufDataFormat4_4:
    case Builder::BufDataFormat4_4_4_4:
    case Builder::BufDataFormat4_4_4_4_BGRA:
        return 4;
    case Builder::BufDataFormat5_6_5:
    case Builder::BufDataFormat5_6_5_BGR:
    case Builder::BufDataFormat5_6_5_1:
    case Builder::BufDataFormat5_6_5_1_BGRA:
    case Builder::BufDataFormat1_5_6_5:
        return 6;
    case Builder::BufDataFormat8:
    case Builder::BufDataFormat8_8:
    case Builder::BufDataFormat8_8_8:
    case Builder::BufDataFormat8_8_8_BGR:
    case Builder::BufDataFormat8_8_8_8:
    case Builder::BufDataFormat8_8_8_8_BGRA:
        return 8;
    case Builder::BufDataFormat5_9_9_9:
        return 9;
    case Builder::BufDataFormat10_10_10_2:
    case Builder::BufDataFormat2_10_10_10:
    case Builder::BufDataFormat2_10_10_10_BGRA:
        return 10;
    case Builder::BufDataFormat10_11_11:
    case Builder::BufDataFormat11_11_10:
        return 11;
    case Builder::BufDataFormat16:
    case Builder::BufDataFormat16_16:
    case Builder::BufDataFormat16_16_16_16:
        return 16;
    case Builder::BufDataFormat32:
    case Builder::BufDataFormat32_32:
    case Builder::BufDataFormat32_32_32:
    case Builder::BufDataFormat32_32_32_32:
        return 32;
    case Builder::BufDataFormat64:
    case Builder::BufDataFormat64_64:
    case Builder::BufDataFormat64_64_64:
    case Builder::BufDataFormat64_64_64_64:
        return 64;
    }
    return 0;
}

// =====================================================================================================================
// Converts an output component value to its floating-point representation. This function is a "helper" in computing
// the export value based on shader export format.
Value* FragColorExport::ConvertToFloat(
    Value*       pValue,        // [in] Output component value
    bool         signedness,    // Whether the type is signed (valid for integer type)
    Instruction* pInsertPos     // [in] Where to insert conversion instructions
    ) const
{
    Type* pValueTy = pValue->getType();
    LLPC_ASSERT(pValueTy->isFloatingPointTy() || pValueTy->isIntegerTy()); // Should be floating-point/integer scalar

    const uint32_t bitWidth = pValueTy->getScalarSizeInBits();
    if (bitWidth == 8)
    {
        LLPC_ASSERT(pValueTy->isIntegerTy());
        if (signedness)
        {
            // %value = sext i8 %value to i32
            pValue = new SExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }
        else
        {
            // %value = zext i8 %value to i32
            pValue = new ZExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }

        // %value = bitcast i32 %value to float
        pValue = new BitCastInst(pValue, m_pContext->FloatTy(), "", pInsertPos);
    }
    else if (bitWidth == 16)
    {
        if (pValueTy->isFloatingPointTy())
        {
            // %value = fpext half %value to float
            pValue = new FPExtInst(pValue, m_pContext->FloatTy(), "", pInsertPos);
        }
        else
        {
            if (signedness)
            {
                // %value = sext i16 %value to i32
                pValue = new SExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
            }
            else
            {
                // %value = zext i16 %value to i32
                pValue = new ZExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
            }

            // %value = bitcast i32 %value to float
            pValue = new BitCastInst(pValue, m_pContext->FloatTy(), "", pInsertPos);
        }
    }
    else
    {
        LLPC_ASSERT(bitWidth == 32); // The valid bit width is 16 or 32
        if (pValueTy->isIntegerTy())
        {
            // %value = bitcast i32 %value to float
            pValue = new BitCastInst(pValue, m_pContext->FloatTy(), "", pInsertPos);
        }
    }

    return pValue;
}

// =====================================================================================================================
// Converts an output component value to its integer representation. This function is a "helper" in computing the
// export value based on shader export format.
Value* FragColorExport::ConvertToInt(
    Value*       pValue,        // [in] Output component value
    bool         signedness,    // Whether the type is signed (valid for integer type)
    Instruction* pInsertPos     // [in] Where to insert conversion instructions
    ) const
{
    Type* pValueTy = pValue->getType();
    LLPC_ASSERT(pValueTy->isFloatingPointTy() || pValueTy->isIntegerTy()); // Should be floating-point/integer scalar

    const uint32_t bitWidth = pValueTy->getScalarSizeInBits();
    if (bitWidth == 8)
    {
        LLPC_ASSERT(pValueTy->isIntegerTy());

        if (signedness)
        {
            // %value = sext i8 %value to i32
            pValue = new SExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }
        else
        {
            // %value = zext i8 %value to i32
            pValue = new ZExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }
    }
    else if (bitWidth == 16)
    {
        if (pValueTy->isFloatingPointTy())
        {
            // %value = bicast half %value to i16
            pValue = new BitCastInst(pValue, m_pContext->Int16Ty(), "", pInsertPos);
        }

        if (signedness)
        {
            // %value = sext i16 %value to i32
            pValue = new SExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }
        else
        {
            // %value = zext i16 %value to i32
            pValue = new ZExtInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }
    }
    else
    {
        LLPC_ASSERT(bitWidth == 32); // The valid bit width is 16 or 32
        if (pValueTy->isFloatingPointTy())
        {
            // %value = bitcast float %value to i32
            pValue = new BitCastInst(pValue, m_pContext->Int32Ty(), "", pInsertPos);
        }
    }

    return pValue;
}

} // Llpc
