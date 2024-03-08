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

#if ENABLE(UNIFIED_TEXT_REPLACEMENT)

#include "config.h"
#include "UnifiedTextReplacementController.h"

#include "Logging.h"
#include "WebPage.h"
#include "WebUnifiedTextReplacementContextData.h"
#include <WebCore/BoundaryPoint.h>
#include <WebCore/DocumentInlines.h>
#include <WebCore/DocumentMarkerController.h>
#include <WebCore/Editor.h>
#include <WebCore/FocusController.h>
#include <WebCore/HTMLConverter.h>
#include <WebCore/RenderedDocumentMarker.h>
#include <WebCore/SimpleRange.h>
#include <WebCore/TextIterator.h>
#include <WebCore/VisiblePosition.h>
#include <WebCore/WebContentReader.h>

namespace WebKit {

static void replaceTextInRange(WebCore::LocalFrame& frame, const WebCore::SimpleRange& range, const String& replacementText)
{
    RefPtr document = frame.document();
    if (!document)
        return;

    document->selection().setSelection({ range });

    frame.editor().replaceSelectionWithText(replacementText, WebCore::Editor::SelectReplacement::Yes, WebCore::Editor::SmartReplace::No, WebCore::EditAction::InsertReplacement);
}

static void replaceContentsInRange(WebCore::LocalFrame& frame, const WebCore::SimpleRange& range, WebCore::DocumentFragment& fragment)
{
    RefPtr document = frame.document();
    if (!document)
        return;

    document->selection().setSelection({ range });

    frame.editor().replaceSelectionWithFragment(fragment, WebCore::Editor::SelectReplacement::Yes, WebCore::Editor::SmartReplace::No, WebCore::Editor::MatchStyle::No, WebCore::EditAction::InsertReplacement);
}

static std::optional<WebCore::BoundaryPoint> extendedBoundaryPoint(const WebCore::BoundaryPoint& point, uint64_t characterCount, WebCore::SelectionDirection direction)
{
    auto visiblePosition = WebCore::VisiblePosition { WebCore::makeContainerOffsetPosition(point) };
    for (uint64_t i = 0; i < characterCount; ++i)
        visiblePosition = WebCore::positionOfNextBoundaryOfGranularity(visiblePosition, WebCore::TextGranularity::CharacterGranularity, direction);

    return WebCore::makeBoundaryPoint(visiblePosition);
}

static std::optional<std::tuple<WebCore::Node&, WebCore::DocumentMarker&>> findReplacementMarkerByUUID(WebCore::Document& document, const WTF::UUID& replacementUUID)
{
    RefPtr<WebCore::Node> targetNode;
    WeakPtr<WebCore::DocumentMarker> targetMarker;

    document.markers().forEachOfTypes({ WebCore::DocumentMarker::Type::UnifiedTextReplacement }, [&replacementUUID, &targetNode, &targetMarker] (auto& node, auto& marker) mutable {
        auto data = std::get<WebCore::DocumentMarker::UnifiedTextReplacementData>(marker.data());
        if (data.uuid != replacementUUID)
            return false;

        targetNode = &node;
        targetMarker = &marker;

        return true;
    });

    if (targetNode && targetMarker)
        return { { *targetNode, *targetMarker } };

    return std::nullopt;
}

UnifiedTextReplacementController::UnifiedTextReplacementController(WebPage& webPage)
    : m_webPage(webPage)
{
}

void UnifiedTextReplacementController::willBeginTextReplacementSession(const WTF::UUID& uuid, CompletionHandler<void(const Vector<WebUnifiedTextReplacementContextData>&)>&& completionHandler)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::willBeginTextReplacementSession (%s)", uuid.toString().utf8().data());

    if (!m_webPage) {
        ASSERT_NOT_REACHED();
        completionHandler({ });
        return;
    }

    RefPtr corePage = m_webPage->corePage();
    if (!corePage) {
        ASSERT_NOT_REACHED();
        completionHandler({ });
        return;
    }

    auto contextRange = m_webPage->autocorrectionContextRange();
    if (!contextRange) {
        RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::willBeginTextReplacementSession (%s) => no context range", uuid.toString().utf8().data());
        completionHandler({ });
        return;
    }

    auto liveRange = createLiveRange(*contextRange);

