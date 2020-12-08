// Copyright (c) 2012-2020 Wojciech Figat. All rights reserved.

#if GRAPHICS_API_VULKAN

#include "CmdBufferVulkan.h"
#include "RenderToolsVulkan.h"
#include "QueueVulkan.h"
#include "GPUContextVulkan.h"
#include "GPUTimerQueryVulkan.h"
#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
#include "DescriptorSetVulkan.h"
#endif

void CmdBufferVulkan::AddWaitSemaphore(VkPipelineStageFlags waitFlags, SemaphoreVulkan* waitSemaphore)
{
    WaitFlags.Add(waitFlags);
    ASSERT(!WaitSemaphores.Contains(waitSemaphore));
    WaitSemaphores.Add(waitSemaphore);
}

void CmdBufferVulkan::Begin()
{
    ASSERT(_state == State::ReadyForBegin);

    VkCommandBufferBeginInfo beginInfo;
    RenderToolsVulkan::ZeroStruct(beginInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VALIDATE_VULKAN_RESULT(vkBeginCommandBuffer(CommandBufferHandle, &beginInfo));

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
    // Acquire a descriptor pool set on
    if (CurrentDescriptorPoolSetContainer == nullptr)
    {
        AcquirePoolSet();
    }
#endif

    _state = State::IsInsideBegin;

#if GPU_ALLOW_PROFILE_EVENTS
    // Reset events counter
    _eventsBegin = 0;
#endif
}

void CmdBufferVulkan::End()
{
    ASSERT(IsOutsideRenderPass());

#if GPU_ALLOW_PROFILE_EVENTS
    // End reaming events
    while (_eventsBegin--)
        vkCmdEndDebugUtilsLabelEXT(GetHandle());
#endif

    VALIDATE_VULKAN_RESULT(vkEndCommandBuffer(GetHandle()));
    _state = State::HasEnded;
}

void CmdBufferVulkan::BeginRenderPass(RenderPassVulkan* renderPass, FramebufferVulkan* framebuffer, uint32 clearValueCount, VkClearValue* clearValues)
{
    ASSERT(IsOutsideRenderPass());

    VkRenderPassBeginInfo info;
    RenderToolsVulkan::ZeroStruct(info, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    info.renderPass = renderPass->GetHandle();
    info.framebuffer = framebuffer->GetHandle();
    info.renderArea.offset.x = 0;
    info.renderArea.offset.y = 0;
    info.renderArea.extent.width = framebuffer->Extent.width;
    info.renderArea.extent.height = framebuffer->Extent.height;
    info.clearValueCount = clearValueCount;
    info.pClearValues = clearValues;

    vkCmdBeginRenderPass(CommandBufferHandle, &info, VK_SUBPASS_CONTENTS_INLINE);
    _state = State::IsInsideRenderPass;
}

void CmdBufferVulkan::EndRenderPass()
{
    ASSERT(IsInsideRenderPass());
    vkCmdEndRenderPass(CommandBufferHandle);
    _state = State::IsInsideBegin;
}

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER

void CmdBufferVulkan::AcquirePoolSet()
{
    ASSERT(!CurrentDescriptorPoolSetContainer);
    CurrentDescriptorPoolSetContainer = &_device->DescriptorPoolsManager->AcquirePoolSetContainer();
}

#endif

#if GPU_ALLOW_PROFILE_EVENTS

void CmdBufferVulkan::BeginEvent(const Char* name)
{
    if (!vkCmdBeginDebugUtilsLabelEXT)
        return;

    _eventsBegin++;

    // Convert to ANSI
    char buffer[101];
    int32 i = 0;
    while (i < 100 && name[i])
    {
        buffer[i] = (char)name[i];
        i++;
    }
    buffer[i] = 0;

    VkDebugUtilsLabelEXT label;
    RenderToolsVulkan::ZeroStruct(label, VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
    label.pLabelName = buffer;
    vkCmdBeginDebugUtilsLabelEXT(GetHandle(), &label);
}

void CmdBufferVulkan::EndEvent()
{
    if (_eventsBegin == 0 || !vkCmdEndDebugUtilsLabelEXT)
        return;
    _eventsBegin--;

    vkCmdEndDebugUtilsLabelEXT(GetHandle());
}

#endif

void CmdBufferVulkan::RefreshFenceStatus()
{
    if (_state == State::Submitted)
    {
        auto fenceManager = _fence->GetOwner();
        if (fenceManager->IsFenceSignaled(_fence))
        {
            _state = State::ReadyForBegin;

            SubmittedWaitSemaphores.Clear();

            vkResetCommandBuffer(CommandBufferHandle, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            _fence->GetOwner()->ResetFence(_fence);
            FenceSignaledCounter++;

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
            if (CurrentDescriptorPoolSetContainer)
            {
                _device->DescriptorPoolsManager->ReleasePoolSet(*CurrentDescriptorPoolSetContainer);
                CurrentDescriptorPoolSetContainer = nullptr;
            }
#endif
        }
    }
    else
    {
        ASSERT(!_fence->IsSignaled());
    }
}

CmdBufferVulkan::CmdBufferVulkan(GPUDeviceVulkan* device, CmdBufferPoolVulkan* pool)
    : _device(device)
    , CommandBufferHandle(VK_NULL_HANDLE)
    , _state(State::ReadyForBegin)
    , _fence(nullptr)
    , FenceSignaledCounter(0)
    , SubmittedFenceCounter(0)
    , CommandBufferPool(pool)
{
    VkCommandBufferAllocateInfo createCmdBufInfo;
    RenderToolsVulkan::ZeroStruct(createCmdBufInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    createCmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    createCmdBufInfo.commandBufferCount = 1;
    createCmdBufInfo.commandPool = CommandBufferPool->GetHandle();

    VALIDATE_VULKAN_RESULT(vkAllocateCommandBuffers(_device->Device, &createCmdBufInfo, &CommandBufferHandle));
    _fence = _device->FenceManager.AllocateFence();
}

CmdBufferVulkan::~CmdBufferVulkan()
{
    auto& fenceManager = _device->FenceManager;
    if (_state == State::Submitted)
    {
        // Wait 60ms
        const uint64 waitForCmdBufferInNanoSeconds = 60 * 1000 * 1000LL;
        fenceManager.WaitAndReleaseFence(_fence, waitForCmdBufferInNanoSeconds);
    }
    else
    {
        // Just free the fence, command buffer was not submitted
        fenceManager.ReleaseFence(_fence);
    }

    vkFreeCommandBuffers(_device->Device, CommandBufferPool->GetHandle(), 1, &CommandBufferHandle);
}

CmdBufferVulkan* CmdBufferPoolVulkan::Create()
{
    const auto cmdBuffer = New<CmdBufferVulkan>(Device, this);
    CmdBuffers.Add(cmdBuffer);
    return cmdBuffer;
}

void CmdBufferPoolVulkan::Create(uint32 queueFamilyIndex)
{
    VkCommandPoolCreateInfo poolInfo;
    RenderToolsVulkan::ZeroStruct(poolInfo, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    // TODO: use VK_COMMAND_POOL_CREATE_TRANSIENT_BIT?
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VALIDATE_VULKAN_RESULT(vkCreateCommandPool(Device->Device, &poolInfo, nullptr, &Handle));
}

CmdBufferPoolVulkan::CmdBufferPoolVulkan(GPUDeviceVulkan* device)
    : Device(device)
    , Handle(VK_NULL_HANDLE)
{
}

CmdBufferPoolVulkan::~CmdBufferPoolVulkan()
{
    for (int32 i = 0; i < CmdBuffers.Count(); i++)
    {
        Delete(CmdBuffers[i]);
    }

    vkDestroyCommandPool(Device->Device, Handle, nullptr);
}

void CmdBufferPoolVulkan::RefreshFenceStatus(CmdBufferVulkan* skipCmdBuffer)
{
    for (int32 i = 0; i < CmdBuffers.Count(); i++)
    {
        auto cmdBuffer = CmdBuffers[i];
        if (cmdBuffer != skipCmdBuffer)
        {
            cmdBuffer->RefreshFenceStatus();
        }
    }
}

CmdBufferManagerVulkan::CmdBufferManagerVulkan(GPUDeviceVulkan* device, GPUContextVulkan* context)
    : Device(device)
    , Pool(device)
    , Queue(context->GetQueue())
    , ActiveCmdBuffer(nullptr)
{
    Pool.Create(Queue->GetFamilyIndex());
}

void CmdBufferManagerVulkan::SubmitActiveCmdBuffer(SemaphoreVulkan* signalSemaphore)
{
    ASSERT(ActiveCmdBuffer);
    if (!ActiveCmdBuffer->IsSubmitted() && ActiveCmdBuffer->HasBegun())
    {
        if (ActiveCmdBuffer->IsInsideRenderPass())
        {
            ActiveCmdBuffer->EndRenderPass();
        }

        // Pause all active queries
        for (int32 i = 0; i < QueriesInProgress.Count(); i++)
        {
            QueriesInProgress[i]->Interrupt(ActiveCmdBuffer);
        }

        ActiveCmdBuffer->End();

        if (signalSemaphore)
        {
            Queue->Submit(ActiveCmdBuffer, signalSemaphore->GetHandle());
        }
        else
        {
            Queue->Submit(ActiveCmdBuffer);
        }
    }

    ActiveCmdBuffer = nullptr;
}

void CmdBufferManagerVulkan::WaitForCmdBuffer(CmdBufferVulkan* cmdBuffer, float timeInSecondsToWait)
{
    ASSERT(cmdBuffer->IsSubmitted());
    bool success = Device->FenceManager.WaitForFence(cmdBuffer->GetFence(), (uint64)(timeInSecondsToWait * 1e9));
    ASSERT(success);
    cmdBuffer->RefreshFenceStatus();
}

void CmdBufferManagerVulkan::PrepareForNewActiveCommandBuffer()
{
    for (int32 i = 0; i < Pool.CmdBuffers.Count(); i++)
    {
        auto cmdBuffer = Pool.CmdBuffers[i];
        cmdBuffer->RefreshFenceStatus();
        if (cmdBuffer->GetState() == CmdBufferVulkan::State::ReadyForBegin)
        {
            ActiveCmdBuffer = cmdBuffer;
            ActiveCmdBuffer->Begin();
            return;
        }
        else
        {
            ASSERT(cmdBuffer->GetState() == CmdBufferVulkan::State::Submitted);
        }
    }

    // All cmd buffers are being executed still
    ActiveCmdBuffer = Pool.Create();
    ActiveCmdBuffer->Begin();

    // Resume any paused queries with the new command buffer
    for (int32 i = 0; i < QueriesInProgress.Count(); i++)
    {
        QueriesInProgress[i]->Resume(ActiveCmdBuffer);
    }
}

void CmdBufferManagerVulkan::OnQueryBegin(GPUTimerQueryVulkan* query)
{
    QueriesInProgress.Add(query);
}

void CmdBufferManagerVulkan::OnQueryEnd(GPUTimerQueryVulkan* query)
{
    QueriesInProgress.Remove(query);
}

#endif