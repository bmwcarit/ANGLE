//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DisplayVk.cpp:
//    Implements the class methods for DisplayVk.
//

#include "libANGLE/renderer/vulkan/DisplayVk.h"

#include "common/debug.h"
#include "common/system_utils.h"
#include "libANGLE/Context.h"
#include "libANGLE/Display.h"
#include "libANGLE/renderer/vulkan/BufferVk.h"
#include "libANGLE/renderer/vulkan/ContextVk.h"
#include "libANGLE/renderer/vulkan/DeviceVk.h"
#include "libANGLE/renderer/vulkan/ImageVk.h"
#include "libANGLE/renderer/vulkan/RendererVk.h"
#include "libANGLE/renderer/vulkan/SurfaceVk.h"
#include "libANGLE/renderer/vulkan/SyncVk.h"
#include "libANGLE/renderer/vulkan/TextureVk.h"
#include "libANGLE/renderer/vulkan/VkImageImageSiblingVk.h"

namespace rx
{

namespace
{
// For DesciptorSetUpdates
constexpr size_t kDescriptorBufferInfosInitialSize = 8;
constexpr size_t kDescriptorImageInfosInitialSize  = 4;
constexpr size_t kDescriptorWriteInfosInitialSize =
    kDescriptorBufferInfosInitialSize + kDescriptorImageInfosInitialSize;
constexpr size_t kDescriptorBufferViewsInitialSize = 0;

constexpr VkDeviceSize kMaxStaticBufferSizeToUseBuddyAlgorithm  = 256;
constexpr VkDeviceSize kMaxDynamicBufferSizeToUseBuddyAlgorithm = 4096;

// How often monolithic pipelines should be created, if preferMonolithicPipelinesOverLibraries is
// enabled.  Pipeline creation is typically O(hundreds of microseconds).  A value of 2ms is chosen
// arbitrarily; it ensures that there is always at most a single pipeline job in progress, while
// maintaining a high throughput of 500 pipelines / second for heavier applications.
constexpr double kMonolithicPipelineJobPeriod = 0.002;

// Query surface format and colorspace support.
void GetSupportedFormatColorspaces(VkPhysicalDevice physicalDevice,
                                   const angle::FeaturesVk &featuresVk,
                                   VkSurfaceKHR surface,
                                   std::vector<VkSurfaceFormat2KHR> *surfaceFormatsOut)
{
    ASSERT(surfaceFormatsOut);
    surfaceFormatsOut->clear();

    constexpr VkSurfaceFormat2KHR kSurfaceFormat2Initializer = {
        VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR,
        nullptr,
        {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};

    if (featuresVk.supportsSurfaceCapabilities2Extension.enabled)
    {
        VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo2 = {};
        surfaceInfo2.sType          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
        surfaceInfo2.surface        = surface;
        uint32_t surfaceFormatCount = 0;

        // Query the count first
        VkResult result = vkGetPhysicalDeviceSurfaceFormats2KHR(physicalDevice, &surfaceInfo2,
                                                                &surfaceFormatCount, nullptr);
        ASSERT(result == VK_SUCCESS);
        ASSERT(surfaceFormatCount > 0);

        // Query the VkSurfaceFormat2KHR list
        std::vector<VkSurfaceFormat2KHR> surfaceFormats2(surfaceFormatCount,
                                                         kSurfaceFormat2Initializer);
        result = vkGetPhysicalDeviceSurfaceFormats2KHR(physicalDevice, &surfaceInfo2,
                                                       &surfaceFormatCount, surfaceFormats2.data());
        ASSERT(result == VK_SUCCESS);

        *surfaceFormatsOut = std::move(surfaceFormats2);
    }
    else
    {
        uint32_t surfaceFormatCount = 0;
        // Query the count first
        VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                                               &surfaceFormatCount, nullptr);
        ASSERT(result == VK_SUCCESS);

        // Query the VkSurfaceFormatKHR list
        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount,
                                                      surfaceFormats.data());
        ASSERT(result == VK_SUCCESS);

        // Copy over data from std::vector<VkSurfaceFormatKHR> to std::vector<VkSurfaceFormat2KHR>
        std::vector<VkSurfaceFormat2KHR> surfaceFormats2(surfaceFormatCount,
                                                         kSurfaceFormat2Initializer);
        for (size_t index = 0; index < surfaceFormatCount; index++)
        {
            surfaceFormats2[index].surfaceFormat.format = surfaceFormats[index].format;
        }

        *surfaceFormatsOut = std::move(surfaceFormats2);
    }
}

}  // namespace

// Time interval in seconds that we should try to prune default buffer pools.
constexpr double kTimeElapsedForPruneDefaultBufferPool = 0.25;

// Set to true will log bufferpool stats into INFO stream
#define ANGLE_ENABLE_BUFFER_POOL_STATS_LOGGING 0

DisplayVk::DisplayVk(const egl::DisplayState &state)
    : DisplayImpl(state),
      vk::Context(new RendererVk()),
      mScratchBuffer(1000u),
      mSavedError({VK_SUCCESS, "", "", 0}),
      mSupportedColorspaceFormatsMap{}
{}