    ASSERT(!m_contextRanges.contains(uuid));
    m_contextRanges.set(uuid, liveRange);

    RefPtr frame = corePage->checkedFocusController()->focusedOrMainFrame();
    if (!frame)
        return completionHandler({ });

    auto selectedTextRange = frame->selection().selection().firstRange();

    auto attributedStringFromRange = attributedString(*contextRange);
    auto selectedTextCharacterRange = WebCore::characterRange(*contextRange, *selectedTextRange);

    completionHandler({ { WTF::UUID { 0 }, attributedStringFromRange, selectedTextCharacterRange } });
}

void UnifiedTextReplacementController::didBeginTextReplacementSession(const WTF::UUID& uuid, const Vector<WebKit::WebUnifiedTextReplacementContextData>& contexts)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::didBeginTextReplacementSession (%s) [received contexts: %zu]", uuid.toString().utf8().data(), contexts.size());
}

void UnifiedTextReplacementController::textReplacementSessionDidReceiveReplacements(const WTF::UUID& uuid, const Vector<WebTextReplacementData>& replacements, const WebUnifiedTextReplacementContextData& context, bool finished)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveReplacements (%s) [received replacements: %zu, finished: %d]", uuid.toString().utf8().data(), replacements.size(), finished);

    if (!m_webPage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr corePage = m_webPage->corePage();
    if (!corePage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr frame = corePage->checkedFocusController()->focusedOrMainFrame();
    if (!frame)
        return;

    ASSERT(m_contextRanges.contains(uuid));

    RefPtr liveRange = m_contextRanges.get(uuid);
    auto sessionRange = makeSimpleRange(liveRange);
    if (!sessionRange) {
        ASSERT_NOT_REACHED();
        return;
    }

    frame->selection().clear();

    size_t additionalOffset = 0;

    for (const auto& replacementData : replacements) {
        auto locationWithOffset = replacementData.originalRange.location + additionalOffset;

        auto originalRangeWithOffset = WebCore::CharacterRange { locationWithOffset, replacementData.originalRange.length };
        auto resolvedRange = resolveCharacterRange(*sessionRange, originalRangeWithOffset);

        replaceTextInRange(*frame, resolvedRange, replacementData.replacement);

        auto newRangeWithOffset = WebCore::CharacterRange { locationWithOffset, replacementData.replacement.length() };
        auto newResolvedRange = resolveCharacterRange(*sessionRange, newRangeWithOffset);

        auto markerData = WebCore::DocumentMarker::UnifiedTextReplacementData { replacementData.originalString.string, replacementData.uuid, WebCore::DocumentMarker::UnifiedTextReplacementData::State::Pending };
        addMarker(resolvedRange, WebCore::DocumentMarker::Type::UnifiedTextReplacement, markerData);

        additionalOffset += replacementData.replacement.length() - replacementData.originalRange.length;
    }
}

void UnifiedTextReplacementController::textReplacementSessionDidUpdateStateForReplacement(const WTF::UUID& uuid, WebTextReplacementData::State state, const WebTextReplacementData& replacement, const WebUnifiedTextReplacementContextData& context)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidUpdateStateForReplacement (%s) [new state: %hhu, replacement: %s]", uuid.toString().utf8().data(), enumToUnderlyingType(state), replacement.uuid.toString().utf8().data());

    if (!m_webPage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr corePage = m_webPage->corePage();
    if (!corePage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr frame = corePage->checkedFocusController()->focusedOrMainFrame();
    if (!frame)
        return;

    RefPtr document = frame->document();
    if (!document) {
        ASSERT_NOT_REACHED();
        return;
    }

    if (state != WebTextReplacementData::State::Committed && state != WebTextReplacementData::State::Reverted)
        return;

    auto nodeAndMarker = findReplacementMarkerByUUID(*document, replacement.uuid);
    if (!nodeAndMarker) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto& [node, marker] = *nodeAndMarker;

    auto rangeToReplace = makeSimpleRange(node, marker);

    auto oldData = std::get<WebCore::DocumentMarker::UnifiedTextReplacementData>(marker.data());

    auto offsetRange = WebCore::OffsetRange { marker.startOffset(), marker.endOffset() };
    document->markers().removeMarkers(node, offsetRange, { WebCore::DocumentMarker::Type::UnifiedTextReplacement });

    auto [newText, newState] = [&]() -> std::tuple<String, WebCore::DocumentMarker::UnifiedTextReplacementData::State> {
        switch (state) {
        case WebTextReplacementData::State::Committed:
            return std::make_tuple(replacement.replacement, WebCore::DocumentMarker::UnifiedTextReplacementData::State::Committed);

        case WebTextReplacementData::State::Reverted:
            return std::make_tuple(oldData.originalText, WebCore::DocumentMarker::UnifiedTextReplacementData::State::Reverted);

        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }();

    replaceTextInRange(*frame, rangeToReplace, newText);

    auto newData = WebCore::DocumentMarker::UnifiedTextReplacementData { oldData.originalText, oldData.uuid, newState };
    document->markers().addMarker(node, WebCore::DocumentMarker { WebCore::DocumentMarker::Type::UnifiedTextReplacement, offsetRange, WTFMove(newData) });
}

void UnifiedTextReplacementController::didEndTextReplacementSession(const WTF::UUID& uuid, bool accepted)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::didEndTextReplacementSession (%s) [accepted: %d]", uuid.toString().utf8().data(), accepted);

    if (!m_webPage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr corePage = m_webPage->corePage();
    if (!corePage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr frame = corePage->checkedFocusController()->focusedOrMainFrame();
    if (!frame)
        return;
    RefPtr document = frame->document();
    if (!document) {
        ASSERT_NOT_REACHED();
        return;
    }

    // FIXME: Associate the markers with a specific session, and only modify the markers which belong
    // to this session.

    if (!accepted) {
        document->markers().forEachOfTypes({ WebCore::DocumentMarker::Type::UnifiedTextReplacement }, [&frame] (auto& node, auto& marker) mutable {
            auto data = std::get<WebCore::DocumentMarker::UnifiedTextReplacementData>(marker.data());
            if (data.state == WebCore::DocumentMarker::UnifiedTextReplacementData::State::Reverted)
                return false;

            auto rangeToReplace = makeSimpleRange(node, marker);
            replaceTextInRange(*frame, rangeToReplace, data.originalText);

            return false;
        });
    }

    document->markers().removeMarkers({ WebCore::DocumentMarker::Type::UnifiedTextReplacement });

    m_contextRanges.remove(uuid);
    m_originalDocumentNodes.remove(uuid);
    m_replacedDocumentNodes.remove(uuid);
}

void UnifiedTextReplacementController::textReplacementSessionDidReceiveTextWithReplacementRange(const WTF::UUID& uuid, const WebCore::AttributedString& attributedText, const WebCore::CharacterRange& range, const WebUnifiedTextReplacementContextData& context)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveTextWithReplacementRange (%s) [range: %llu, %llu]", uuid.toString().utf8().data(), range.location, range.length);

    if (!m_webPage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr corePage = m_webPage->corePage();
    if (!corePage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr frame = corePage->checkedFocusController()->focusedOrMainFrame();
    if (!frame) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr document = frame->document();
    if (!document) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr liveRange = m_contextRanges.get(uuid);
    auto sessionRange = makeSimpleRange(liveRange);
    if (!sessionRange) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto sessionRangeCharacterCount = WebCore::characterCount(*sessionRange);

    frame->selection().clear();

    auto resolvedRange = resolveCharacterRange(*sessionRange, range);

    if (!m_originalDocumentNodes.contains(uuid)) {
        auto contents = liveRange->cloneContents();
        if (contents.hasException()) {
            RELEASE_LOG_ERROR(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveTextWithReplacementRange (%s) => exception when cloning contents", uuid.toString().utf8().data());
            return;
        }

        m_originalDocumentNodes.set(uuid, contents.returnValue()); // Deep clone.
    }

    RefPtr fragment = WebCore::createFragment(*frame, attributedText.nsAttributedString().get(), WebCore::AddResources::No);
    if (!fragment) {
        ASSERT_NOT_REACHED();
        return;
    }

    replaceContentsInRange(*frame, resolvedRange, *fragment);

    auto selectedTextRange = frame->selection().selection().firstRange();
    if (!selectedTextRange) {
        ASSERT_NOT_REACHED();
        return;
    }

    if (UNLIKELY(sessionRangeCharacterCount < range.location + range.length)) {
        RELEASE_LOG_ERROR(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveTextWithReplacementRange (%s) => trying to replace a range larger than the context range (context range count: %llu, range.location %llu, range.length %llu)", uuid.toString().utf8().data(), sessionRangeCharacterCount, range.location, range.length);
        ASSERT_NOT_REACHED();
        return;
    }

    auto extendedSelectedTextRangeStartPoint = extendedBoundaryPoint(selectedTextRange->start, range.location, WebCore::SelectionDirection::Backward);
    auto extendedSelectionTextRangeEndPoint = extendedBoundaryPoint(selectedTextRange->end, sessionRangeCharacterCount - range.length - range.location, WebCore::SelectionDirection::Forward);

    if (!extendedSelectedTextRangeStartPoint || !extendedSelectionTextRangeEndPoint) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto updatedLiveRange = createLiveRange({ *extendedSelectedTextRangeStartPoint, *extendedSelectionTextRangeEndPoint });

    m_contextRanges.set(uuid, updatedLiveRange);
}

void UnifiedTextReplacementController::textReplacementSessionDidReceiveEditAction(const WTF::UUID& uuid, WebKit::WebTextReplacementData::EditAction action)
{
    RELEASE_LOG(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveEditAction (%s) [action: %hhu]", uuid.toString().utf8().data(), enumToUnderlyingType(action));

    if (!m_webPage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr corePage = m_webPage->corePage();
    if (!corePage) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr frame = corePage->checkedFocusController()->focusedOrMainFrame();
    if (!frame) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr document = frame->document();
    if (!document) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr liveRange = m_contextRanges.get(uuid);
    auto sessionRange = makeSimpleRange(liveRange);
    if (!sessionRange) {
        ASSERT_NOT_REACHED();
        return;
    }

    if (m_originalDocumentNodes.isEmpty())
        return;

    auto contents = liveRange->cloneContents();
    if (contents.hasException()) {
        RELEASE_LOG_ERROR(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveEditAction (%s) => exception when cloning contents", uuid.toString().utf8().data());
        return;
    }

    switch (action) {
    case WebKit::WebTextReplacementData::EditAction::Undo: {
        RefPtr originalFragment = m_originalDocumentNodes.take(uuid);
        if (!originalFragment) {
            ASSERT_NOT_REACHED();
            return;
        }

        m_replacedDocumentNodes.set(uuid, contents.returnValue()); // Deep clone.
        replaceContentsInRange(*frame, *sessionRange, *originalFragment);

        break;
    }

    case WebKit::WebTextReplacementData::EditAction::Redo: {
        RefPtr originalFragment = m_replacedDocumentNodes.take(uuid);
        if (!originalFragment) {
            ASSERT_NOT_REACHED();
            return;
        }

        m_replacedDocumentNodes.set(uuid, contents.returnValue()); // Deep clone.
        replaceContentsInRange(*frame, *sessionRange, *originalFragment);

        break;
    }

    case WebKit::WebTextReplacementData::EditAction::UndoAll: {
        RefPtr originalFragment = m_originalDocumentNodes.take(uuid);
        if (!originalFragment) {
            ASSERT_NOT_REACHED();
            return;
        }

        replaceContentsInRange(*frame, *sessionRange, *originalFragment);
        m_replacedDocumentNodes.remove(uuid);

        break;
    }
    }

    auto selectedTextRange = frame->selection().selection().firstRange();
    auto updatedLiveRange = createLiveRange(selectedTextRange);

    if (!updatedLiveRange) {
        ASSERT_NOT_REACHED();
        return;
    }

    m_contextRanges.set(uuid, *updatedLiveRange);

    switch (action) {
    case WebKit::WebTextReplacementData::EditAction::Undo:
    case WebKit::WebTextReplacementData::EditAction::UndoAll: {
        auto updatedContents = updatedLiveRange->cloneContents();
        if (updatedContents.hasException()) {
            RELEASE_LOG_ERROR(UnifiedTextReplacement, "UnifiedTextReplacementController::textReplacementSessionDidReceiveEditAction (%s) => exception when cloning contents after action", uuid.toString().utf8().data());
            return;
        }

        m_originalDocumentNodes.set(uuid, updatedContents.returnValue()); // Deep clone.

        break;
    }

    case WebKit::WebTextReplacementData::EditAction::Redo:
        break;
    }
}

} // namespace WebKit

#endif
