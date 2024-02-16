/*
 * Copyright (C) 2024 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PathSkia.h"

#if USE(SKIA)
#include "GraphicsContextSkia.h"
#include "NotImplemented.h"
#include "PathStream.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

Ref<PathSkia> PathSkia::create()
{
    return adoptRef(*new PathSkia);
}

Ref<PathSkia> PathSkia::create(const PathSegment& segment)
{
    auto pathSkia = PathSkia::create();
    pathSkia->addSegment(segment);
    return pathSkia;
}

Ref<PathSkia> PathSkia::create(const PathStream& stream)
{
    auto pathSkia = PathSkia::create();
    for (auto& segment : stream.segments())
        pathSkia->addSegment(segment);
    return pathSkia;
}

Ref<PathSkia> PathSkia::create(SkPath&& platformPath, RefPtr<PathStream>&& elementsStream)
{
    return adoptRef(*new PathSkia(WTFMove(platformPath), WTFMove(elementsStream)));
}

PathSkia::PathSkia()
    : m_elementsStream(PathStream::create())
{
}

PathSkia::PathSkia(SkPath&& platformPath, RefPtr<PathStream>&& elementsStream)
    : m_platformPath(WTFMove(platformPath))
    , m_elementsStream(WTFMove(elementsStream))
{
}

Ref<PathImpl> PathSkia::copy() const
{
    SkPath platformPathCopy = m_platformPath;
    auto elementsStream = m_elementsStream ? RefPtr<PathImpl> { m_elementsStream->copy() } : nullptr;
    return PathSkia::create(WTFMove(platformPathCopy), downcast<PathStream>(WTFMove(elementsStream)));
}

PlatformPathPtr PathSkia::platformPath() const
{
    return const_cast<SkPath*>(&m_platformPath);
}

void PathSkia::add(PathMoveTo moveTo)
{
    m_platformPath.moveTo(SkFloatToScalar(moveTo.point.x()), SkFloatToScalar(moveTo.point.y()));
    if (m_elementsStream)
        m_elementsStream->add(moveTo);
}

void PathSkia::add(PathLineTo lineTo)
{
    m_platformPath.lineTo(SkFloatToScalar(lineTo.point.x()), SkFloatToScalar(lineTo.point.y()));
    if (m_elementsStream)
        m_elementsStream->add(lineTo);
}

void PathSkia::add(PathQuadCurveTo quadTo)
{
    m_platformPath.quadTo(SkFloatToScalar(quadTo.controlPoint.x()), SkFloatToScalar(quadTo.controlPoint.y()), SkFloatToScalar(quadTo.endPoint.x()), SkFloatToScalar(quadTo.endPoint.y()));
    if (m_elementsStream)
        m_elementsStream->add(quadTo);
}

void PathSkia::add(PathBezierCurveTo cubicTo)
{
    m_platformPath.cubicTo(SkFloatToScalar(cubicTo.controlPoint1.x()), SkFloatToScalar(cubicTo.controlPoint1.y()), SkFloatToScalar(cubicTo.controlPoint2.x()), SkFloatToScalar(cubicTo.controlPoint2.y()),
        SkFloatToScalar(cubicTo.endPoint.x()), SkFloatToScalar(cubicTo.endPoint.y()));
    if (m_elementsStream)
        m_elementsStream->add(cubicTo);
}

void PathSkia::add(PathArcTo)
{
    notImplemented();
}

void PathSkia::add(PathArc arc)
{
    auto x = SkFloatToScalar(arc.center.x());
    auto y = SkFloatToScalar(arc.center.y());
    auto radius = SkFloatToScalar(arc.radius);
    SkRect oval = { x - radius, y - radius, x + radius, y + radius };

    const float twoPI = 2 * piFloat;
    auto startAngle = fmodf(arc.startAngle, twoPI);
    if (startAngle < 0) {
        startAngle += twoPI;
        if (startAngle >= twoPI)
            startAngle -= twoPI;
    }
    float delta = startAngle - arc.startAngle;
    auto endAngle = arc.endAngle + delta;
    if (arc.direction == RotationDirection::Clockwise && endAngle - startAngle >= twoPI)
        endAngle = startAngle + twoPI;
    else if (arc.direction == RotationDirection::Counterclockwise && startAngle - endAngle >= twoPI)
        endAngle = startAngle - twoPI;
    else if (arc.direction == RotationDirection::Clockwise && startAngle > endAngle)
        endAngle = startAngle + (twoPI - fmodf(startAngle - endAngle, twoPI));
    else if (arc.direction == RotationDirection::Counterclockwise && startAngle < endAngle)
        endAngle = startAngle - (twoPI - fmodf(endAngle - startAngle, twoPI));

    auto sweepAngle = endAngle - startAngle;
    SkScalar startDegrees = SkFloatToScalar(startAngle * 180 / piFloat);
    SkScalar sweepDegrees = SkFloatToScalar(sweepAngle * 180 / piFloat);
    SkScalar s360 = SkIntToScalar(360);

    if (SkScalarNearlyEqual(sweepDegrees, s360) || SkScalarNearlyEqual(sweepDegrees, -s360)) {
        SkScalar startOver90 = startDegrees / SkIntToScalar(90);
        SkScalar startOver90I = SkScalarRoundToScalar(startOver90);
        SkScalar startIndex = std::fmod(startOver90I + SkIntToScalar(1), SkIntToScalar(4));
        m_platformPath.addOval(oval, sweepDegrees > 0 ? SkPathDirection::kCW : SkPathDirection::kCCW, static_cast<unsigned>(startIndex));
    } else
        m_platformPath.arcTo(oval, startDegrees, sweepDegrees, false);

    if (m_elementsStream)
        m_elementsStream->add(arc);
}

void PathSkia::add(PathClosedArc closedArc)
{
    add(closedArc.arc);
    add(PathCloseSubpath());
}

void PathSkia::add(PathEllipse)
{
    notImplemented();
}

void PathSkia::add(PathEllipseInRect ellipseInRect)
{
    m_platformPath.addOval(ellipseInRect.rect);
    if (m_elementsStream)
        m_elementsStream->add(ellipseInRect);
}

void PathSkia::add(PathRect rect)
{
    m_platformPath.addRect(rect.rect);
    if (m_elementsStream)
        m_elementsStream->addLinesForRect(rect.rect);
}

void PathSkia::add(PathRoundedRect roundedRect)
{
    addBeziersForRoundedRect(roundedRect.roundedRect);
}

void PathSkia::add(PathCloseSubpath)
{
    m_platformPath.close();
    if (m_elementsStream)
        m_elementsStream->add(PathCloseSubpath { });
}

void PathSkia::addPath(const PathSkia&, const AffineTransform&)
{
    notImplemented();
}

void PathSkia::applySegments(const PathSegmentApplier&) const
{
    notImplemented();
}

bool PathSkia::applyElements(const PathElementApplier& applier) const
{
    if (m_elementsStream && m_elementsStream->applyElements(applier))
        return true;

    auto convertPoints = [](FloatPoint dst[], const SkPoint src[], int count) {
        for (int i = 0; i < count; i++) {
            dst[i].setX(SkScalarToFloat(src[i].fX));
            dst[i].setY(SkScalarToFloat(src[i].fY));
        }
    };

    SkPath::RawIter iter(m_platformPath);
    SkPoint skPoints[4];
    PathElement pathElement;
    while (true) {
        switch (iter.next(skPoints)) {
        case SkPath::kMove_Verb:
            pathElement.type = PathElement::Type::MoveToPoint;
            convertPoints(pathElement.points, &skPoints[0], 1);
            break;
        case SkPath::kLine_Verb:
            pathElement.type = PathElement::Type::AddLineToPoint;
            convertPoints(pathElement.points, &skPoints[1], 1);
            break;
        case SkPath::kQuad_Verb:
            pathElement.type = PathElement::Type::AddQuadCurveToPoint;
            convertPoints(pathElement.points, &skPoints[1], 2);
            break;
        case SkPath::kCubic_Verb:
            pathElement.type = PathElement::Type::AddCurveToPoint;
            convertPoints(pathElement.points, &skPoints[1], 3);
            break;
        case SkPath::kConic_Verb:
            notImplemented();
            break;
        case SkPath::kClose_Verb:
            pathElement.type = PathElement::Type::CloseSubpath;
            break;
        case SkPath::kDone_Verb:
            return true;
        }
        applier(pathElement);
    }
    return true;
}

bool PathSkia::isEmpty() const
{
    return m_platformPath.isEmpty();
}

FloatPoint PathSkia::currentPoint() const
{
    if (m_platformPath.countPoints() > 0) {
        SkPoint lastPoint;
        m_platformPath.getLastPt(&lastPoint);
        return { SkScalarToFloat(lastPoint.fX), SkScalarToFloat(lastPoint.fY) };
    }

    return { };
}

bool PathSkia::transform(const AffineTransform& matrix)
{
    m_platformPath.transform(matrix, nullptr);
    return true;
}

bool PathSkia::contains(const FloatPoint& point, WindRule windRule) const
{
    if (isEmpty() || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return false;

    auto toSkiaFillType = [](const WindRule& windRule) -> SkPathFillType {
        switch (windRule) {
        case WindRule::EvenOdd:
            return SkPathFillType::kEvenOdd;
        case WindRule::NonZero:
            return SkPathFillType::kWinding;
        }

        return SkPathFillType::kWinding;
    };

    SkScalar x = point.x();
    SkScalar y = point.y();
    auto fillType = toSkiaFillType(windRule);
    if (m_platformPath.getFillType() != fillType) {
        SkPath pathCopy = m_platformPath;
        pathCopy.setFillType(fillType);
        return pathCopy.contains(x, y);
    }
    return m_platformPath.contains(x, y);
}

bool PathSkia::strokeContains(const FloatPoint&, const Function<void(GraphicsContext&)>&) const
{
    if (isEmpty())
        return false;

    notImplemented();
    return false;
}

FloatRect PathSkia::fastBoundingRect() const
{
    if (m_elementsStream)
        return m_elementsStream->fastBoundingRect();

    return boundingRect();
}

FloatRect PathSkia::boundingRect() const
{
    return m_platformPath.getBounds();
}

FloatRect PathSkia::strokeBoundingRect(const Function<void(GraphicsContext&)>&) const
{
    if (isEmpty())
        return { };

    // FIXME: Respect stroke style!
    notImplemented();
    return m_platformPath.getBounds();
}

} // namespace WebCore

#endif // USE(SKIA)