DisplayVk::~DisplayVk()
{
    delete mRenderer;
}

egl::Error DisplayVk::initialize(egl::Display *display)
{
    ASSERT(mRenderer != nullptr && display != nullptr);
    angle::Result result = mRenderer->initialize(this, display, getWSIExtension(), getWSILayer());
    ANGLE_TRY(angle::ToEGL(result, this, EGL_NOT_INITIALIZED));
    // Query and cache supported surface format and colorspace for later use.
    initSupportedSurfaceFormatColorspaces();
    return egl::NoError();
}

void DisplayVk::terminate()
{
    mRenderer->reloadVolkIfNeeded();

    ASSERT(mRenderer);
    mRenderer->onDestroy(this);
}

egl::Error DisplayVk::makeCurrent(egl::Display * /*display*/,
                                  egl::Surface * /*drawSurface*/,
                                  egl::Surface * /*readSurface*/,
                                  gl::Context * /*context*/)
{
    // Ensure the appropriate global DebugAnnotator is used
    ASSERT(mRenderer);
    mRenderer->setGlobalDebugAnnotator();

    return egl::NoError();
}

bool DisplayVk::testDeviceLost()
{
    return mRenderer->isDeviceLost();
}

egl::Error DisplayVk::restoreLostDevice(const egl::Display *display)
{
    // A vulkan device cannot be restored, the entire renderer would have to be re-created along
    // with any other EGL objects that reference it.
    return egl::EglBadDisplay();
}

std::string DisplayVk::getRendererDescription()
{
    if (mRenderer)
    {
        return mRenderer->getRendererDescription();
    }
    return std::string();
}

std::string DisplayVk::getVendorString()
{
    if (mRenderer)
    {
        return mRenderer->getVendorString();
    }
    return std::string();
}

std::string DisplayVk::getVersionString(bool includeFullVersion)
{
    if (mRenderer)
    {
        return mRenderer->getVersionString(includeFullVersion);
    }
    return std::string();
}

DeviceImpl *DisplayVk::createDevice()
{
    return new DeviceVk();
}

egl::Error DisplayVk::waitClient(const gl::Context *context)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "DisplayVk::waitClient");
    ContextVk *contextVk = vk::GetImpl(context);
    return angle::ToEGL(contextVk->finishImpl(RenderPassClosureReason::EGLWaitClient), this,
                        EGL_BAD_ACCESS);
}

egl::Error DisplayVk::waitNative(const gl::Context *context, EGLint engine)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "DisplayVk::waitNative");
    return angle::ResultToEGL(waitNativeImpl());
}

angle::Result DisplayVk::waitNativeImpl()
{
    return angle::Result::Continue;
}

SurfaceImpl *DisplayVk::createWindowSurface(const egl::SurfaceState &state,
                                            EGLNativeWindowType window,
                                            const egl::AttributeMap &attribs)
{
    return createWindowSurfaceVk(state, window);
}

SurfaceImpl *DisplayVk::createPbufferSurface(const egl::SurfaceState &state,
                                             const egl::AttributeMap &attribs)
{
    ASSERT(mRenderer);
    return new OffscreenSurfaceVk(state, mRenderer);
}

SurfaceImpl *DisplayVk::createPbufferFromClientBuffer(const egl::SurfaceState &state,
                                                      EGLenum buftype,
                                                      EGLClientBuffer clientBuffer,
                                                      const egl::AttributeMap &attribs)
{
    UNIMPLEMENTED();
    return static_cast<SurfaceImpl *>(0);
}

SurfaceImpl *DisplayVk::createPixmapSurface(const egl::SurfaceState &state,
                                            NativePixmapType nativePixmap,
                                            const egl::AttributeMap &attribs)
{
    UNIMPLEMENTED();
    return static_cast<SurfaceImpl *>(0);
}

ImageImpl *DisplayVk::createImage(const egl::ImageState &state,
                                  const gl::Context *context,
                                  EGLenum target,
                                  const egl::AttributeMap &attribs)
{
    return new ImageVk(state, context);
}

ShareGroupImpl *DisplayVk::createShareGroup()
{
    return new ShareGroupVk();
}

bool DisplayVk::isConfigFormatSupported(VkFormat format) const
{
    // Requires VK_GOOGLE_surfaceless_query extension to be supported.
    ASSERT(mRenderer->getFeatures().supportsSurfacelessQueryExtension.enabled);

    // A format is considered supported if it is supported in atleast 1 colorspace.
    using ColorspaceFormatSetItem =
        const std::pair<const VkColorSpaceKHR, std::unordered_set<VkFormat>>;
    for (ColorspaceFormatSetItem &colorspaceFormatSetItem : mSupportedColorspaceFormatsMap)
    {
        if (colorspaceFormatSetItem.second.count(format) > 0)
        {
            return true;
        }
    }

    return false;
}

