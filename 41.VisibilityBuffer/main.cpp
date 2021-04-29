// Copyright (C) 2018-2020 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#define _NBL_STATIC_LIB_
#include <iostream>
#include <cstdio>
#include <nabla.h>

//! I advise to check out this file, its a basic input handler
#include "../common/QToQuitEventReceiver.h"
#include "nbl/asset/utils/CCPUMeshPackerV1.h"
#include "nbl/asset/CCPUMeshPackerV2.h"
#include "nbl/video/CGPUMeshPackerV2.h"

using namespace nbl;
using namespace nbl::core;
using namespace nbl::asset;
using namespace nbl::video;

#include "common.h"

constexpr uint32_t TEX_OF_INTEREST_CNT = 6u; 
#include "nbl/nblpack.h"
struct BatchInstanceData
{
    union
    {
        //Ka
        vector3df_SIMD ambient;
        struct
        {
            uint32_t invalid_0[3];
            uint32_t baseVertex;
        };
    };
    union
    {
        //Kd
        vector3df_SIMD diffuse;
        struct
        {
            uint32_t invalid_1[3];
            uint32_t vAttrPos;
        };
    };
    union
    {
        //Ks
        vector3df_SIMD specular;
        struct
        {
            uint32_t invalid_2[3];
            uint32_t vAttrUV;
        };
    };
    union
    {
        //Ke
        vector3df_SIMD emissive;
        struct
        {
            uint32_t invalid_3[3];
            uint32_t vAttrNormal;
        };
    };
    uint64_t map_data[TEX_OF_INTEREST_CNT];
    //Ns, specular exponent in phong model
    float shininess = 32.f;
    //d
    float opacity = 1.f;
    //Ni, index of refraction
    float IoR = 1.6f;
    uint32_t extra;
} PACK_STRUCT;
#include "nbl/nblunpack.h"
static_assert(sizeof(BatchInstanceData) <= asset::ICPUMeshBuffer::MAX_PUSH_CONSTANT_BYTESIZE, "doesnt fit in push constants");

//mesh packing stuff
struct DrawIndexedIndirectInput
{
    size_t offset = 0u;
    size_t maxCount = 0u;

    static constexpr asset::E_PRIMITIVE_TOPOLOGY mode = asset::EPT_TRIANGLE_LIST;
    static constexpr asset::E_INDEX_TYPE indexType = asset::EIT_16BIT;
};

using MbPipelineRange = std::pair<core::smart_refctd_ptr<ICPURenderpassIndependentPipeline>,const core::smart_refctd_ptr<ICPUMeshBuffer>*>;


struct SceneData
{
    smart_refctd_ptr<IGPURenderpassIndependentPipeline> fillVBufferPpln;
    smart_refctd_ptr<IGPUComputePipeline> shadeVBufferPpln;

    smart_refctd_ptr<IGPUBuffer> mdiBuffer,idxBuffer;
    smart_refctd_ptr<IGPUDescriptorSet> vtDS,vgDS,perFrameDS,shadingDS;

    core::vector<DrawIndexedIndirectInput> drawIndirectInput;
    core::vector<uint32_t> pushConstantsData;

    smart_refctd_ptr<IGPUBuffer> ubo;

};

using MeshPacker = CCPUMeshPackerV2<DrawElementsIndirectCommand_t>;
using GPUMeshPacker = CGPUMeshPackerV2<DrawElementsIndirectCommand_t>;

