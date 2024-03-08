/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ModelProcessModelPlayer.h"

#if ENABLE(MODEL_PROCESS)

#include "ModelProcessModelPlayerProxy.h"
#include "ModelProcessModelPlayerProxyMessages.h"
#include "WebPage.h"
#include "WebProcess.h"

namespace WebKit {

Ref<ModelProcessModelPlayer> ModelProcessModelPlayer::create(WebCore::ModelPlayerIdentifier identifier, WebPage& page, WebCore::ModelPlayerClient& client)
{
    return adoptRef(*new ModelProcessModelPlayer(identifier, page, client));
}

ModelProcessModelPlayer::ModelProcessModelPlayer(WebCore::ModelPlayerIdentifier identifier, WebPage& page, WebCore::ModelPlayerClient& client)
    : m_id { identifier }
    , m_page { page }
    , m_client { client }
{
    RELEASE_LOG(ModelElement, "%p - ModelProcessModelPlayer spawned id=%" PRIu64, this, m_id.toUInt64());
    send(Messages::ModelProcessModelPlayerProxy::CreateLayer());
}

ModelProcessModelPlayer::~ModelProcessModelPlayer()
{
    RELEASE_LOG(ModelElement, "%p - ModelProcessModelPlayer deallocating id=%" PRIu64, this, m_id.toUInt64());
}

template<typename T>
ALWAYS_INLINE void ModelProcessModelPlayer::send(T&& message)
{
    WebProcess::singleton().modelProcessModelPlayerManager().modelProcessConnection().connection().send(std::forward<T>(message), m_id);
}

// Messages
void ModelProcessModelPlayer::didCreateLayer(LayerHostingContextIdentifier identifier)
{
    RELEASE_LOG(ModelElement, "%p - ModelProcessModelPlayer obtained new layerHostingContextIdentifier id=%" PRIu64, this, m_id.toUInt64());
    m_layerHostingContextIdentifier = identifier;
    m_client->didUpdateLayerHostingContextIdentifier(*this, identifier);
}

// ModelPlayer
void ModelProcessModelPlayer::load(WebCore::Model& model, WebCore::LayoutSize size)
{
    RELEASE_LOG(ModelElement, "%p - ModelProcessModelPlayer load model id=%" PRIu64, this, m_id.toUInt64());
    send(Messages::ModelProcessModelPlayerProxy::LoadModel(model, size));
}

void ModelProcessModelPlayer::sizeDidChange(WebCore::LayoutSize size)
{
    RELEASE_LOG(ModelElement, "%p - ModelProcessModelPlayer size did change to w=%f h=%f id=%" PRIu64, this, size.width().toFloat(), size.height().toFloat(), m_id.toUInt64());
    send(Messages::ModelProcessModelPlayerProxy::SizeDidChange(size));
}

PlatformLayer* ModelProcessModelPlayer::layer()
{
    return nullptr;
}

void ModelProcessModelPlayer::handleMouseDown(const WebCore::LayoutPoint&, MonotonicTime)
{
}

void ModelProcessModelPlayer::handleMouseMove(const WebCore::LayoutPoint&, MonotonicTime)
{
}

void ModelProcessModelPlayer::handleMouseUp(const WebCore::LayoutPoint&, MonotonicTime)
{
}

void ModelProcessModelPlayer::enterFullscreen()
{
}

void ModelProcessModelPlayer::getCamera(CompletionHandler<void(std::optional<WebCore::HTMLModelElementCamera>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void ModelProcessModelPlayer::setCamera(WebCore::HTMLModelElementCamera camera, CompletionHandler<void(bool success)>&& completionHandler)
{
    completionHandler(false);
}

void ModelProcessModelPlayer::isPlayingAnimation(CompletionHandler<void(std::optional<bool>&&)>&& completionHandler)
{
    completionHandler(false);
}

void ModelProcessModelPlayer::setAnimationIsPlaying(bool isPlaying, CompletionHandler<void(bool success)>&& completionHandler)
{
    completionHandler(false);
}

void ModelProcessModelPlayer::isLoopingAnimation(CompletionHandler<void(std::optional<bool>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void ModelProcessModelPlayer::setIsLoopingAnimation(bool isLooping, CompletionHandler<void(bool success)>&& completionHandler)
{
    completionHandler(false);
}

void ModelProcessModelPlayer::animationDuration(CompletionHandler<void(std::optional<Seconds>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void ModelProcessModelPlayer::animationCurrentTime(CompletionHandler<void(std::optional<Seconds>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void ModelProcessModelPlayer::setAnimationCurrentTime(Seconds currentTime, CompletionHandler<void(bool success)>&& completionHandler)
{
    completionHandler(false);
}

void ModelProcessModelPlayer::hasAudio(CompletionHandler<void(std::optional<bool>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void ModelProcessModelPlayer::isMuted(CompletionHandler<void(std::optional<bool>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void ModelProcessModelPlayer::setIsMuted(bool isMuted, CompletionHandler<void(bool success)>&& completionHandler)
{
    completionHandler(false);
}

Vector<RetainPtr<id>> ModelProcessModelPlayer::accessibilityChildren()
{
    return { };
}

}

#endif // ENABLE(MODEL_PROCESS)