bool DisplayVk::isSurfaceFormatColorspacePairSupported(VkSurfaceKHR surface,
                                                       VkFormat format,
                                                       VkColorSpaceKHR colorspace) const
{
    if (mSupportedColorspaceFormatsMap.size() > 0)
    {
        return mSupportedColorspaceFormatsMap.count(colorspace) > 0 &&
               mSupportedColorspaceFormatsMap.at(colorspace).count(format) > 0;
    }
    else
    {
        const angle::FeaturesVk &featuresVk = mRenderer->getFeatures();
        std::vector<VkSurfaceFormat2KHR> surfaceFormats;
        GetSupportedFormatColorspaces(mRenderer->getPhysicalDevice(), featuresVk, surface,
                                      &surfaceFormats);

        if (!featuresVk.supportsSurfaceCapabilities2Extension.enabled)
        {
            if (surfaceFormats.size() == 1u &&
                surfaceFormats[0].surfaceFormat.format == VK_FORMAT_UNDEFINED)
            {
                return true;
            }
        }

        for (const VkSurfaceFormat2KHR &surfaceFormat : surfaceFormats)
        {
            if (surfaceFormat.surfaceFormat.format == format &&
                surfaceFormat.surfaceFormat.colorSpace == colorspace)
            {
                return true;
            }
        }
    }

    return false;
}

bool DisplayVk::isColorspaceSupported(VkColorSpaceKHR colorspace) const
{
    return mSupportedColorspaceFormatsMap.count(colorspace) > 0;
}

void DisplayVk::initSupportedSurfaceFormatColorspaces()
{
    const angle::FeaturesVk &featuresVk = mRenderer->getFeatures();
    if (featuresVk.supportsSurfacelessQueryExtension.enabled &&
        featuresVk.supportsSurfaceCapabilities2Extension.enabled)
    {
        // Use the VK_GOOGLE_surfaceless_query extension to query supported surface formats and
        // colorspaces by using a VK_NULL_HANDLE for the VkSurfaceKHR handle.
        std::vector<VkSurfaceFormat2KHR> surfaceFormats;
        GetSupportedFormatColorspaces(mRenderer->getPhysicalDevice(), featuresVk, VK_NULL_HANDLE,
                                      &surfaceFormats);
        for (const VkSurfaceFormat2KHR &surfaceFormat : surfaceFormats)
        {
            // Cache supported VkFormat and VkColorSpaceKHR for later use
            VkFormat format            = surfaceFormat.surfaceFormat.format;
            VkColorSpaceKHR colorspace = surfaceFormat.surfaceFormat.colorSpace;

            ASSERT(format != VK_FORMAT_UNDEFINED);

            mSupportedColorspaceFormatsMap[colorspace].insert(format);
        }

        ASSERT(mSupportedColorspaceFormatsMap.size() > 0);
    }
    else
    {
        mSupportedColorspaceFormatsMap.clear();
    }
}

ContextImpl *DisplayVk::createContext(const gl::State &state,
                                      gl::ErrorSet *errorSet,
                                      const egl::Config *configuration,
                                      const gl::Context *shareContext,
                                      const egl::AttributeMap &attribs)
{
    return new ContextVk(state, errorSet, mRenderer);
}

StreamProducerImpl *DisplayVk::createStreamProducerD3DTexture(
    egl::Stream::ConsumerType consumerType,
    const egl::AttributeMap &attribs)
{
    UNIMPLEMENTED();
    return static_cast<StreamProducerImpl *>(0);
}

EGLSyncImpl *DisplayVk::createSync(const egl::AttributeMap &attribs)
{
    return new EGLSyncVk(attribs);
}

gl::Version DisplayVk::getMaxSupportedESVersion() const
{
    return mRenderer->getMaxSupportedESVersion();
}

gl::Version DisplayVk::getMaxConformantESVersion() const
{
    return mRenderer->getMaxConformantESVersion();
}

Optional<gl::Version> DisplayVk::getMaxSupportedDesktopVersion() const
{
    return gl::Version{4, 6};
}

egl::Error DisplayVk::validateImageClientBuffer(const gl::Context *context,
                                                EGLenum target,
                                                EGLClientBuffer clientBuffer,
                                                const egl::AttributeMap &attribs) const
{
    switch (target)
    {
        case EGL_VULKAN_IMAGE_ANGLE:
        {
            VkImage *vkImage = reinterpret_cast<VkImage *>(clientBuffer);
            if (!vkImage || *vkImage == VK_NULL_HANDLE)
            {
                return egl::EglBadParameter() << "clientBuffer is invalid.";
            }

            GLenum internalFormat =
                static_cast<GLenum>(attribs.get(EGL_TEXTURE_INTERNAL_FORMAT_ANGLE, GL_NONE));
            switch (internalFormat)
            {
                case GL_RGBA:
                case GL_BGRA_EXT:
                case GL_RGB:
                case GL_RED_EXT:
                case GL_RG_EXT:
                case GL_RGB10_A2_EXT:
                case GL_R16_EXT:
                case GL_RG16_EXT:
                case GL_NONE:
                    break;
                default:
                    return egl::EglBadParameter() << "Invalid EGLImage texture internal format: 0x"
                                                  << std::hex << internalFormat;
            }

            uint64_t hi = static_cast<uint64_t>(attribs.get(EGL_VULKAN_IMAGE_CREATE_INFO_HI_ANGLE));
            uint64_t lo = static_cast<uint64_t>(attribs.get(EGL_VULKAN_IMAGE_CREATE_INFO_LO_ANGLE));
            uint64_t info = ((hi & 0xffffffff) << 32) | (lo & 0xffffffff);
            if (reinterpret_cast<const VkImageCreateInfo *>(info)->sType !=
                VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)
            {
                return egl::EglBadParameter()
                       << "EGL_VULKAN_IMAGE_CREATE_INFO_HI_ANGLE and "
                          "EGL_VULKAN_IMAGE_CREATE_INFO_LO_ANGLE are not pointing to a "
                          "valid VkImageCreateInfo structure.";
            }

            return egl::NoError();
        }
        default:
            return DisplayImpl::validateImageClientBuffer(context, target, clientBuffer, attribs);
    }
}