GPUMeshPacker packMeshBuffers(video::IVideoDriver* driver, core::vector<MbPipelineRange>& ranges, SceneData& sceneData)
{
    assert(ranges.size()>=2u);

    constexpr uint16_t minTrisBatch = 256u; 
    constexpr uint16_t maxTrisBatch = MAX_TRIANGLES_IN_BATCH;

    constexpr uint32_t kVerticesPerTriangle = 3u;
    MeshPacker::AllocationParams allocParams;
    allocParams.indexBuffSupportedCnt = 32u*1024u*1024u;
    allocParams.indexBufferMinAllocCnt = minTrisBatch*kVerticesPerTriangle;
    allocParams.vertexBuffSupportedByteSize = 128u*1024u*1024u;
    allocParams.vertexBufferMinAllocByteSize = minTrisBatch;
    allocParams.MDIDataBuffSupportedCnt = 8192u;
    allocParams.MDIDataBuffMinAllocCnt = 1u; //so structs are adjacent in memory (TODO: WTF NO!)
    
    CCPUMeshPackerV2 mp(allocParams,minTrisBatch,maxTrisBatch);

    auto wholeMbRangeBegin = ranges.front().second;
    auto wholeMbRangeEnd = ranges.back().second;
    const uint32_t meshBufferCnt = std::distance(wholeMbRangeBegin,wholeMbRangeEnd);

    const uint32_t mdiCntTotal = mp.calcMDIStructMaxCount(wholeMbRangeBegin,wholeMbRangeEnd); // TODO rename

    auto allocData = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<MeshPacker::ReservedAllocationMeshBuffers>>(mdiCntTotal);

    core::vector<uint32_t> allocDataOffsetForDrawCall(ranges.size());
    allocDataOffsetForDrawCall[0] = 0u;
    uint32_t i = 0u;
    for (auto it=ranges.begin(); it!=ranges.end()-1u; )
    {
        auto mbRangeBegin = &it->second->get();
        auto mbRangeEnd = &(++it)->second->get();

        bool allocSuccessfull = mp.alloc(allocData->data() + allocDataOffsetForDrawCall[i], mbRangeBegin, mbRangeEnd);
        if (!allocSuccessfull)
        {
            std::cout << "Alloc failed \n";
            _NBL_DEBUG_BREAK_IF(true);
        }

        const uint32_t mdiMaxCnt = mp.calcMDIStructMaxCount(mbRangeBegin,mbRangeEnd);
        allocDataOffsetForDrawCall[i + 1] = allocDataOffsetForDrawCall[i] + mdiMaxCnt;
        i++;
    }
    
    mp.shrinkOutputBuffersSize();
    mp.instantiateDataStorage();
    MeshPacker::PackerDataStore packerDataStore = mp.getPackerDataStore();

    core::vector<BatchInstanceData> batchData;
    batchData.reserve(mdiCntTotal);

    core::vector<uint32_t> mdiCntForMeshBuffer;
    mdiCntForMeshBuffer.reserve(meshBufferCnt);

    uint32_t offsetForDrawCall = 0u;
    i = 0u;
    for (auto it=ranges.begin(); it!=ranges.end()-1u;)
    {
        auto mbRangeBegin = &it->second->get();
        auto mbRangeEnd = &(++it)->second->get();

        const uint32_t mdiMaxCnt = mp.calcMDIStructMaxCount(mbRangeBegin, mbRangeEnd);
        core::vector<IMeshPackerBase::PackedMeshBufferData> pmbd(mdiMaxCnt); //why mdiMaxCnt and not meshBuffersInRangeCnt??????????
        core::vector<MeshPacker::CombinedDataOffsetTable> cdot(mdiMaxCnt);

        uint32_t mdiCnt = mp.commit(pmbd.data(), cdot.data(), allocData->data() + allocDataOffsetForDrawCall[i], mbRangeBegin, mbRangeEnd);
        if (mdiCnt == 0u)
        {
            std::cout << "Commit failed \n";
            _NBL_DEBUG_BREAK_IF(true);
        }

        sceneData.pushConstantsData.push_back(offsetForDrawCall);
        offsetForDrawCall += mdiCnt;

        DrawIndexedIndirectInput mdiCallInput;
        mdiCallInput.maxCount = mdiCnt;
        mdiCallInput.offset = pmbd[0].mdiParameterOffset * sizeof(DrawElementsIndirectCommand_t);

        sceneData.drawIndirectInput.push_back(mdiCallInput);

        const uint32_t mbInRangeCnt = std::distance(mbRangeBegin, mbRangeEnd);
        for (uint32_t j = 0u; j < mbInRangeCnt; j++)
        {
            mdiCntForMeshBuffer.push_back(pmbd[j].mdiParameterCount);
        }

        //setOffsetTables
        for (uint32_t j = 0u; j < mdiCnt; j++)
        {
            MeshPacker::CombinedDataOffsetTable& virtualAttribTable = cdot[j];

            offsetTableLocal.push_back(virtualAttribTable.attribInfo[0]);
            offsetTableLocal.push_back(virtualAttribTable.attribInfo[2]);
            offsetTableLocal.push_back(virtualAttribTable.attribInfo[3]);
        }

        i++;
    }

    //prepare data for (set = 1, binding = 0) shader ssbo
    {

        uint32_t i = 0u;
        for (auto it = wholeMbRangeBegin; it != wholeMbRangeEnd; it++)
        {
            const uint32_t mdiCntForThisMb = mdiCntForMeshBuffer[i];
            for (uint32_t i = 0u; i < mdiCntForThisMb; i++)
                vtData.push_back(*reinterpret_cast<MaterialParams*>((*it)->getPushConstantsDataPtr()));

            i++;
        }

        sceneData.vtDataSSBO = driver->createFilledDeviceLocalGPUBufferOnDedMem(batchData.size()*sizeof(BatchInstanceData),batchData.data());
    }

    return GPUMeshPacker(driver,std::move(mp));
}

//vt stuff
using STextureData = asset::ICPUVirtualTexture::SMasterTextureData;

constexpr uint32_t PAGE_SZ_LOG2 = 7u;
constexpr uint32_t TILES_PER_DIM_LOG2 = 4u;
constexpr uint32_t PAGE_PADDING = 8u;
constexpr uint32_t MAX_ALLOCATABLE_TEX_SZ_LOG2 = 12u; //4096

constexpr uint32_t VT_SET = 0u;
constexpr uint32_t PGTAB_BINDING = 4u;
constexpr uint32_t PHYSICAL_STORAGE_VIEWS_BINDING = 5u;

struct commit_t
{
    STextureData addr;
    core::smart_refctd_ptr<asset::ICPUImage> texture;
    asset::ICPUImage::SSubresourceRange subresource;
    asset::ICPUSampler::E_TEXTURE_CLAMP uwrap;
    asset::ICPUSampler::E_TEXTURE_CLAMP vwrap;
    asset::ICPUSampler::E_TEXTURE_BORDER_COLOR border;
};

constexpr uint32_t texturesOfInterest[TEX_OF_INTEREST_CNT] =
{
    asset::CMTLMetadata::CRenderpassIndependentPipeline::EMP_AMBIENT,
    asset::CMTLMetadata::CRenderpassIndependentPipeline::EMP_DIFFUSE,
    asset::CMTLMetadata::CRenderpassIndependentPipeline::EMP_SPECULAR,
    asset::CMTLMetadata::CRenderpassIndependentPipeline::EMP_SHININESS,
    asset::CMTLMetadata::CRenderpassIndependentPipeline::EMP_OPACITY,
    asset::CMTLMetadata::CRenderpassIndependentPipeline::EMP_BUMP
};

STextureData getTextureData(core::vector<commit_t>& _out_commits, const asset::ICPUImage* _img, asset::ICPUVirtualTexture* _vt, asset::ISampler::E_TEXTURE_CLAMP _uwrap, asset::ISampler::E_TEXTURE_CLAMP _vwrap, asset::ISampler::E_TEXTURE_BORDER_COLOR _borderColor)
{
    const auto& extent = _img->getCreationParameters().extent;

    auto imgAndOrigSz = asset::ICPUVirtualTexture::createPoTPaddedSquareImageWithMipLevels(_img, _uwrap, _vwrap, _borderColor);

    asset::IImage::SSubresourceRange subres;
    subres.baseMipLevel = 0u;
    subres.levelCount = core::findLSB(core::roundDownToPoT<uint32_t>(std::max(extent.width, extent.height))) + 1;
    subres.baseArrayLayer = 0u;
    subres.layerCount = 1u;

    auto addr = _vt->alloc(_img->getCreationParameters().format, imgAndOrigSz.second, subres, _uwrap, _vwrap);
    commit_t cm{ addr, std::move(imgAndOrigSz.first), subres, _uwrap, _vwrap, _borderColor };

    _out_commits.push_back(cm);

    return addr;
}

constexpr bool useSSBO = true;

#if 0
void createDescriptorSets(IVideoDriver* driver, const IGPUPipelineLayout* pplnLayout, SceneData& sceneData, const GPUMeshPacker& mp)
{    
    auto getMpWriteAndInfoSize = [&mp]() -> std::pair<uint32_t, uint32_t>
    {
        if constexpr (useSSBO)
        {
            uint32_t writeAndInfoSize = mp.getDescriptorSetWritesForSSBO(nullptr, nullptr, nullptr);
            return std::make_pair(writeAndInfoSize, writeAndInfoSize);
        }
        else
            return mp.getDescriptorSetWritesForUTB(nullptr, nullptr, nullptr);
    };

    //mesh packing stuff
    auto sizesMP = getMpWriteAndInfoSize();
    auto writesMP = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<IGPUDescriptorSet::SWriteDescriptorSet>>(sizesMP.first);
    auto infoMP = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<IGPUDescriptorSet::SDescriptorInfo>>(sizesMP.second);

    auto writesPtr = writesMP->data();
    auto infoPtr = infoMP->data();

    if constexpr (useSSBO)
        mp.getDescriptorSetWritesForSSBO(writesMP->data(), infoMP->data(), sceneData.ds[0].get());
    else
        mp.getDescriptorSetWritesForUTB(writesMP->data(), infoMP->data(), sceneData.ds[0].get());

    driver->updateDescriptorSets(writesMP->size(), writesMP->data(), 0u, nullptr);

    IGPUDescriptorSet::SWriteDescriptorSet w3;
    w3.arrayElement = 0u;
    w3.count = 1u;
    w3.binding = 0u;
    w3.descriptorType = EDT_STORAGE_BUFFER;
    w3.dstSet = sceneData.ds[3].get();

    IGPUDescriptorSet::SDescriptorInfo info3;
    info3.buffer.offset = 0u;
    info3.buffer.size = sceneData.virtualAttribTable->getSize();
    info3.desc = core::smart_refctd_ptr(sceneData.virtualAttribTable);
    w3.info = &info3;

    driver->updateDescriptorSets(1u, &w3, 0u, nullptr);

    //vt stuff
    auto sizesVT = sceneData.vt->getDescriptorSetWrites(nullptr, nullptr, nullptr);
    auto writesVT = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<video::IGPUDescriptorSet::SWriteDescriptorSet>>(sizesVT.first);
    auto infoVT = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<video::IGPUDescriptorSet::SDescriptorInfo>>(sizesVT.second);

    sceneData.vt->getDescriptorSetWrites(writesVT->data(), infoVT->data(), sceneData.ds[0].get(), PGTAB_BINDING, PHYSICAL_STORAGE_VIEWS_BINDING);

    driver->updateDescriptorSets(writesVT->size(), writesVT->data(), 0u, nullptr);

    IGPUDescriptorSet::SWriteDescriptorSet w2[2];
    w2[0].arrayElement = 0u;
    w2[0].count = 1u;
    w2[0].binding = 0u;
    w2[0].descriptorType = EDT_STORAGE_BUFFER;
    w2[0].dstSet = sceneData.ds[2].get();

    w2[1].arrayElement = 0u;
    w2[1].count = 1u;
    w2[1].binding = 1u;
    w2[1].descriptorType = EDT_STORAGE_BUFFER;
    w2[1].dstSet = sceneData.ds[2].get();

    core::smart_refctd_ptr<video::IGPUBuffer> buffer = driver->createFilledDeviceLocalGPUBufferOnDedMem(sizeof(video::IGPUVirtualTexture::SPrecomputedData), &sceneData.vt->getPrecomputedData());

    IGPUDescriptorSet::SDescriptorInfo info2[2];
    info2[0].buffer.offset = 0u;
    info2[0].buffer.size = sizeof(video::IGPUVirtualTexture::SPrecomputedData);
    info2[0].desc = buffer;

    info2[1].buffer.offset = 0u;
    info2[1].buffer.size = sceneData.vtDataSSBO->getSize();
    info2[1].desc = sceneData.vtDataSSBO; // TODO: rename vtData to materialData

    w2[0].info = &info2[0];
    w2[1].info = &info2[1];

    driver->updateDescriptorSets(2u,w2,0u,nullptr);
}
#endif