ExternalImageSiblingImpl *DisplayVk::createExternalImageSibling(const gl::Context *context,
                                                                EGLenum target,
                                                                EGLClientBuffer buffer,
                                                                const egl::AttributeMap &attribs)
{
    switch (target)
    {
        case EGL_VULKAN_IMAGE_ANGLE:
            return new VkImageImageSiblingVk(buffer, attribs);
        default:
            return DisplayImpl::createExternalImageSibling(context, target, buffer, attribs);
    }
}

void DisplayVk::generateExtensions(egl::DisplayExtensions *outExtensions) const
{
    outExtensions->createContextRobustness    = getRenderer()->getNativeExtensions().robustnessEXT;
    outExtensions->surfaceOrientation         = true;
    outExtensions->displayTextureShareGroup   = true;
    outExtensions->displaySemaphoreShareGroup = true;
    outExtensions->robustResourceInitializationANGLE = true;

    // The Vulkan implementation will always say that EGL_KHR_swap_buffers_with_damage is supported.
    // When the Vulkan driver supports VK_KHR_incremental_present, it will use it.  Otherwise, it
    // will ignore the hint and do a regular swap.
    outExtensions->swapBuffersWithDamage = true;

    outExtensions->fenceSync = true;
    outExtensions->waitSync  = true;

    outExtensions->image                 = true;
    outExtensions->imageBase             = true;
    outExtensions->imagePixmap           = false;  // ANGLE does not support pixmaps
    outExtensions->glTexture2DImage      = true;
    outExtensions->glTextureCubemapImage = true;
    outExtensions->glTexture3DImage = getRenderer()->getFeatures().supportsImage2dViewOf3d.enabled;
    outExtensions->glRenderbufferImage = true;
    outExtensions->imageNativeBuffer =
        getRenderer()->getFeatures().supportsAndroidHardwareBuffer.enabled;
    outExtensions->surfacelessContext = true;
    outExtensions->glColorspace       = true;
    outExtensions->imageGlColorspace =
        outExtensions->glColorspace && getRenderer()->getFeatures().supportsImageFormatList.enabled;

#if defined(ANGLE_PLATFORM_ANDROID)
    outExtensions->getNativeClientBufferANDROID = true;
    outExtensions->framebufferTargetANDROID     = true;
#endif  // defined(ANGLE_PLATFORM_ANDROID)

    // EGL_EXT_image_dma_buf_import is only exposed if EGL_EXT_image_dma_buf_import_modifiers can
    // also be exposed.  The Vulkan extensions that support these EGL extensions are not split in
    // the same way; both Vulkan extensions are needed for EGL_EXT_image_dma_buf_import, and with
    // both Vulkan extensions, EGL_EXT_image_dma_buf_import_modifiers is also supportable.
    outExtensions->imageDmaBufImportEXT =
        getRenderer()->getFeatures().supportsExternalMemoryDmaBufAndModifiers.enabled;
    outExtensions->imageDmaBufImportModifiersEXT = outExtensions->imageDmaBufImportEXT;

    // Disable context priority when non-zero memory init is enabled. This enforces a queue order.
    outExtensions->contextPriority = !getRenderer()->getFeatures().allocateNonZeroMemory.enabled;
    outExtensions->noConfigContext = true;

#if defined(ANGLE_PLATFORM_ANDROID)
    outExtensions->nativeFenceSyncANDROID =
        getRenderer()->getFeatures().supportsAndroidNativeFenceSync.enabled;
#endif  // defined(ANGLE_PLATFORM_ANDROID)

#if defined(ANGLE_PLATFORM_GGP)
    outExtensions->ggpStreamDescriptor = true;
    outExtensions->swapWithFrameToken  = getRenderer()->getFeatures().supportsGGPFrameToken.enabled;
#endif  // defined(ANGLE_PLATFORM_GGP)

    outExtensions->bufferAgeEXT = true;

    outExtensions->protectedContentEXT =
        (getRenderer()->getFeatures().supportsProtectedMemory.enabled &&
         getRenderer()->getFeatures().supportsSurfaceProtectedSwapchains.enabled);

    outExtensions->createSurfaceSwapIntervalANGLE = true;

    outExtensions->mutableRenderBufferKHR =
        getRenderer()->getFeatures().supportsSharedPresentableImageExtension.enabled;

    outExtensions->vulkanImageANGLE = true;

    outExtensions->lockSurface3KHR =
        getRenderer()->getFeatures().supportsLockSurfaceExtension.enabled;

    outExtensions->partialUpdateKHR = true;

    outExtensions->timestampSurfaceAttributeANGLE =
        getRenderer()->getFeatures().supportsTimestampSurfaceAttribute.enabled;

    outExtensions->eglColorspaceAttributePassthroughANGLE =
        outExtensions->glColorspace &&
        getRenderer()->getFeatures().eglColorspaceAttributePassthrough.enabled;

    // If EGL_KHR_gl_colorspace extension is supported check if other colorspace extensions
    // can be supported as well.
    if (outExtensions->glColorspace)
    {
        if (isColorspaceSupported(VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT))
        {
            outExtensions->glColorspaceDisplayP3            = true;
            outExtensions->glColorspaceDisplayP3Passthrough = true;
        }

        outExtensions->glColorspaceDisplayP3Linear =
            isColorspaceSupported(VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT);
        outExtensions->glColorspaceScrgb =
            isColorspaceSupported(VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT);
        outExtensions->glColorspaceScrgbLinear =
            isColorspaceSupported(VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
    }
}

void DisplayVk::generateCaps(egl::Caps *outCaps) const
{
    outCaps->textureNPOT = true;
    outCaps->stencil8    = getRenderer()->getNativeExtensions().textureStencil8OES;
}

const char *DisplayVk::getWSILayer() const
{
    return nullptr;
}

bool DisplayVk::isUsingSwapchain() const
{
    return true;
}

bool DisplayVk::getScratchBuffer(size_t requstedSizeBytes,
                                 angle::MemoryBuffer **scratchBufferOut) const
{
    return mScratchBuffer.get(requstedSizeBytes, scratchBufferOut);
}

void DisplayVk::handleError(VkResult result,
                            const char *file,
                            const char *function,
                            unsigned int line)
{
    ASSERT(result != VK_SUCCESS);

    mSavedError.errorCode = result;
    mSavedError.file      = file;
    mSavedError.function  = function;
    mSavedError.line      = line;

    if (result == VK_ERROR_DEVICE_LOST)
    {
        WARN() << "Internal Vulkan error (" << result << "): " << VulkanResultString(result)
               << ", in " << file << ", " << function << ":" << line << ".";
        mRenderer->notifyDeviceLost();
    }
}

// TODO(jmadill): Remove this. http://anglebug.com/3041
egl::Error DisplayVk::getEGLError(EGLint errorCode)
{
    std::stringstream errorStream;
    errorStream << "Internal Vulkan error (" << mSavedError.errorCode
                << "): " << VulkanResultString(mSavedError.errorCode) << ", in " << mSavedError.file
                << ", " << mSavedError.function << ":" << mSavedError.line << ".";
    std::string errorString = errorStream.str();

    return egl::Error(errorCode, 0, std::move(errorString));
}

void DisplayVk::initializeFrontendFeatures(angle::FrontendFeatures *features) const
{
    mRenderer->initializeFrontendFeatures(features);
}

void DisplayVk::populateFeatureList(angle::FeatureList *features)
{
    mRenderer->getFeatures().populateFeatureList(features);
}

ShareGroupVk::ShareGroupVk()
    : mContextsPriority(egl::ContextPriority::InvalidEnum),
      mIsContextsPriorityLocked(false),
      mLastMonolithicPipelineJobTime(0),
      mOrphanNonEmptyBufferBlock(false)
{
    mLastPruneTime = angle::GetCurrentSystemTime();
    mSizeLimitForBuddyAlgorithm[BufferUsageType::Dynamic] =
        kMaxDynamicBufferSizeToUseBuddyAlgorithm;
    mSizeLimitForBuddyAlgorithm[BufferUsageType::Static] = kMaxStaticBufferSizeToUseBuddyAlgorithm;
}

void ShareGroupVk::addContext(ContextVk *contextVk)
{
    // All mContexts must have mContextsPriority set
    ASSERT(mContextsPriority != egl::ContextPriority::InvalidEnum);
    ASSERT(contextVk->getPriority() == mContextsPriority);

    mContexts.insert(contextVk);

    if (contextVk->getState().hasDisplayTextureShareGroup())
    {
        mOrphanNonEmptyBufferBlock = true;
    }
}

void ShareGroupVk::removeContext(ContextVk *contextVk)
{
    mContexts.erase(contextVk);
}

angle::Result ShareGroupVk::unifyContextsPriority(ContextVk *newContextVk)
{
    const egl::ContextPriority newContextPriority = newContextVk->getPriority();
    ASSERT(newContextPriority != egl::ContextPriority::InvalidEnum);

    if (mContextsPriority == egl::ContextPriority::InvalidEnum)
    {
        ASSERT(!mIsContextsPriorityLocked);
        ASSERT(mContexts.empty());
        mContextsPriority = newContextPriority;
        return angle::Result::Continue;
    }

    static_assert(egl::ContextPriority::Low < egl::ContextPriority::Medium);
    static_assert(egl::ContextPriority::Medium < egl::ContextPriority::High);
    if (mContextsPriority >= newContextPriority || mIsContextsPriorityLocked)
    {
        newContextVk->setPriority(mContextsPriority);
        return angle::Result::Continue;
    }

    ANGLE_TRY(updateContextsPriority(newContextVk, newContextPriority));

    return angle::Result::Continue;
}

angle::Result ShareGroupVk::lockDefaultContextsPriority(ContextVk *contextVk)
{
    constexpr egl::ContextPriority kDefaultPriority = egl::ContextPriority::Medium;
    if (!mIsContextsPriorityLocked)
    {
        if (mContextsPriority != kDefaultPriority)
        {
            ANGLE_TRY(updateContextsPriority(contextVk, kDefaultPriority));
        }
        mIsContextsPriorityLocked = true;
    }
    ASSERT(mContextsPriority == kDefaultPriority);
    return angle::Result::Continue;
}

angle::Result ShareGroupVk::updateContextsPriority(ContextVk *contextVk,
                                                   egl::ContextPriority newPriority)
{
    ASSERT(!mIsContextsPriorityLocked);
    ASSERT(newPriority != egl::ContextPriority::InvalidEnum);
    ASSERT(newPriority != mContextsPriority);
    if (mContextsPriority == egl::ContextPriority::InvalidEnum)
    {
        ASSERT(mContexts.empty());
        mContextsPriority = newPriority;
        return angle::Result::Continue;
    }

    vk::ProtectionTypes protectionTypes;
    protectionTypes.set(contextVk->getProtectionType());
    for (ContextVk *ctx : mContexts)
    {
        protectionTypes.set(ctx->getProtectionType());
    }

    {
        vk::ScopedQueueSerialIndex index;
        RendererVk *renderer = contextVk->getRenderer();
        ANGLE_TRY(renderer->allocateScopedQueueSerialIndex(&index));
        ANGLE_TRY(renderer->submitPriorityDependency(contextVk, protectionTypes, mContextsPriority,
                                                     newPriority, index.get()));
    }

    for (ContextVk *ctx : mContexts)
    {
        ASSERT(ctx->getPriority() == mContextsPriority);
        ctx->setPriority(newPriority);
    }
    mContextsPriority = newPriority;

    return angle::Result::Continue;
}

void ShareGroupVk::onDestroy(const egl::Display *display)
{
    RendererVk *renderer = vk::GetImpl(display)->getRenderer();

    for (vk::BufferPoolPointerArray &array : mDefaultBufferPools)
    {
        for (std::unique_ptr<vk::BufferPool> &pool : array)
        {
            if (pool)
            {
                pool->destroy(renderer, mOrphanNonEmptyBufferBlock);
            }
        }
    }

    mPipelineLayoutCache.destroy(renderer);
    mDescriptorSetLayoutCache.destroy(renderer);

    mMetaDescriptorPools[DescriptorSetIndex::UniformsAndXfb].destroy(renderer);
    mMetaDescriptorPools[DescriptorSetIndex::Texture].destroy(renderer);
    mMetaDescriptorPools[DescriptorSetIndex::ShaderResource].destroy(renderer);

    mFramebufferCache.destroy(renderer);
    resetPrevTexture();
}

angle::Result ShareGroupVk::onMutableTextureUpload(ContextVk *contextVk, TextureVk *newTexture)
{
    return mTextureUpload.onMutableTextureUpload(contextVk, newTexture);
}

void ShareGroupVk::onTextureRelease(TextureVk *textureVk)
{
    mTextureUpload.onTextureRelease(textureVk);
}

angle::Result ShareGroupVk::scheduleMonolithicPipelineCreationTask(
    ContextVk *contextVk,
    vk::WaitableMonolithicPipelineCreationTask *taskOut)
{
    ASSERT(contextVk->getFeatures().preferMonolithicPipelinesOverLibraries.enabled);

    // Limit to a single task to avoid hogging all the cores.
    if (mMonolithicPipelineCreationEvent && !mMonolithicPipelineCreationEvent->isReady())
    {
        return angle::Result::Continue;
    }

    // Additionally, rate limit the job postings.
    double currentTime = angle::GetCurrentSystemTime();
    if (currentTime - mLastMonolithicPipelineJobTime < kMonolithicPipelineJobPeriod)
    {
        return angle::Result::Continue;
    }

    mLastMonolithicPipelineJobTime = currentTime;

    const vk::RenderPass *compatibleRenderPass = nullptr;
    // Pull in a compatible RenderPass to be used by the task.  This is done at the last minute,
    // just before the task is scheduled, to minimize the time this reference to the render pass
    // cache is held.  If the render pass cache needs to be cleared, the main thread will wait for
    // the job to complete.
    ANGLE_TRY(contextVk->getCompatibleRenderPass(taskOut->getTask()->getRenderPassDesc(),
                                                 &compatibleRenderPass));
    taskOut->setRenderPass(compatibleRenderPass);

    egl::Display *display = contextVk->getRenderer()->getDisplay();
    mMonolithicPipelineCreationEvent =
        display->getMultiThreadPool()->postWorkerTask(taskOut->getTask());

    taskOut->onSchedule(mMonolithicPipelineCreationEvent);

    return angle::Result::Continue;
}

void ShareGroupVk::waitForCurrentMonolithicPipelineCreationTask()
{
    if (mMonolithicPipelineCreationEvent)
    {
        mMonolithicPipelineCreationEvent->wait();
    }
}

angle::Result TextureUpload::onMutableTextureUpload(ContextVk *contextVk, TextureVk *newTexture)
{
    // This feature is currently disabled in the case of display-level texture sharing.
    ASSERT(!contextVk->hasDisplayTextureShareGroup());

    // If the previous texture is null, it should be set to the current texture. We also have to
    // make sure that the previous texture pointer is still a mutable texture. Otherwise, we skip
    // the optimization.
    if (mPrevUploadedMutableTexture == nullptr || mPrevUploadedMutableTexture->isImmutable())
    {
        mPrevUploadedMutableTexture = newTexture;
        return angle::Result::Continue;
    }

    // Skip the optimization if we have not switched to a new texture yet.
    if (mPrevUploadedMutableTexture == newTexture)
    {
        return angle::Result::Continue;
    }

    // If the mutable texture is consistently specified, we initialize a full mip chain for it.
    if (mPrevUploadedMutableTexture->isMutableTextureConsistentlySpecifiedForFlush())
    {
        ANGLE_TRY(mPrevUploadedMutableTexture->ensureImageInitialized(
            contextVk, ImageMipLevels::FullMipChain));
        contextVk->getPerfCounters().mutableTexturesUploaded++;
    }

    // Update the mutable texture pointer with the new pointer for the next potential flush.
    mPrevUploadedMutableTexture = newTexture;

    return angle::Result::Continue;
}

void TextureUpload::onTextureRelease(TextureVk *textureVk)
{
    if (mPrevUploadedMutableTexture == textureVk)
    {
        resetPrevTexture();
    }
}

// UpdateDescriptorSetsBuilder implementation.
UpdateDescriptorSetsBuilder::UpdateDescriptorSetsBuilder()
{
    // Reserve reasonable amount of spaces so that for majority of apps we don't need to grow at all
    mDescriptorBufferInfos.reserve(kDescriptorBufferInfosInitialSize);
    mDescriptorImageInfos.reserve(kDescriptorImageInfosInitialSize);
    mWriteDescriptorSets.reserve(kDescriptorWriteInfosInitialSize);
    mBufferViews.reserve(kDescriptorBufferViewsInitialSize);
}

UpdateDescriptorSetsBuilder::~UpdateDescriptorSetsBuilder() = default;

template <typename T, const T *VkWriteDescriptorSet::*pInfo>
void UpdateDescriptorSetsBuilder::growDescriptorCapacity(std::vector<T> *descriptorVector,
                                                         size_t newSize)
{
    const T *const oldInfoStart = descriptorVector->empty() ? nullptr : &(*descriptorVector)[0];
    size_t newCapacity          = std::max(descriptorVector->capacity() << 1, newSize);
    descriptorVector->reserve(newCapacity);

    if (oldInfoStart)
    {
        // patch mWriteInfo with new BufferInfo/ImageInfo pointers
        for (VkWriteDescriptorSet &set : mWriteDescriptorSets)
        {
            if (set.*pInfo)
            {
                size_t index = set.*pInfo - oldInfoStart;
                set.*pInfo   = &(*descriptorVector)[index];
            }
        }
    }
}

template <typename T, const T *VkWriteDescriptorSet::*pInfo>
T *UpdateDescriptorSetsBuilder::allocDescriptorInfos(std::vector<T> *descriptorVector, size_t count)
{
    size_t oldSize = descriptorVector->size();
    size_t newSize = oldSize + count;
    if (newSize > descriptorVector->capacity())
    {
        // If we have reached capacity, grow the storage and patch the descriptor set with new
        // buffer info pointer
        growDescriptorCapacity<T, pInfo>(descriptorVector, newSize);
    }
    descriptorVector->resize(newSize);
    return &(*descriptorVector)[oldSize];
}

VkDescriptorBufferInfo *UpdateDescriptorSetsBuilder::allocDescriptorBufferInfos(size_t count)
{
    return allocDescriptorInfos<VkDescriptorBufferInfo, &VkWriteDescriptorSet::pBufferInfo>(
        &mDescriptorBufferInfos, count);
}

VkDescriptorImageInfo *UpdateDescriptorSetsBuilder::allocDescriptorImageInfos(size_t count)
{
    return allocDescriptorInfos<VkDescriptorImageInfo, &VkWriteDescriptorSet::pImageInfo>(
        &mDescriptorImageInfos, count);
}

VkWriteDescriptorSet *UpdateDescriptorSetsBuilder::allocWriteDescriptorSets(size_t count)
{
    size_t oldSize = mWriteDescriptorSets.size();
    size_t newSize = oldSize + count;
    mWriteDescriptorSets.resize(newSize);
    return &mWriteDescriptorSets[oldSize];
}

VkBufferView *UpdateDescriptorSetsBuilder::allocBufferViews(size_t count)
{
    return allocDescriptorInfos<VkBufferView, &VkWriteDescriptorSet::pTexelBufferView>(
        &mBufferViews, count);
}

uint32_t UpdateDescriptorSetsBuilder::flushDescriptorSetUpdates(VkDevice device)
{
    if (mWriteDescriptorSets.empty())
    {
        ASSERT(mDescriptorBufferInfos.empty());
        ASSERT(mDescriptorImageInfos.empty());
        return 0;
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(mWriteDescriptorSets.size()),
                           mWriteDescriptorSets.data(), 0, nullptr);

    uint32_t retVal = static_cast<uint32_t>(mWriteDescriptorSets.size());

    mWriteDescriptorSets.clear();
    mDescriptorBufferInfos.clear();
    mDescriptorImageInfos.clear();
    mBufferViews.clear();

    return retVal;
}

vk::BufferPool *ShareGroupVk::getDefaultBufferPool(RendererVk *renderer,
                                                   VkDeviceSize size,
                                                   uint32_t memoryTypeIndex,
                                                   BufferUsageType usageType)
{
    // First pick allocation algorithm. Buddy algorithm is faster, but waste more memory
    // due to power of two alignment. For smaller size allocation we always use buddy algorithm
    // since align to power of two does not waste too much memory. For dynamic usage, the size
    // threshold for buddy algorithm is relaxed since the performance is more important.
    SuballocationAlgorithm algorithm = size <= mSizeLimitForBuddyAlgorithm[usageType]
                                           ? SuballocationAlgorithm::Buddy
                                           : SuballocationAlgorithm::General;

    if (!mDefaultBufferPools[algorithm][memoryTypeIndex])
    {
        const vk::Allocator &allocator = renderer->getAllocator();
        VkBufferUsageFlags usageFlags  = GetDefaultBufferUsageFlags(renderer);

        VkMemoryPropertyFlags memoryPropertyFlags;
        allocator.getMemoryTypeProperties(memoryTypeIndex, &memoryPropertyFlags);

        std::unique_ptr<vk::BufferPool> pool  = std::make_unique<vk::BufferPool>();
        vma::VirtualBlockCreateFlags vmaFlags = algorithm == SuballocationAlgorithm::Buddy
                                                    ? vma::VirtualBlockCreateFlagBits::BUDDY
                                                    : vma::VirtualBlockCreateFlagBits::GENERAL;
        pool->initWithFlags(renderer, vmaFlags, usageFlags, 0, memoryTypeIndex,
                            memoryPropertyFlags);
        mDefaultBufferPools[algorithm][memoryTypeIndex] = std::move(pool);
    }

    return mDefaultBufferPools[algorithm][memoryTypeIndex].get();
}

void ShareGroupVk::pruneDefaultBufferPools(RendererVk *renderer)
{
    mLastPruneTime = angle::GetCurrentSystemTime();

    // Bail out if no suballocation have been destroyed since last prune.
    if (renderer->getSuballocationDestroyedSize() == 0)
    {
        return;
    }

    for (vk::BufferPoolPointerArray &array : mDefaultBufferPools)
    {
        for (std::unique_ptr<vk::BufferPool> &pool : array)
        {
            if (pool)
            {
                pool->pruneEmptyBuffers(renderer);
            }
        }
    }

    renderer->onBufferPoolPrune();

#if ANGLE_ENABLE_BUFFER_POOL_STATS_LOGGING
    logBufferPools();
#endif
}

bool ShareGroupVk::isDueForBufferPoolPrune(RendererVk *renderer)
{
    // Ensure we periodically prune to maintain the heuristic information
    double timeElapsed = angle::GetCurrentSystemTime() - mLastPruneTime;
    if (timeElapsed > kTimeElapsedForPruneDefaultBufferPool)
    {
        return true;
    }

    // If we have destroyed a lot of memory, also prune to ensure memory gets freed as soon as
    // possible
    if (renderer->getSuballocationDestroyedSize() >= kMaxTotalEmptyBufferBytes)
    {
        return true;
    }

    return false;
}

void ShareGroupVk::calculateTotalBufferCount(size_t *bufferCount, VkDeviceSize *totalSize) const
{
    *bufferCount = 0;
    *totalSize   = 0;
    for (const vk::BufferPoolPointerArray &array : mDefaultBufferPools)
    {
        for (const std::unique_ptr<vk::BufferPool> &pool : array)
        {
            if (pool)
            {
                *bufferCount += pool->getBufferCount();
                *totalSize += pool->getMemorySize();
            }
        }
    }
}

void ShareGroupVk::logBufferPools() const
{
    size_t totalBufferCount;
    VkDeviceSize totalMemorySize;
    calculateTotalBufferCount(&totalBufferCount, &totalMemorySize);

    INFO() << "BufferBlocks count:" << totalBufferCount << " memorySize:" << totalMemorySize / 1024
           << " UnusedBytes/memorySize (KBs):";
    for (const vk::BufferPoolPointerArray &array : mDefaultBufferPools)
    {
        for (const std::unique_ptr<vk::BufferPool> &pool : array)
        {
            if (pool && pool->getBufferCount() > 0)
            {
                std::ostringstream log;
                pool->addStats(&log);
                INFO() << "\t" << log.str();
            }
        }
    }
}
}  // namespace rx