int main()
{
    // create device with full flexibility over creation parameters
    // you can add more parameters if desired, check irr::SIrrlichtCreationParameters
    nbl::SIrrlichtCreationParameters params;
    params.Bits = 24; //may have to set to 32bit for some platforms
    params.ZBufferBits = 24; //we'd like 32bit here
    params.DriverType = video::EDT_OPENGL; //! Only Well functioning driver, software renderer left for sake of 2D image drawing
    params.WindowSize = dimension2d<uint32_t>(1280, 720);
    params.Fullscreen = false;
    params.Vsync = true; //! If supported by target platform
    params.Doublebuffer = true;
    params.Stencilbuffer = false; //! This will not even be a choice soon
    auto device = createDeviceEx(params);

    if (!device)
        return 1; // could not create selected driver.

    //! disable mouse cursor, since camera will force it to the middle
    //! and we don't want a jittery cursor in the middle distracting us
    device->getCursorControl()->setVisible(false);

    //! Since our cursor will be enslaved, there will be no way to close the window
    //! So we listen for the "Q" key being pressed and exit the application
    QToQuitEventReceiver receiver;
    device->setEventReceiver(&receiver);

    auto* driver = device->getVideoDriver();
    auto* smgr = device->getSceneManager();
    auto* am = device->getAssetManager();
    auto* fs = am->getFileSystem();

    //
    auto createScreenSizedImage = [driver,&params](const E_FORMAT format) -> auto
    {
        IGPUImage::SCreationParams param;
        param.flags = static_cast<IImage::E_CREATE_FLAGS>(0u);
        param.type = IImage::ET_2D;
        param.format = format;
        param.extent = {params.WindowSize.Width,params.WindowSize.Height,1u};
        param.mipLevels = 1u;
        param.arrayLayers = 1u;
        param.samples = IImage::ESCF_1_BIT;
        return driver->createDeviceLocalGPUImageOnDedMem(std::move(param));
    };
    auto framebuffer = createScreenSizedImage(EF_R8G8B8A8_SRGB);
    auto createImageView = [driver,&params](smart_refctd_ptr<IGPUImage>&& image, const E_FORMAT format=EF_UNKNOWN) -> auto
    {
        IGPUImageView::SCreationParams params;
        params.flags = static_cast<IGPUImageView::E_CREATE_FLAGS>(0u);
        params.image = std::move(image);
        params.viewType = IGPUImageView::ET_2D;
        params.format = format!=EF_UNKNOWN ? format:params.image->getCreationParameters().format;
        params.components = {};
        params.subresourceRange = {};
        params.subresourceRange.levelCount = 1u;
        params.subresourceRange.layerCount = 1u;
        return driver->createGPUImageView(std::move(params));
    };
    auto visbufferView = createImageView(createScreenSizedImage(EF_R32G32B32A32_UINT));

    auto visbuffer = driver->addFrameBuffer();
    visbuffer->attach(EFAP_COLOR_ATTACHMENT0,smart_refctd_ptr(visbufferView));
    auto fb = driver->addFrameBuffer();
    fb->attach(EFAP_COLOR_ATTACHMENT0,createImageView(smart_refctd_ptr(framebuffer)));

    //
    SceneData sceneData;
    {
        //
        smart_refctd_ptr<IGPUDescriptorSetLayout> perFrameDSLayout,shadingDSLayout;
        {
            {
                IGPUDescriptorSetLayout::SBinding bindings[1];
                bindings[0].binding = 0u;
                bindings[0].count = 1u;
                bindings[0].samplers = nullptr;
                bindings[0].stageFlags = ISpecializedShader::ESS_VERTEX;
                bindings[0].type = EDT_UNIFORM_BUFFER;

                perFrameDSLayout = driver->createGPUDescriptorSetLayout(bindings,bindings+sizeof(bindings)/sizeof(IGPUDescriptorSetLayout::SBinding));
            }
            {
                sceneData.ubo = driver->createDeviceLocalGPUBufferOnDedMem(sizeof(SBasicViewParameters));
                IGPUDescriptorSet::SDescriptorInfo infos[1];
                infos[0].desc = core::smart_refctd_ptr(sceneData.ubo);
                infos[0].buffer.offset = 0u;
                infos[0].buffer.size = sceneData.ubo->getSize();

                sceneData.perFrameDS = driver->createGPUDescriptorSet(smart_refctd_ptr(perFrameDSLayout));
                IGPUDescriptorSet::SWriteDescriptorSet writes[1];
                writes[0].dstSet = sceneData.perFrameDS.get();
                writes[0].binding = 0u;
                writes[0].arrayElement = 0u;
                writes[0].count = 1u;
                writes[0].descriptorType = EDT_UNIFORM_BUFFER;
                writes[0].info = infos+0u;
                driver->updateDescriptorSets(sizeof(writes)/sizeof(IGPUDescriptorSet::SWriteDescriptorSet),writes,0u,nullptr);
            }
            
            {
                IGPUSampler::SParams params;
                params.TextureWrapU = ISampler::ETC_MIRROR;
                params.TextureWrapV = ISampler::ETC_MIRROR;
                params.TextureWrapW = ISampler::ETC_MIRROR;
                params.BorderColor = ISampler::ETBC_FLOAT_OPAQUE_BLACK;
                params.MinFilter = ISampler::ETF_NEAREST;
                params.MaxFilter = ISampler::ETF_NEAREST;
                params.MipmapMode = ISampler::ESMM_NEAREST;
                params.AnisotropicFilter = 0;
                params.CompareEnable = 0;
                auto sampler = driver->createGPUSampler(params);

                IGPUDescriptorSetLayout::SBinding bindings[2];
                bindings[0].binding = 0u;
                bindings[0].count = 1u;
                bindings[0].samplers = &sampler;
                bindings[0].stageFlags = ISpecializedShader::ESS_COMPUTE;
                bindings[0].type = EDT_COMBINED_IMAGE_SAMPLER;
                bindings[1].binding = 1u;
                bindings[1].count = 1u;
                bindings[1].samplers = nullptr;
                bindings[1].stageFlags = ISpecializedShader::ESS_COMPUTE;
                bindings[1].type = EDT_STORAGE_IMAGE;

                shadingDSLayout = driver->createGPUDescriptorSetLayout(bindings,bindings+sizeof(bindings)/sizeof(IGPUDescriptorSetLayout::SBinding));
            }
            {
                IGPUDescriptorSet::SDescriptorInfo infos[2];
                infos[0].desc = core::smart_refctd_ptr(visbufferView);
                //infos[0].image.imageLayout = ?;
                infos[0].image.sampler = nullptr; // used immutable in the layout
                infos[1].desc = createImageView(std::move(framebuffer),EF_R8G8B8A8_UNORM);
                //infos[0].image.imageLayout = ?;
                infos[1].image.sampler = nullptr; // storage image

                sceneData.shadingDS = driver->createGPUDescriptorSet(smart_refctd_ptr(shadingDSLayout));
                IGPUDescriptorSet::SWriteDescriptorSet writes[2];
                for (auto i=0u; i<2u; i++)
                {
                    writes[i].dstSet = sceneData.shadingDS.get();
                    writes[i].binding = i;
                    writes[i].arrayElement = 0u;
                    writes[i].count = 1u;
                    writes[i].info = infos+i;
                }
                writes[0].descriptorType = EDT_COMBINED_IMAGE_SAMPLER;
                writes[1].descriptorType = EDT_STORAGE_IMAGE;
                driver->updateDescriptorSets(sizeof(writes)/sizeof(IGPUDescriptorSet::SWriteDescriptorSet),writes,0u,nullptr);
            }
        }

        //
        auto* qnc = am->getMeshManipulator()->getQuantNormalCache();
        //loading cache from file
        qnc->loadCacheFromFile<asset::EF_A2B10G10R10_SNORM_PACK32>(fs, "../../tmp/normalCache101010.sse", true);

        // register the zip
        device->getFileSystem()->addFileArchive("../../media/sponza.zip");
        asset::IAssetLoader::SAssetLoadParams lp;
        auto meshes_bundle = am->getAsset("sponza.obj", lp);
        assert(!meshes_bundle.getContents().empty());
        auto mesh_raw = static_cast<asset::ICPUMesh*>(meshes_bundle.getContents().begin()->get());
        // ensure memory will be freed as soon as CPU assets are dropped
        // am->clearAllAssetCache();


        //saving cache to file
        qnc->saveCacheToFile<asset::EF_A2B10G10R10_SNORM_PACK32>(fs, "../../tmp/normalCache101010.sse");

        //
        auto meshBuffers = mesh_raw->getMeshBufferVector();

        auto pipelineMeshBufferRanges = [&meshBuffers]() -> core::vector<MbPipelineRange>
        {
            if (meshBuffers.empty())
                return {};

            // sort meshbuffers by pipeline
            std::sort(meshBuffers.begin(),meshBuffers.end(),[](const auto& lhs, const auto& rhs)
                {
                    auto lPpln = lhs->getPipeline();
                    auto rPpln = rhs->getPipeline();
                    // render non-transparent things first
                    if (lPpln->getBlendParams().blendParams[0].blendEnable < rPpln->getBlendParams().blendParams[0].blendEnable)
                        return true;
                    if (lPpln->getBlendParams().blendParams[0].blendEnable == rPpln->getBlendParams().blendParams[0].blendEnable)
                        return lPpln < rPpln;
                    return false;
                }
            );

            core::vector<MbPipelineRange> output;
            smart_refctd_ptr<ICPURenderpassIndependentPipeline> mbPipeline = nullptr;
            for (const auto& mb : meshBuffers)
            if (mb->getPipeline()!=mbPipeline.get())
            {
                mbPipeline = core::smart_refctd_ptr<ICPURenderpassIndependentPipeline>(mb->getPipeline());
                output.emplace_back(core::smart_refctd_ptr(mbPipeline),&mb);
            }
            output.emplace_back(core::smart_refctd_ptr<ICPURenderpassIndependentPipeline>(),meshBuffers.data()+meshBuffers.size());
            return output;
        }();

        smart_refctd_ptr<IGPUVirtualTexture> gpuvt;
        {
            smart_refctd_ptr<ICPUVirtualTexture> vt = core::make_smart_refctd_ptr<asset::ICPUVirtualTexture>([](asset::E_FORMAT_CLASS) -> uint32_t { return TILES_PER_DIM_LOG2; }, PAGE_SZ_LOG2, PAGE_PADDING, MAX_ALLOCATABLE_TEX_SZ_LOG2);
            {
                core::unordered_map<core::smart_refctd_ptr<asset::ICPUImage>,STextureData> VTtexDataMap;

                core::vector<commit_t> vt_commits;
                //modifying push constants and default fragment shader for VT
                for (auto it = meshBuffers.begin(); it != meshBuffers.end(); it++)
                {
                    MaterialParams pushConsts;
                    memset(pushConsts.map_data, 0xff, TEX_OF_INTEREST_CNT * sizeof(pushConsts.map_data[0]));
                    pushConsts.extra = 0u;

                    auto* ds = (*it)->getAttachedDescriptorSet();
                    if (!ds)
                        continue;
                    for (uint32_t k = 0u; k < TEX_OF_INTEREST_CNT; ++k)
                    {
                        uint32_t j = texturesOfInterest[k];

                        auto* view = static_cast<asset::ICPUImageView*>(ds->getDescriptors(j).begin()->desc.get());
                        auto* smplr = ds->getLayout()->getBindings().begin()[j].samplers[0].get();
                        const auto uwrap = static_cast<asset::ISampler::E_TEXTURE_CLAMP>(smplr->getParams().TextureWrapU);
                        const auto vwrap = static_cast<asset::ISampler::E_TEXTURE_CLAMP>(smplr->getParams().TextureWrapV);
                        const auto borderColor = static_cast<asset::ISampler::E_TEXTURE_BORDER_COLOR>(smplr->getParams().BorderColor);
                        auto img = view->getCreationParameters().image;
                        auto extent = img->getCreationParameters().extent;
                        if (extent.width <= 2u || extent.height <= 2u)//dummy 2x2
                            continue;
                        STextureData texData = STextureData::invalid();
                        auto found = VTtexDataMap.find(img);
                        if (found != VTtexDataMap.end())
                            texData = found->second;
                        else
                        {
                            const asset::E_FORMAT fmt = img->getCreationParameters().format;
                            texData = getTextureData(vt_commits, img.get(), vt.get(), uwrap, vwrap, borderColor);
                            VTtexDataMap.insert({ img,texData });
                        }

                        static_assert(sizeof(texData) == sizeof(pushConsts.map_data[0]), "wrong reinterpret_cast");
                        pushConsts.map_data[k] = reinterpret_cast<uint64_t*>(&texData)[0];
                    }

                    // all pipelines will have the same metadata
                    auto pipelineMetadata = static_cast<const asset::CMTLMetadata::CRenderpassIndependentPipeline*>(meshes_bundle.getMetadata()->selfCast<const asset::COBJMetadata>()->getAssetSpecificMetadata((*it)->getPipeline()));
                    assert(pipelineMetadata);

                    //copy texture presence flags
                    pushConsts.extra = pipelineMetadata->m_materialParams.extra;
                    pushConsts.ambient = pipelineMetadata->m_materialParams.ambient;
                    pushConsts.diffuse = pipelineMetadata->m_materialParams.diffuse;
                    pushConsts.emissive = pipelineMetadata->m_materialParams.emissive;
                    pushConsts.specular = pipelineMetadata->m_materialParams.specular;
                    pushConsts.IoR = pipelineMetadata->m_materialParams.IoR;
                    pushConsts.opacity = pipelineMetadata->m_materialParams.opacity;
                    pushConsts.shininess = pipelineMetadata->m_materialParams.shininess;
                    memcpy((*it)->getPushConstantsDataPtr(), &pushConsts, sizeof(pushConsts));

                    //we dont want this DS to be converted into GPU DS, so set to nullptr
                    //dont worry about deletion of textures (invalidation of pointers), they're grabbed in VTtexDataMap
                    (*it)->setAttachedDescriptorSet(nullptr);
                }

                vt->shrink();
                for (const auto& cm : vt_commits)
                    vt->commit(cm.addr, cm.texture.get(), cm.subresource, cm.uwrap, cm.vwrap, cm.border);
            }

            gpuvt = core::make_smart_refctd_ptr<IGPUVirtualTexture>(driver, vt.get());
        }

        // the vertex packing
        GPUMeshPacker mp = packMeshBuffers(driver,pipelineMeshBufferRanges,sceneData);

        //
        auto overrideShaderJustAfterVersionDirective = [am, driver](const char* path, const std::string& extraCode)
        {
            asset::IAssetLoader::SAssetLoadParams lp;
            auto _specShader = IAsset::castDown<ICPUSpecializedShader>(*am->getAsset(path, lp).getContents().begin());
            assert(_specShader);
            const asset::ICPUShader* unspec = _specShader->getUnspecialized();
            assert(unspec->containsGLSL());

            auto begin = reinterpret_cast<const char*>(unspec->getSPVorGLSL()->getPointer());
            const std::string_view origSource(begin, unspec->getSPVorGLSL()->getSize());

            const size_t firstNewlineAfterVersion = origSource.find("\n", origSource.find("#version "));
            assert(firstNewlineAfterVersion != std::string_view::npos);
            const std::string_view sourceWithoutVersion(begin + firstNewlineAfterVersion, origSource.size() - firstNewlineAfterVersion);

            std::string newSource("#version 460 core\n");
            newSource += extraCode;
            newSource += sourceWithoutVersion;

            auto unspecNew = core::make_smart_refctd_ptr<asset::ICPUShader>(newSource.c_str());
            auto specinfo = _specShader->getSpecializationInfo();

            auto newSpecShader = core::make_smart_refctd_ptr<asset::ICPUSpecializedShader>(std::move(unspecNew), std::move(specinfo));
            auto gpuSpecShaders = driver->getGPUObjectsFromAssets(&newSpecShader.get(), &newSpecShader.get() + 1u);
            return std::move(gpuSpecShaders->begin()[0]);
        };
        smart_refctd_ptr<IGPUDescriptorSetLayout> vtDSLayout,vgDSLayout;
        {
            auto xxxx = gpuvt->getDSlayoutBindings(nullptr,nullptr);
            core::vector<IGPUDescriptorSetLayout::SBinding> vtBindings();
            gpuvt->getDSlayoutBindings(vtBindings.data(),vtSamplers->data(),PGTAB_BINDING,PHYSICAL_STORAGE_VIEWS_BINDING);
            for (auto& binding : vtBindings)
                binding.stageFlags = static_cast<ISpecializedShader::E_SHADER_STAGE>(ISpecializedShader::ESS_COMPUTE|ISpecializedShader::ESS_FRAGMENT);

            core::vector<IGPUDescriptorSetLayout::SBinding> vgBindings(useSSBO ? mp.getDSlayoutBindingsForSSBO(nullptr):mp.getDSlayoutBindingsForUTB(nullptr));
            if constexpr (useSSBO)
                mp.getDSlayoutBindingsForSSBO(vgBindings.data());
            else
                mp.getDSlayoutBindingsForUTB(vgBindings.data());
            for (auto& binding : vgBindings)
                binding.stageFlags = static_cast<ISpecializedShader::E_SHADER_STAGE>(ISpecializedShader::ESS_VERTEX|ISpecializedShader::ESS_COMPUTE|ISpecializedShader::ESS_FRAGMENT);});
        }
        //
        std::string extraCode = useSSBO ? mp.getGLSLForSSBO() : mp.getGLSLForUTB();
        // TODO: sprintf(fragPrelude.data(), FRAGMENT_SHADER_OVERRIDES, sceneData.vt->getFloatViews().size(), sceneData.vt->getGLSLFunctionsIncludePath().c_str());
        //
        {
            SPushConstantRange pcRange;
            pcRange.size = sizeof(uint32_t);
            pcRange.offset = 0u;
            pcRange.stageFlags = ISpecializedShader::ESS_VERTEX;

            smart_refctd_ptr<IGPUSpecializedShader> fillShaders[2] = {
                overrideShaderJustAfterVersionDirective("fillVBuffer.vert",extraCode),
                overrideShaderJustAfterVersionDirective("fillVBuffer.frag",extraCode)
            };

            sceneData.fillVBufferPpln = driver->createGPURenderpassIndependentPipeline(
                nullptr,driver->createGPUPipelineLayout(&pcRange,&pcRange+1,smart_refctd_ptr(vtDSLayout),smart_refctd_ptr(vgDSLayout),smart_refctd_ptr(perFrameDSLayout)),
                &fillShaders[0].get(),&fillShaders[0].get()+2u,
                SVertexInputParams{},
                SBlendParams{},
                SPrimitiveAssemblyParams{},
                SRasterizationParams{}
            );
        }
        {
            SPushConstantRange pcRange;
            pcRange.size = sizeof(core::vectorSIMDf); // TODO: send camera data
            pcRange.offset = 0u;
            pcRange.stageFlags = ISpecializedShader::ESS_COMPUTE;

            sceneData.shadeVBufferPpln = driver->createGPUComputePipeline(
                nullptr,driver->createGPUPipelineLayout(&pcRange,&pcRange+1,std::move(vtDSLayout),std::move(vgDSLayout),std::move(perFrameDSLayout),std::move(shadingDSLayout)),
                overrideShaderJustAfterVersionDirective("shadeVBuffer.comp",extraCode)
            );
        }
    }

    //! we want to move around the scene and view it from different angles
    scene::ICameraSceneNode* camera = smgr->addCameraSceneNodeFPS(0, 100.0f, 0.5f);

    camera->setPosition(core::vector3df(-4, 0, 0));
    camera->setTarget(core::vector3df(0, 0, 0));
    camera->setNearValue(1.f);
    camera->setFarValue(5000.0f);

    smgr->setActiveCamera(camera);
    

    uint64_t lastFPSTime = 0;
    while (device->run() && receiver.keepOpen())
    {
        driver->beginScene(true, true, video::SColor(255, 0, 0, 255));

        //! This animates (moves) the camera and sets the transforms
        camera->OnAnimate(std::chrono::duration_cast<std::chrono::milliseconds>(device->getTimer()->getTime()).count());
        camera->render();

        SBasicViewParameters uboData;
        memcpy(uboData.MVP, camera->getConcatenatedMatrix().pointer(), sizeof(core::matrix4SIMD));
        memcpy(uboData.MV, camera->getViewMatrix().pointer(), sizeof(core::matrix3x4SIMD));
        memcpy(uboData.NormalMat, camera->getViewMatrix().pointer(), sizeof(core::matrix3x4SIMD));
        driver->updateBufferRangeViaStagingBuffer(sceneData.ubo.get(), 0u, sizeof(SBasicViewParameters), &uboData);

        // TODO: Cull MDIs

        driver->setRenderTarget(visbuffer);
        driver->clearZBuffer();
        const uint32_t invalidObjectCode[4] = {~0u,0u,0u,0u};
        driver->clearColorBuffer(EFAP_COLOR_ATTACHMENT0,invalidObjectCode);

        const IGPUDescriptorSet* ds[4] = {sceneData.vtDS.get(),sceneData.vgDS.get(),sceneData.perFrameDS.get(),sceneData.shadingDS.get()};
        driver->bindDescriptorSets(video::EPBP_GRAPHICS,sceneData.fillVBufferPpln->getLayout(),0u,3u,ds,nullptr);
        // fill visibility buffer
        driver->bindGraphicsPipeline(sceneData.fillVBufferPpln.get());
        for (auto i = 0u; i<sceneData.pushConstantsData.size(); i++)
        {
            driver->pushConstants(sceneData.fillVBufferPpln->getLayout(),IGPUSpecializedShader::ESS_ALL,0u,sizeof(uint32_t),sceneData.pushConstantsData.data()+i);

            const asset::SBufferBinding<IGPUBuffer> noVtxBindings[IGPUMeshBuffer::MAX_ATTR_BUF_BINDING_COUNT] = {};
            driver->drawIndexedIndirect(
                noVtxBindings,DrawIndexedIndirectInput::mode,DrawIndexedIndirectInput::indexType,
                sceneData.idxBuffer.get(),sceneData.mdiBuffer.get(),
                sceneData.drawIndirectInput[i].offset,sceneData.drawIndirectInput[i].maxCount,
                sizeof(DrawElementsIndirectCommand_t)
            );
        }

        // shade
        driver->bindDescriptorSets(video::EPBP_COMPUTE,sceneData.shadeVBufferPpln->getLayout(),0u,4u,ds,nullptr);
        driver->bindComputePipeline(sceneData.shadeVBufferPpln.get());
        driver->dispatch((params.WindowSize.Width-1u)/SHADING_WG_SIZE_X+1u,(params.WindowSize.Height-1u)/SHADING_WG_SIZE_Y+1u,1u);
        COpenGLExtensionHandler::extGlMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

        // blit
        driver->blitRenderTargets(fb,0);
        driver->endScene();

        // display frames per second in window title
        uint64_t time = device->getTimer()->getRealTime();
        if (time - lastFPSTime > 1000)
        {
            std::wostringstream str;
            str << L"Visibility Buffer - Nabla Engine [" << driver->getName() << "] FPS:" << driver->getFPS() << " PrimitvesDrawn:" << driver->getPrimitiveCountDrawn();

            device->setWindowCaption(str.str().c_str());
            lastFPSTime = time;
        }
    }
    driver->removeAllFrameBuffers();
}